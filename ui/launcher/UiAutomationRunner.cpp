/** @file UiAutomationRunner.cpp
 *  @brief Implements orchestration for launcher UI automation execution and reporting. */
#include "ui/launcher/UiAutomationRunner.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_set>

// clang-format off
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
// clang-format on

#include "core/Logger.h"
#include "ui/launcher/LauncherEditorShell.h"
#include "ui/launcher/UiAutomationConfig.h"
#include "ui/launcher/UiTestHarness.h"

#ifdef HORO_STANDALONE_UI_AUTOMATION
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_test_engine/imgui_te_context.h>
#include <imgui_test_engine/imgui_te_engine.h>
#endif

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

namespace Horo {
#ifdef HORO_STANDALONE_UI_AUTOMATION
namespace {
namespace fs = std::filesystem;
constexpr const char *kFfmpegVideoParams =
    "-hide_banner -loglevel error -r $FPS -f rawvideo -pix_fmt rgba -s "
    "$WIDTHx$HEIGHT -i - -threads 0 -y -preset ultrafast -pix_fmt yuv420p -crf "
    "20 $OUTPUT";
constexpr const char *kFfmpegGifParams =
    "-hide_banner -loglevel error -r $FPS -f rawvideo -pix_fmt rgba -s "
    "$WIDTHx$HEIGHT -i - -threads 0 -y -filter_complex \"split=2 [a] [b]; [a] "
    "palettegen [pal]; "
    "[b] [pal] paletteuse\" $OUTPUT";

/** @brief Temporarily redirects home-directory environment variables for automation isolation. */
struct HomeDirGuard {
  std::string previousUserProfile = {};
  std::string previousHomeDrive = {};
  std::string previousHomePath = {};
  std::string previousHome = {};

  HomeDirGuard(const HomeDirGuard &) = delete;

  HomeDirGuard &operator=(const HomeDirGuard &) = delete;

  HomeDirGuard(HomeDirGuard &&) = delete;

  HomeDirGuard &operator=(HomeDirGuard &&) = delete;

  static std::string ReadEnv(const char *name) {
    if (!name || !*name)
      return {};
#if defined(_MSC_VER)
    size_t len = 0;
    if (getenv_s(&len, nullptr, 0, name) != 0 || len <= 1)
      return {};
    std::string value(len, '\0');
    if (getenv_s(&len, value.data(), value.size(), name) != 0 || len <= 1)
      return {};
    value.resize(len - 1);
    return value;
#else
    const char *value = std::getenv(name);
    return value ? std::string(value) : std::string();
#endif
  }

  explicit HomeDirGuard(const fs::path &nextHome) {
    previousUserProfile = ReadEnv("USERPROFILE");
    previousHomeDrive = ReadEnv("HOMEDRIVE");
    previousHomePath = ReadEnv("HOMEPATH");
    previousHome = ReadEnv("HOME");
#ifdef _WIN32
    _putenv_s("USERPROFILE", nextHome.string().c_str());
    _putenv_s("HOMEDRIVE", "");
    _putenv_s("HOMEPATH", "");
#else
    setenv("HOME", nextHome.string().c_str(), 1);
#endif
  }

