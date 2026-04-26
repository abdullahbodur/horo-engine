#include "launcher/UiAutomationRunner.h"

#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <memory>
#include <string>

// clang-format off
#include <glad/glad.h>
#include <GLFW/glfw3.h>
// clang-format on

#include "core/Logger.h"
#include "launcher/LauncherEditorShell.h"
#include "launcher/UiAutomationConfig.h"
#include "launcher/UiTestHarness.h"

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
#ifdef _WIN32
    char *value = nullptr;
    size_t len = 0;
    if (_dupenv_s(&value, &len, name) != 0 || !value)
      return {};
    std::string result(value);
    std::free(value);
    return result;
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

bool ParseBoolEnv(const char *name) {
  if (!name || !*name)
    return false;
  const std::string value = HomeDirGuard::ReadEnv(name);
  return ParseUiAutomationBoolValue(value, false);
}

bool ParseBoolEnvDefaultTrue(const char *name) {
  if (!name || !*name)
    return true;
  const std::string value = HomeDirGuard::ReadEnv(name);
  return ParseUiAutomationBoolValue(value, true);
}

int ParseNonNegativeIntEnv(const char *name, int fallback = 0) {
  if (!name || !*name)
    return fallback;
  const std::string value = HomeDirGuard::ReadEnv(name);
  return ParseUiAutomationNonNegativeIntValue(value, fallback);
}

std::string ReadEnvString(const char *name) {
  if (!name || !*name)
    return {};
  return HomeDirGuard::ReadEnv(name);
}

void LogUiEnvVar(const char *name);

class UiAutomationInitException final : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

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

const fs::path &UiAutomationTempRoot() {
  static const fs::path tempRoot = BuildUiAutomationTempRoot();
  return tempRoot;
}

void LogUiAutomationEnv() {
  static constexpr std::array<const char *, 10> kEnvVars = {
      "HORO_UI_TEST_CAPTURE",     "HORO_UI_TEST_VIDEO",
      "HORO_UI_TEST_RECORDING",   "HORO_UI_TEST_FILTER",
      "HORO_UI_TEST_DELAY_MS",    "HORO_UI_TEST_OUTPUT_DIR",
      "HORO_UI_TEST_FFMPEG_PATH", "HORO_UI_TEST_CLEAN_TEMP",
      "HORO_GLFW_SAMPLES",        "HORO_GLFW_VISIBLE",
  };
  for (const char *name : kEnvVars)
    LogUiEnvVar(name);
}

fs::path ResolveCaptureOutputDir(bool captureEnabled,
                                 const std::string &outputDirEnv) {
  return ResolveUiCaptureOutputDir(captureEnabled, outputDirEnv,
                                   fs::current_path());
}

void PrepareUiAutomationDirectories(const UiAutomationRunState *state) {
  if (!state)
    return;
  const bool cleanTempRoot =
      ParseBoolEnvDefaultTrue("HORO_UI_TEST_CLEAN_TEMP");
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

struct UiHeartbeatSnapshot {
  int frameCount = 0;
  int maxFrames = 0;
  int heartbeatInterval = 0;
  GLFWwindow *nativeWindowHandle = nullptr;
  double elapsedSec = 0.0;
  double frameDeltaSec = 0.0;
  bool running = false;
  bool queueEmpty = false;
};

void LogUiHeartbeat(const UiHeartbeatSnapshot &snapshot) {
  if (!kUiAutomationHeartbeatLogEnabled)
    return;

  const int frameCount = snapshot.frameCount;
  if (frameCount != 1 && (frameCount % snapshot.heartbeatInterval) != 0)
    return;

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
      snapshot.running ? 1 : 0, snapshot.queueEmpty ? 1 : 0, focused, iconified,
      visible, shouldClose);
  if (snapshot.frameDeltaSec > 1.0) {
    LogWarn(
        "UI automation large frame delta detected: frame={} frame_dt={:.3f}s",
        frameCount, snapshot.frameDeltaSec);
  }
}

void LogUiEnvVar(const char *name) {
  const std::string value = ReadEnvString(name);
  LogDebug("UI automation env: {}={}", name, value.empty() ? "<unset>" : value);
}