  ~HomeDirGuard() {

#ifdef _WIN32
    _putenv_s("USERPROFILE", previousUserProfile.c_str());
    _putenv_s("HOMEDRIVE", previousHomeDrive.c_str());
    _putenv_s("HOMEPATH", previousHomePath.c_str());
#else
    if (previousHome.empty()) {
      unsetenv("HOME");
    } else {
      setenv("HOME", previousHome.c_str(), 1);
    }
#endif
  }
};

/** @brief Reads a boolean value from the named environment variable. */
bool ParseBoolEnv(const char *name) {
  if (!name || !*name)
    return false;
  const std::string value = HomeDirGuard::ReadEnv(name);
  return ParseUiAutomationBoolValue(value, false);
}

/** @brief Reads a boolean value from the named environment variable, defaulting to true. */
bool ParseBoolEnvDefaultTrue(const char *name) {
  if (!name || !*name)
    return true;
  const std::string value = HomeDirGuard::ReadEnv(name);
  return ParseUiAutomationBoolValue(value, true);
}

/** @brief Reads a non-negative integer from the named environment variable. */
int ParseNonNegativeIntEnv(const char *name, int fallback = 0) {
  if (!name || !*name)
    return fallback;
  const std::string value = HomeDirGuard::ReadEnv(name);
  return ParseUiAutomationNonNegativeIntValue(value, fallback);
}

/** @brief Reads a string value from the named environment variable. */
std::string ReadEnvString(const char *name) {
  if (!name || !*name)
    return {};
  return HomeDirGuard::ReadEnv(name);
}

/** @brief Logs the value of a UI automation environment variable for diagnostics. */
void LogUiEnvVar(const char *name);

/** @brief Returns a human-readable name for an ImGui test status enum value. */
const char *UiTestStatusName(const ImGuiTestStatus status) {
  switch (status) {
  case ImGuiTestStatus_Unknown:
    return "Unknown";
  case ImGuiTestStatus_Success:
    return "Success";
  case ImGuiTestStatus_Queued:
    return "Queued";
  case ImGuiTestStatus_Running:
    return "Running";
  case ImGuiTestStatus_Error:
    return "Error";
  case ImGuiTestStatus_Suspended:
    return "Suspended";
  case ImGuiTestStatus_COUNT:
    break;
  }
  return "Invalid";
}

/** @brief Returns the fully qualified category/name string for a test. */
std::string UiTestFullName(const ImGuiTest &test) {
  return std::format("{}/{}", test.Category ? test.Category : "<unknown>",
                     test.Name ? test.Name : "<unknown>");
}

/** @brief Builds a fixed-width ASCII progress bar string. */
std::string BuildProgressBar(const int completed, const int total) {
  constexpr int kWidth = 20;
  if (total <= 0)
    return std::string(kWidth, '-');
  const int filled = std::clamp((completed * kWidth) / total, 0, kWidth);
  return std::string(static_cast<size_t>(filled), '#') +
         std::string(static_cast<size_t>(kWidth - filled), '-');
}

/** @brief Transparent hash functor used for de-duplicating failed scenario names. */
struct TransparentStringHash {
  using is_transparent = void;

  size_t operator()(std::string_view value) const noexcept {
    return std::hash<std::string_view>{}(value);
  }

  size_t operator()(const std::string &value) const noexcept {
    return (*this)(std::string_view(value));
  }

  size_t operator()(const char *value) const noexcept {
    return (*this)(std::string_view(value ? value : ""));
  }
};

/** @brief Extracts the error-level log lines from a completed test's output buffer. */
std::string ExtractUiTestFailureLog(ImGuiTest &test) {
  ImGuiTextBuffer buffer;
  test.Output.Log.ExtractLinesForVerboseLevels(
      ImGuiTestVerboseLevel_Error, ImGuiTestVerboseLevel_Error, &buffer);
  if (!buffer.empty())
    return buffer.c_str();

  test.Output.Log.ExtractLinesForVerboseLevels(
      ImGuiTestVerboseLevel_Warning, ImGuiTestVerboseLevel_Warning, &buffer);
  return buffer.empty() ? std::string("<no failure log captured>")
                        : std::string(buffer.c_str());
}

/** @brief Aggregated pass/fail progress snapshot derived from current queued tests. */
struct UiAutomationProgressSnapshot {
  int completed = 0;
  int succeeded = 0;
  int failed = 0;
  std::string currentTest;
};

/** @brief Collects a pass/fail progress snapshot from the current test engine state. */
UiAutomationProgressSnapshot
CollectUiAutomationProgress(ImGuiTestEngine *engine, const int totalQueued) {
  UiAutomationProgressSnapshot snapshot{};
  if (!engine)
    return snapshot;

  ImVector<ImGuiTest *> tests;
  ImGuiTestEngine_GetTestList(engine, &tests);
  for (const ImGuiTest *test : tests) {
    if (!test)
      continue;
    const ImGuiTestStatus status = test->Output.Status;
    if (status == ImGuiTestStatus_Running ||
        status == ImGuiTestStatus_Suspended)
      snapshot.currentTest = UiTestFullName(*test);
    if (status == ImGuiTestStatus_Success)
      ++snapshot.succeeded;
    else if (status == ImGuiTestStatus_Error)
      ++snapshot.failed;
  }
  snapshot.completed = snapshot.succeeded + snapshot.failed;
  snapshot.completed = std::min(snapshot.completed, std::max(0, totalQueued));
  return snapshot;
}

/** @brief Thrown when UI automation initialization cannot create required engine state. */
class UiAutomationInitException final : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

/** @brief Constructs a unique temporary directory path for this automation run. */
fs::path BuildUiAutomationTempRoot() {
  const std::string homePath = HomeDirGuard::ReadEnv("HOME");
#ifdef _WIN32
  const std::string userProfilePath = HomeDirGuard::ReadEnv("USERPROFILE");
  const fs::path baseDir = SelectUiAutomationBaseDir(homePath, userProfilePath,
                                                     fs::current_path(), true);
#else
  const fs::path baseDir =
      SelectUiAutomationBaseDir(homePath, {}, fs::current_path(), false);
#endif
  const auto now =
      std::chrono::high_resolution_clock::now().time_since_epoch().count();
#ifdef _WIN32
  const int pid = _getpid();
#else
  const auto pid = static_cast<int>(getpid());
#endif
  return baseDir / std::format(".horo_editor_ui_automation_{}_{}", now, pid);
}

/** @brief Returns the lazily-initialized singleton temp root path for this process. */
const fs::path &UiAutomationTempRoot() {
  static const fs::path tempRoot = BuildUiAutomationTempRoot();
  return tempRoot;
}

/** @brief Logs all relevant UI automation environment variables for diagnostics. */
void LogUiAutomationEnv() {
  static constexpr std::array<const char *, 11> kEnvVars = {
      "HORO_UI_TEST_CAPTURE",    "HORO_UI_TEST_VIDEO",
      "HORO_UI_TEST_RECORDING",  "HORO_UI_TEST_FILTER",
      "HORO_UI_TEST_SUITE",      "HORO_UI_TEST_DELAY_MS",
      "HORO_UI_TEST_OUTPUT_DIR", "HORO_UI_TEST_FFMPEG_PATH",
      "HORO_UI_TEST_CLEAN_TEMP", "HORO_GLFW_SAMPLES",
      "HORO_GLFW_VISIBLE",
  };
  for (const char *name : kEnvVars)
    LogUiEnvVar(name);
}

/** @brief Resolves the capture output directory from environment and defaults. */
fs::path ResolveCaptureOutputDir(bool captureEnabled,
                                 const std::string &outputDirEnv) {
  return ResolveUiCaptureOutputDir(captureEnabled, outputDirEnv,
                                   fs::current_path());
}

/** @brief Creates and secures the temporary and output directories for the automation run. */
void PrepareUiAutomationDirectories(const UiAutomationRunState *state) {
  if (!state)
    return;
  const bool cleanTempRoot = ParseBoolEnvDefaultTrue("HORO_UI_TEST_CLEAN_TEMP");
  std::error_code ec;
  if (state->tempRoot.empty()) {
    LogError("UI temp root path is empty.");
    return;
  }

  if (fs::is_symlink(state->tempRoot, ec) && !ec) {
    LogError("UI temp root points to symlink, refusing to use path: {}",
             state->tempRoot.string());
    return;
  }
  ec.clear();

  if (cleanTempRoot) {
    fs::remove_all(state->tempRoot, ec);
    if (ec) {
      LogWarn("UI temp root cleanup error for '{}': {}",
              state->tempRoot.string(), ec.message());
      ec.clear();
    }
  } else {
    LogDebug("Skipping UI temp root cleanup for this run: {}",
             state->tempRoot.string());
  }

  fs::create_directories(state->tempRoot, ec);
  if (ec) {
    LogError("UI temp root creation error at '{}': {}",
             state->tempRoot.string(), ec.message());
    return;
  }
#ifndef _WIN32
  fs::permissions(state->tempRoot, fs::perms::owner_all,
                  fs::perm_options::replace, ec);
  if (ec) {
    LogWarn("UI temp root permissions update failed at '{}': {}",
            state->tempRoot.string(), ec.message());
    ec.clear();
  }
#endif

  const fs::path homePath = state->tempRoot / "home";
  fs::create_directories(homePath, ec);
  if (ec) {
    LogWarn("UI temp home creation error at '{}': {}", homePath.string(),
            ec.message());
    ec.clear();
  }

  if (state->captureEnabled)
    fs::create_directories(state->uiCaptureOutputDir, ec);
  if (ec) {
    LogWarn("UI output directory creation error at '{}': {}",
            state->uiCaptureOutputDir.string(), ec.message());
  }
}

/** @brief Configures the test engine IO for video capture if ffmpeg is available. */
void ConfigureUiVideoCapture(UiAutomationRunState *state,
                             ImGuiTestEngineIO *testIo) {
  if (!state || !testIo || !state->captureEnabled || !state->videoEnabled)
    return;

  const std::string ffmpegPath = ReadEnvString("HORO_UI_TEST_FFMPEG_PATH");
  const fs::path encoderPath =
      !ffmpegPath.empty() ? fs::path(ffmpegPath) : fs::path("/usr/bin/ffmpeg");
  LogDebug("UI video encoder candidate path: '{}'", encoderPath.string());
  if (std::error_code ffmpegEc;
      !fs::exists(encoderPath, ffmpegEc) || ffmpegEc) {
    state->videoEnabled = false;
    LogWarn("UI test video requested but ffmpeg is missing at: {}",
            encoderPath.string());
    return;
  }

  ImStrncpy(testIo->VideoCaptureEncoderPath, encoderPath.string().c_str(),
            IM_ARRAYSIZE(testIo->VideoCaptureEncoderPath));
  ImStrncpy(testIo->VideoCaptureEncoderParams, kFfmpegVideoParams,
            IM_ARRAYSIZE(testIo->VideoCaptureEncoderParams));
  ImStrncpy(testIo->GifCaptureEncoderParams, kFfmpegGifParams,
            IM_ARRAYSIZE(testIo->GifCaptureEncoderParams));
  ImStrncpy(testIo->VideoCaptureExtension, ".mp4",
            IM_ARRAYSIZE(testIo->VideoCaptureExtension));
}

/** @brief Snapshot used when emitting periodic heartbeat diagnostics for UI automation. */
struct UiHeartbeatSnapshot {
  int frameCount = 0;
  int maxFrames = 0;
  int heartbeatInterval = 0;
  bool heartbeatLogEnabled = false;
  GLFWwindow *nativeWindowHandle = nullptr;
  double elapsedSec = 0.0;
  double frameDeltaSec = 0.0;
  bool running = false;
  bool queueEmpty = false;
};

/** @brief Emits periodic heartbeat and large-delta diagnostics for the automation run. */
void LogUiHeartbeat(const UiHeartbeatSnapshot &snapshot) {
  const int frameCount = snapshot.frameCount;
  if (ShouldLogUiAutomationHeartbeat(snapshot.heartbeatLogEnabled, frameCount,
                                     snapshot.heartbeatInterval)) {
    int focused = -1;
    int iconified = -1;
    int visible = -1;
    int shouldClose = -1;
    if (snapshot.nativeWindowHandle) {
      focused = glfwGetWindowAttrib(snapshot.nativeWindowHandle, GLFW_FOCUSED);
      iconified =
          glfwGetWindowAttrib(snapshot.nativeWindowHandle, GLFW_ICONIFIED);
      visible = glfwGetWindowAttrib(snapshot.nativeWindowHandle, GLFW_VISIBLE);
      shouldClose = glfwWindowShouldClose(snapshot.nativeWindowHandle);
    }
    LogDebug(
        "UI automation heartbeat: frame={}/{} elapsed={:.2f}s frame_dt={:.4f}s "
        "avg_fps={:.2f} running={} queue_empty={} focused={} iconified={} "
        "visible={} should_close={}",
        frameCount, snapshot.maxFrames, snapshot.elapsedSec,
        snapshot.frameDeltaSec,
        snapshot.elapsedSec > 0.0
            ? static_cast<double>(frameCount) / snapshot.elapsedSec
            : 0.0,
        snapshot.running ? 1 : 0, snapshot.queueEmpty ? 1 : 0, focused,
        iconified, visible, shouldClose);
  }
  if (ShouldWarnUiAutomationLargeFrameDelta(snapshot.frameDeltaSec)) {
    LogWarn(
        "UI automation large frame delta detected: frame={} frame_dt={:.3f}s",
        frameCount, snapshot.frameDeltaSec);
  }
}

/** @brief Logs the value of a UI automation environment variable for diagnostics. */
void LogUiEnvVar(const char *name) {
  const std::string value = ReadEnvString(name);
  LogDebug("UI automation env: {}={}", name, value.empty() ? "<unset>" : value);
}

/** @brief ImGui test-engine capture callback that reads back pixels from the active renderer. */
bool UiScreenCaptureFunc(ImGuiID viewport_id, int x, int y, int w, int h,
                         unsigned int *pixels,
                         void *user_data) { // NOSONAR
  IM_UNUSED(viewport_id);
  IM_UNUSED(user_data);
  if (!pixels || w <= 0 || h <= 0)
    return false;
  if (glfwGetCurrentContext() == nullptr)
    return false;

  const auto viewport = Renderer::GetViewport();
  if (viewport[2] <= 0 || viewport[3] <= 0)
    return false;
  if (x < 0 || y < 0 || x + w > viewport[2] || y + h > viewport[3])
    return false;

  if (std::string readError;
      !Renderer::ReadbackRegionRgba8(x, y, w, h, pixels, &readError))
    return false;

  for (int row = 0; row < h / 2; ++row) {
    unsigned int *top =
        pixels + static_cast<size_t>(row) * static_cast<size_t>(w);
    unsigned int *bottom =
        pixels + static_cast<size_t>(h - 1 - row) * static_cast<size_t>(w);
    for (int col = 0; col < w; ++col) {
      const unsigned int tmp = top[col];
      top[col] = bottom[col];
      bottom[col] = tmp;
    }
  }
  return true;
}

} // namespace
#endif

/** @brief Runtime implementation state for a single UI automation execution session. */
struct UiAutomationRunner::Impl {
#ifdef HORO_STANDALONE_UI_AUTOMATION
  static constexpr float kActionDelayShortSec =
      0.05f; // 50 ms between short actions
  static constexpr float kActionDelayStandardSec =
      0.15f; // 150 ms between standard actions
  static constexpr float kMouseSpeedPixPerSec =
      200.0f; // slow enough to follow visually
  static constexpr int kHeartbeatFrameInterval = 30;
  bool active = false;
  bool passed = true;
  bool timedOut = false;
  int frameCount = 0;
  int maxFrames = kUiAutomationDefaultMaxFrames;
  int totalQueued = 0;
  int lastProgressCompleted = -1;
  int testsRun = 0;
  int testsSucceeded = 0;
  bool heartbeatLogEnabled = false;
  bool lastRunningState = false;
  bool lastQueueEmptyState = true;
  double startTimeSeconds = 0.0;
  double lastFrameTimeSeconds = 0.0;
  UiAutomationRunState state{};
  ImGuiTestEngine *engine = nullptr;
  std::unordered_set<std::string, TransparentStringHash, std::equal_to<>>
      loggedFailureTests;