bool UiScreenCaptureFunc(ImGuiID viewport_id, int x, int y, int w, int h,
                         unsigned int *pixels,
                         void *user_data) { // NOSONAR
  IM_UNUSED(viewport_id);
  IM_UNUSED(user_data);
  if (!pixels || w <= 0 || h <= 0)
    return false;
  if (glfwGetCurrentContext() == nullptr)
    return false;
  std::array<GLint, 4> viewport{0, 0, 0, 0};
  glGetIntegerv(GL_VIEWPORT, viewport.data());
  if (viewport[2] <= 0 || viewport[3] <= 0)
    return false;
  if (x < 0 || y < 0 || x + w > viewport[2] || y + h > viewport[3])
    return false;
  glReadPixels(x, y, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
  if (glGetError() != GL_NO_ERROR)
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
  int testsRun = 0;
  int testsSucceeded = 0;
  bool lastRunningState = false;
  bool lastQueueEmptyState = true;
  double startTimeSeconds = 0.0;
  double lastFrameTimeSeconds = 0.0;
  UiAutomationRunState state{};
  ImGuiTestEngine *engine = nullptr;
#endif
};

UiAutomationRunner::UiAutomationRunner() { m_impl = std::make_unique<Impl>(); }

UiAutomationRunner::~UiAutomationRunner() = default;

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

void UiAutomationRunner::StartIfRequested(
    bool runUiAutomation, Launcher::LauncherEditorShell *shellContext) const {
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
  const std::string uiFilter = ReadEnvString("HORO_UI_TEST_FILTER");
  const std::string outputDirEnv = ReadEnvString("HORO_UI_TEST_OUTPUT_DIR");
  m_impl->state.uiCaptureOutputDir =
      ResolveCaptureOutputDir(m_impl->state.captureEnabled, outputDirEnv);
  m_impl->state.shellContext = shellContext;
  LogInfo(
      "UI automation config: filter='{}', recording={}, capture={}, video={}",
      uiFilter, recordingEnabled ? 1 : 0, m_impl->state.captureEnabled ? 1 : 0,
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
  LogInfo("Queued {} UI scenario(s) with filter '{}'.", queuedCount, uiFilter);
  LogInfo(
      "Running Dear ImGui Test Suite in Normal mode "
      "(delay_short={:.0f}ms delay_std={:.0f}ms) with full rendering enabled.",
      Impl::kActionDelayShortSec * 1000.0f,
      Impl::kActionDelayStandardSec * 1000.0f);
#else
  (void)runUiAutomation;
  (void)shellContext;
#endif
}

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
  if (running != m_impl->lastRunningState ||
      queueEmpty != m_impl->lastQueueEmptyState) {
    LogDebug("UI automation state change: frame={} elapsed={:.2f}s running={} "
             "queue_empty={}",
             m_impl->frameCount, elapsedSec, running ? 1 : 0,
             queueEmpty ? 1 : 0);
    m_impl->lastRunningState = running;
    m_impl->lastQueueEmptyState = queueEmpty;
  }
  LogUiHeartbeat(UiHeartbeatSnapshot{
      .frameCount = m_impl->frameCount,
      .maxFrames = m_impl->maxFrames,
      .heartbeatInterval = Impl::kHeartbeatFrameInterval,
      .nativeWindowHandle = nativeWindowHandle,
      .elapsedSec = elapsedSec,
      .frameDeltaSec = frameDeltaSec,
      .running = running,
      .queueEmpty = queueEmpty,
  });
  if (done || timeout) {
    if (timeout) {
      m_impl->timedOut = true;
      m_impl->passed = false;
      ImGuiTestEngine_TryAbortEngine(m_impl->engine);
      LogError("UI automation timed out after {} frames (elapsed={:.2f}s).",
               m_impl->frameCount, elapsedSec);
    }
    if (nativeWindowHandle != nullptr) {
      LogDebug("UI automation requesting window close: done={} timeout={} "
               "frame={} elapsed={:.2f}s",
               done ? 1 : 0, timeout ? 1 : 0, m_impl->frameCount, elapsedSec);
      glfwSetWindowShouldClose(nativeWindowHandle, GLFW_TRUE);
    } else {
      LogWarn("UI automation finished but native window handle is null; "
              "skipping window close request.");
    }
  }
#else
  (void)nativeWindowHandle;
#endif
}

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
  if (const bool stillRunning =
          testIo.IsRunningTests ||
          !ImGuiTestEngine_IsTestQueueEmpty(m_impl->engine);
      m_impl->timedOut || stillRunning) {
    m_impl->passed = false;
    LogError("UI automation did not finish cleanly before shutdown: "
             "timed_out={}, running={}, queued={}",
             m_impl->timedOut ? 1 : 0, testIo.IsRunningTests ? 1 : 0,
             ImGuiTestEngine_IsTestQueueEmpty(m_impl->engine) ? 0 : 1);
    LogDebug("UI automation stopping engine after timeout/incomplete run.");
    ImGuiTestEngine_Stop(m_impl->engine);
    m_impl->active = false;
    return;
  }

  ImGuiTestEngine_GetResult(m_impl->engine, m_impl->testsRun,
                            m_impl->testsSucceeded);
  m_impl->passed =
      (m_impl->testsRun > 0) && (m_impl->testsRun == m_impl->testsSucceeded);
  LogInfo("UI automation results: tests_run={}, tests_succeeded={}",
          m_impl->testsRun, m_impl->testsSucceeded);

  LogDebug("UI automation stopping engine after successful completion.");
  ImGuiTestEngine_Stop(m_impl->engine);
  m_impl->active = false;
#endif
}

void UiAutomationRunner::DestroyContext() const {
#ifdef HORO_STANDALONE_UI_AUTOMATION
  if (m_impl->engine == nullptr)
    return;
  LogDebug("Destroying UI automation engine context.");
  ImGuiTestEngine_DestroyContext(m_impl->engine);
  m_impl->engine = nullptr;
#endif
}

bool UiAutomationRunner::DidPass() const {
#ifdef HORO_STANDALONE_UI_AUTOMATION
  return m_impl->passed;
#else
  return true;
#endif
}

bool UiAutomationRunner::IsActive() const {
#ifdef HORO_STANDALONE_UI_AUTOMATION
  return m_impl->active;
#else
  return false;
#endif
}
} // namespace Horo