  void LogStateChange(bool running, bool queueEmpty, double elapsedSec) {
    if (running == lastRunningState && queueEmpty == lastQueueEmptyState)
      return;
    LogDebug("UI automation state change: frame={} elapsed={:.2f}s running={} "
             "queue_empty={}",
             frameCount, elapsedSec, running ? 1 : 0, queueEmpty ? 1 : 0);
    lastRunningState = running;
    lastQueueEmptyState = queueEmpty;
  }

  void LogProgress(double elapsedSec) {
    if (const UiAutomationProgressSnapshot progress =
            CollectUiAutomationProgress(engine, totalQueued);
        progress.completed != lastProgressCompleted) {
      const int percent =
          totalQueued > 0 ? (progress.completed * 100) / totalQueued : 0;
      const std::string current = progress.currentTest.empty()
                                      ? std::string("<idle>")
                                      : progress.currentTest;
      LogInfo("UI automation progress: [{}] {}/{} ({}%) passed={} failed={} "
              "current='{}' elapsed={:.2f}s",
              BuildProgressBar(progress.completed, totalQueued),
              progress.completed, totalQueued, percent, progress.succeeded,
              progress.failed, current, elapsedSec);
      lastProgressCompleted = progress.completed;
    }
  }

  void LogNewFailures(double elapsedSec) {
    ImVector<ImGuiTest *> tests;
    ImGuiTestEngine_GetTestList(engine, &tests);
    for (ImGuiTest *test : tests) {
      if (!test || test->Output.Status != ImGuiTestStatus_Error)
        continue;
      const std::string fullName = UiTestFullName(*test);
      if (!loggedFailureTests.insert(fullName).second)
        continue;
      LogError(
          "UI automation scenario failed: name='{}' status={} frame={} "
          "elapsed={:.2f}s; detailed failure log will be printed at shutdown.",
          fullName, UiTestStatusName(test->Output.Status), frameCount,
          elapsedSec);
    }
  }

  void RequestWindowCloseIfFinished(GLFWwindow *nativeWindowHandle, bool done,
                                    bool timeout, double elapsedSec) {
    if (!done && !timeout)
      return;
    if (timeout) {
      timedOut = true;
      passed = false;
      ImGuiTestEngine_TryAbortEngine(engine);
      LogError("UI automation timed out after {} frames (elapsed={:.2f}s).",
               frameCount, elapsedSec);
    }
    if (nativeWindowHandle != nullptr) {
      LogDebug("UI automation requesting window close: done={} timeout={} "
               "frame={} elapsed={:.2f}s",
               done ? 1 : 0, timeout ? 1 : 0, frameCount, elapsedSec);
      glfwSetWindowShouldClose(nativeWindowHandle, GLFW_TRUE);
    } else {
      LogWarn("UI automation finished but native window handle is null; "
              "skipping window close request.");
    }
  }

  void LogIncompleteScenarios() {
    ImVector<ImGuiTest *> tests;
    ImGuiTestEngine_GetTestList(engine, &tests);
    for (ImGuiTest *test : tests) {
      if (!test)
        continue;
      const ImGuiTestStatus status = test->Output.Status;
      if (status == ImGuiTestStatus_Unknown ||
          status == ImGuiTestStatus_Queued || status == ImGuiTestStatus_Success)
        continue;
      const std::string failureLog =
          status == ImGuiTestStatus_Error
              ? std::format("\n{}", ExtractUiTestFailureLog(*test))
              : std::string();
      LogError("UI automation incomplete scenario: name='{}' status={}{}",
               UiTestFullName(*test), UiTestStatusName(status), failureLog);
    }
  }

  bool StopIfIncomplete(const ImGuiTestEngineIO &testIo) {
    const bool stillRunning =
        testIo.IsRunningTests || !ImGuiTestEngine_IsTestQueueEmpty(engine);
    if (!timedOut && !stillRunning)
      return false;
    passed = false;
    LogError("UI automation did not finish cleanly before shutdown: "
             "timed_out={}, running={}, queued={}",
             timedOut ? 1 : 0, testIo.IsRunningTests ? 1 : 0,
             ImGuiTestEngine_IsTestQueueEmpty(engine) ? 0 : 1);
    LogDebug("UI automation stopping engine after timeout/incomplete run.");
    LogIncompleteScenarios();
    ImGuiTestEngine_Stop(engine);
    active = false;
    return true;
  }

  void LogFailedScenarioSummaries() {
    ImVector<ImGuiTest *> tests;
    ImGuiTestEngine_GetTestList(engine, &tests);
    for (ImGuiTest *test : tests) {
      if (!test || test->Output.Status == ImGuiTestStatus_Success ||
          test->Output.Status == ImGuiTestStatus_Unknown ||
          test->Output.Status == ImGuiTestStatus_Queued)
        continue;
      LogError("UI automation failed scenario summary: name='{}' status={}\n{}",
               UiTestFullName(*test), UiTestStatusName(test->Output.Status),
               ExtractUiTestFailureLog(*test));
    }
  }
#endif
};

/** @copydoc UiAutomationRunner::UiAutomationRunner */
UiAutomationRunner::UiAutomationRunner() { m_impl = std::make_unique<Impl>(); }

/** @copydoc UiAutomationRunner::~UiAutomationRunner */
UiAutomationRunner::~UiAutomationRunner() = default;

/** @copydoc UiAutomationRunner::PrepareEnvironmentBeforeAppStart */
void UiAutomationRunner::PrepareEnvironmentBeforeAppStart(
    bool runUiAutomation) {
#ifdef HORO_STANDALONE_UI_AUTOMATION
  if (!runUiAutomation)
    return;

  static std::unique_ptr<const HomeDirGuard> s_homeGuard = nullptr;
  const fs::path tempRoot = UiAutomationTempRoot();
  const fs::path homeRoot = tempRoot / "home";
  LogInfo("UI automation pre-start: cwd='{}', temp_root='{}', home_root='{}'",
          fs::current_path().string(), tempRoot.string(), homeRoot.string());

  std::error_code ec;
  fs::create_directories(homeRoot, ec);
  if (ec) {
    LogWarn("UI automation could not create home root '{}': {}",
            homeRoot.string(), ec.message());
  }
  if (!s_homeGuard)
    s_homeGuard = std::make_unique<HomeDirGuard>(homeRoot);
  LogUiEnvVar("USERPROFILE");
  LogUiEnvVar("HOME");
  LogUiEnvVar("HOMEDRIVE");
  LogUiEnvVar("HOMEPATH");
#else
  (void)runUiAutomation;
#endif
}

/** @copydoc UiAutomationRunner::StartIfRequested */
void UiAutomationRunner::StartIfRequested(
    bool runUiAutomation, Launcher::LauncherEditorShell *shellContext,
    Editor::EditorLayer *editorContext) const {
#ifdef HORO_STANDALONE_UI_AUTOMATION
  if (!runUiAutomation)
    return;

  m_impl->active = true;
  m_impl->state.tempRoot = UiAutomationTempRoot();
  m_impl->state.projectRoot = m_impl->state.tempRoot / "UiSmokeGame";
  LogDebug("UI automation start requested: shell_context={}",
           static_cast<const void *>(shellContext));
  LogUiAutomationEnv();
  const bool recordingEnabled =
      ParseBoolEnvDefaultTrue("HORO_UI_TEST_RECORDING");
  m_impl->state.captureEnabled =
      recordingEnabled && ParseBoolEnv("HORO_UI_TEST_CAPTURE");
  m_impl->state.videoEnabled =
      recordingEnabled && ParseBoolEnv("HORO_UI_TEST_VIDEO");
  const std::string uiFilterEnv = ReadEnvString("HORO_UI_TEST_FILTER");
  const std::string uiSuite = ReadEnvString("HORO_UI_TEST_SUITE");
  const std::string uiFilter(
      ResolveUiAutomationScenarioFilter(uiFilterEnv, uiSuite));
  const std::string outputDirEnv = ReadEnvString("HORO_UI_TEST_OUTPUT_DIR");
  m_impl->state.uiCaptureOutputDir =
      ResolveCaptureOutputDir(m_impl->state.captureEnabled, outputDirEnv);
  m_impl->heartbeatLogEnabled = ParseBoolEnv("HORO_UI_TEST_HEARTBEAT");
  m_impl->state.shellContext = shellContext;
  m_impl->state.editorContext = editorContext;
  LogInfo("UI automation config: suite='{}', filter='{}', recording={}, "
          "capture={}, "
          "video={}",
          uiSuite, uiFilter, recordingEnabled ? 1 : 0,
          m_impl->state.captureEnabled ? 1 : 0,
          m_impl->state.videoEnabled ? 1 : 0);
  LogDebug(
      "UI automation paths: temp_root='{}', project_root='{}', output_dir='{}'",
      m_impl->state.tempRoot.string(), m_impl->state.projectRoot.string(),
      m_impl->state.uiCaptureOutputDir.empty()
          ? "<disabled>"
          : m_impl->state.uiCaptureOutputDir.string());
  PrepareUiAutomationDirectories(&m_impl->state);
  m_impl->maxFrames = kUiAutomationDefaultMaxFrames;
  m_impl->frameCount = 0;
  m_impl->totalQueued = 0;
  m_impl->lastProgressCompleted = -1;
  m_impl->loggedFailureTests.clear();
  m_impl->timedOut = false;
  m_impl->startTimeSeconds = glfwGetTime();
  m_impl->lastFrameTimeSeconds = m_impl->startTimeSeconds;
  LogDebug("UI automation frame budget: max_frames={}, heartbeat_interval={}",
           m_impl->maxFrames, Impl::kHeartbeatFrameInterval);

  m_impl->engine = ImGuiTestEngine_CreateContext();
  if (!m_impl->engine)
    throw UiAutomationInitException(
        "Failed to create ImGui test engine context");

  ImGuiTestEngineIO &testIo = ImGuiTestEngine_GetIO(m_impl->engine);
  testIo.ConfigCaptureEnabled = m_impl->state.captureEnabled;
  testIo.ScreenCaptureFunc =
      m_impl->state.captureEnabled ? &UiScreenCaptureFunc : nullptr;
  ConfigureUiVideoCapture(&m_impl->state, &testIo);
  testIo.ConfigFixedDeltaTime = 1.0f / 60.0f;
  testIo.ConfigRunSpeed = ImGuiTestRunSpeed_Normal;
  testIo.ActionDelayShort = Impl::kActionDelayShortSec;
  testIo.ActionDelayStandard = Impl::kActionDelayStandardSec;
  testIo.MouseSpeed = Impl::kMouseSpeedPixPerSec;
  testIo.ConfigVerboseLevel = ImGuiTestVerboseLevel_Error;
  testIo.ConfigVerboseLevelOnError = ImGuiTestVerboseLevel_Debug;
  LogDebug("UI engine IO config: capture={} run_speed={} fixed_dt={:.4f} "
           "delay_short={:.3f} delay_std={:.3f} verbose={} verbose_on_error={}",
           testIo.ConfigCaptureEnabled ? 1 : 0,
           static_cast<int>(testIo.ConfigRunSpeed), testIo.ConfigFixedDeltaTime,
           testIo.ActionDelayShort, testIo.ActionDelayStandard,
           static_cast<int>(testIo.ConfigVerboseLevel),
           static_cast<int>(testIo.ConfigVerboseLevelOnError));

  ImGuiTestEngine_Start(m_impl->engine, ImGui::GetCurrentContext());
  m_impl->lastRunningState = testIo.IsRunningTests;
  m_impl->lastQueueEmptyState =
      ImGuiTestEngine_IsTestQueueEmpty(m_impl->engine);
  LogDebug("UI automation engine started: is_running={}, queue_empty={}",
           m_impl->lastRunningState ? 1 : 0,
           m_impl->lastQueueEmptyState ? 1 : 0);
  int queuedCount = 0;
  if (!QueueRegisteredUiScenarios(m_impl->engine, &m_impl->state, uiFilter,
                                  &queuedCount)) {
    m_impl->passed = false;
    LogError("No UI scenarios were queued (filter='{}').", uiFilter);
    ImGuiTestEngine_Stop(m_impl->engine);
    return;
  }
  m_impl->totalQueued = queuedCount;
  LogInfo("Queued {} UI scenario(s) with filter '{}'.", queuedCount, uiFilter);
  LogInfo(
      "Running Dear ImGui test suite in Normal mode "
      "(delay_short={:.0f}ms delay_std={:.0f}ms) with full rendering enabled.",
      Impl::kActionDelayShortSec * 1000.0f,
      Impl::kActionDelayStandardSec * 1000.0f);
#else
  (void)runUiAutomation;
  (void)shellContext;
  (void)editorContext;
#endif
}

/** @copydoc UiAutomationRunner::PostRenderFrame */
void UiAutomationRunner::PostRenderFrame(GLFWwindow *nativeWindowHandle) const {
#ifdef HORO_STANDALONE_UI_AUTOMATION
  if (!m_impl->active || m_impl->engine == nullptr)
    return;

  ImGuiTestEngine_PostSwap(m_impl->engine);
  ++m_impl->frameCount;

  const ImGuiTestEngineIO &testIo = ImGuiTestEngine_GetIO(m_impl->engine);
  const bool running = testIo.IsRunningTests;
  const bool queueEmpty = ImGuiTestEngine_IsTestQueueEmpty(m_impl->engine);
  const bool done = !running && queueEmpty;
  const bool timeout = m_impl->frameCount >= m_impl->maxFrames;
  const double nowSec = glfwGetTime();
  const double elapsedSec = nowSec - m_impl->startTimeSeconds;
  const double frameDeltaSec = nowSec - m_impl->lastFrameTimeSeconds;
  m_impl->lastFrameTimeSeconds = nowSec;
  m_impl->LogStateChange(running, queueEmpty, elapsedSec);
  m_impl->LogProgress(elapsedSec);
  m_impl->LogNewFailures(elapsedSec);

  LogUiHeartbeat(UiHeartbeatSnapshot{
      .frameCount = m_impl->frameCount,
      .maxFrames = m_impl->maxFrames,
      .heartbeatInterval = Impl::kHeartbeatFrameInterval,
      .heartbeatLogEnabled = m_impl->heartbeatLogEnabled,
      .nativeWindowHandle = nativeWindowHandle,
      .elapsedSec = elapsedSec,
      .frameDeltaSec = frameDeltaSec,
      .running = running,
      .queueEmpty = queueEmpty,
  });
  m_impl->RequestWindowCloseIfFinished(nativeWindowHandle, done, timeout,
                                       elapsedSec);
#else
  (void)nativeWindowHandle;
#endif
}

/** @copydoc UiAutomationRunner::Shutdown */
void UiAutomationRunner::Shutdown() const {
#ifdef HORO_STANDALONE_UI_AUTOMATION
  if (m_impl->engine == nullptr)
    return;

  const ImGuiTestEngineIO &testIo = ImGuiTestEngine_GetIO(m_impl->engine);
  const double elapsedSec = glfwGetTime() - m_impl->startTimeSeconds;
  LogInfo(
      "UI automation shutdown begin: running={}, queue_empty={}, "
      "capture_enabled={}, video_capture_open={}, frame={}/{}, elapsed={:.2f}s",
      testIo.IsRunningTests ? 1 : 0,
      ImGuiTestEngine_IsTestQueueEmpty(m_impl->engine) ? 1 : 0,
      testIo.ConfigCaptureEnabled ? 1 : 0,
      m_impl->state.videoCaptureOpen ? 1 : 0, m_impl->frameCount,
      m_impl->maxFrames, elapsedSec);
  if (m_impl->state.videoCaptureOpen)
    LogWarn("Video capture still marked open during shutdown; letting test "
            "engine close capture on stop.");
  if (m_impl->StopIfIncomplete(testIo))
    return;

  ImGuiTestEngine_GetResult(m_impl->engine, m_impl->testsRun,
                            m_impl->testsSucceeded);
  m_impl->passed =
      (m_impl->testsRun > 0) && (m_impl->testsRun == m_impl->testsSucceeded);
  LogInfo("UI automation results: tests_run={}, tests_succeeded={}",
          m_impl->testsRun, m_impl->testsSucceeded);
  if (!m_impl->passed)
    m_impl->LogFailedScenarioSummaries();

  LogDebug("UI automation stopping engine after successful completion.");
  ImGuiTestEngine_Stop(m_impl->engine);
  m_impl->active = false;
#endif
}

/** @copydoc UiAutomationRunner::DestroyContext */
void UiAutomationRunner::DestroyContext() const {
#ifdef HORO_STANDALONE_UI_AUTOMATION
  if (m_impl->engine == nullptr)
    return;
  LogDebug("Destroying UI automation engine context.");
  ImGuiTestEngine_DestroyContext(m_impl->engine);
  m_impl->engine = nullptr;
#endif
}

/** @copydoc UiAutomationRunner::DidPass */
bool UiAutomationRunner::DidPass() const {
#ifdef HORO_STANDALONE_UI_AUTOMATION
  return m_impl->passed;
#else
  return true;
#endif
}

/** @copydoc UiAutomationRunner::IsActive */
bool UiAutomationRunner::IsActive() const {
#ifdef HORO_STANDALONE_UI_AUTOMATION
  return m_impl->active;
#else
  return false;
#endif
}
} // namespace Horo
