#include "launcher/UiAutomationRunner.h"

#include <array>
#include <cstdlib>
#include <chrono>
#include <filesystem>
#include <memory>
#include <random>
#include <string>

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include "core/Logger.h"
#include "launcher/LauncherEditorShell.h"
#include "launcher/UiTestHarness.h"

#ifdef MONOLITH_STANDALONE_UI_AUTOMATION
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_test_engine/imgui_te_context.h>
#include <imgui_test_engine/imgui_te_engine.h>
#endif

namespace Monolith {

#ifdef MONOLITH_STANDALONE_UI_AUTOMATION
namespace {
namespace fs = std::filesystem;
constexpr const char* kFfmpegVideoParams =
    "-hide_banner -loglevel error -r $FPS -f rawvideo -pix_fmt rgba -s "
    "$WIDTHx$HEIGHT -i - -threads 0 -y -preset ultrafast -pix_fmt yuv420p -crf 20 $OUTPUT";
constexpr const char* kFfmpegGifParams =
    "-hide_banner -loglevel error -r $FPS -f rawvideo -pix_fmt rgba -s "
    "$WIDTHx$HEIGHT -i - -threads 0 -y -filter_complex \"split=2 [a] [b]; [a] palettegen [pal]; "
    "[b] [pal] paletteuse\" $OUTPUT";

struct HomeDirGuard {
  std::string previousUserProfile = {};
  std::string previousHomeDrive = {};
  std::string previousHomePath = {};
  std::string previousHome = {};

  HomeDirGuard(const HomeDirGuard&) = delete;
  HomeDirGuard& operator=(const HomeDirGuard&) = delete;
  HomeDirGuard(HomeDirGuard&&) = delete;
  HomeDirGuard& operator=(HomeDirGuard&&) = delete;

#ifdef _WIN32
  static std::string ReadWin32EnvString(const char* name) {
    char* rawValue = nullptr;
    size_t len = 0;
    if (_dupenv_s(&rawValue, &len, name) != 0 || !rawValue)
      return {};
    std::unique_ptr<char, decltype(&std::free)> value(rawValue, &std::free);
    return std::string(value.get());
  }
#endif

  static std::string ReadEnv(const char* name) {
    if (!name || !*name)
      return {};
#ifdef _WIN32
    return ReadWin32EnvString(name);
#else
    const char* value = std::getenv(name);
    return value ? std::string(value) : std::string();
#endif
  }

  explicit HomeDirGuard(const fs::path& nextHome) {
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
    if (previousHome.empty())
      unsetenv("HOME");
    else
      setenv("HOME", previousHome.c_str(), 1);
#endif
  }
};

bool ParseBoolEnv(const char* name) {
  if (!name || !*name)
    return false;
#ifdef _WIN32
  const std::string out = HomeDirGuard::ReadWin32EnvString(name);
  if (out.empty())
    return false;
  return out != "0";
#else
  const char* value = std::getenv(name);
  if (!value || !*value)
    return false;
  return std::string(value) != "0";
#endif
}

bool ParseBoolEnvDefaultTrue(const char* name) {
  if (!name || !*name)
    return true;
#ifdef _WIN32
  const std::string out = HomeDirGuard::ReadWin32EnvString(name);
  if (out.empty())
    return true;
  return out != "0";
#else
  const char* value = std::getenv(name);
  if (!value || !*value)
    return true;
  return std::string(value) != "0";
#endif
}

int ParseNonNegativeIntEnv(const char* name, int fallback = 0) {
  if (!name || !*name)
    return fallback;
#ifdef _WIN32
  const std::string value = HomeDirGuard::ReadWin32EnvString(name);
  if (value.empty())
    return fallback;
  char* end = nullptr;
  const long parsed = std::strtol(value.c_str(), &end, 10);
  if (const bool valid = (end != value.c_str() && *end == '\0'); !valid)
    return fallback;
#else
  const char* value = std::getenv(name);
  if (!value || !*value)
    return fallback;
  char* end = nullptr;
  const long parsed = std::strtol(value, &end, 10);
  if (const bool valid = (end != value && *end == '\0'); !valid)
    return fallback;
#endif
  return parsed > 0 ? static_cast<int>(parsed) : 0;
}

std::string ReadEnvString(const char* name) {
  if (!name || !*name)
    return {};
#ifdef _WIN32
  return HomeDirGuard::ReadWin32EnvString(name);
#else
  const char* value = std::getenv(name);
  return value ? std::string(value) : std::string();
#endif
}

void LogUiEnvVar(const char* name);

class UiAutomationInitException final : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

fs::path BuildUiAutomationTempRoot() {
  std::error_code ec;
  const fs::path tempBase = fs::temp_directory_path(ec);
  if (ec || tempBase.empty())
    return fs::current_path() / ".horo_editor_ui_automation";

  const auto now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
  std::mt19937_64 rng(std::random_device{}());
  const uint64_t nonce = rng();
  return tempBase / ("horo_editor_ui_automation_" + std::to_string(now) + "_" + std::to_string(nonce));
}

const fs::path& UiAutomationTempRoot() {
  static const fs::path tempRoot = BuildUiAutomationTempRoot();
  return tempRoot;
}

void LogUiAutomationEnv() {
  static constexpr std::array<const char*, 10> kEnvVars = {
      "MONOLITH_UI_TEST_CAPTURE",  "MONOLITH_UI_TEST_VIDEO",   "MONOLITH_UI_TEST_RECORDING",
      "MONOLITH_UI_TEST_FILTER",   "MONOLITH_UI_TEST_DELAY_MS","MONOLITH_UI_TEST_OUTPUT_DIR",
      "MONOLITH_UI_TEST_FFMPEG_PATH","MONOLITH_UI_TEST_CLEAN_TEMP","MONOLITH_GLFW_SAMPLES",
      "MONOLITH_GLFW_VISIBLE",
  };
  for (const char* name : kEnvVars)
    LogUiEnvVar(name);
}

fs::path ResolveCaptureOutputDir(bool captureEnabled, const std::string& outputDirEnv) {
  if (!captureEnabled)
    return {};
  if (!outputDirEnv.empty())
    return fs::path(outputDirEnv);
  return fs::current_path() / "ui_test_output";
}

void PrepareUiAutomationDirectories(UiAutomationRunState* state) {
  if (!state)
    return;
  const bool cleanTempRoot = ParseBoolEnvDefaultTrue("MONOLITH_UI_TEST_CLEAN_TEMP");
  std::error_code ec;
  if (state->tempRoot.empty()) {
    LOG_ERROR("UI temp root path is empty.");
    return;
  }

  if (fs::is_symlink(state->tempRoot, ec) && !ec) {
    LOG_ERROR("UI temp root points to symlink, refusing to use path: %s",
              state->tempRoot.string().c_str());
    return;
  }
  ec.clear();

  if (cleanTempRoot) {
    fs::remove_all(state->tempRoot, ec);
    if (ec) {
      LOG_WARN("UI temp root cleanup error for '%s': %s",
               state->tempRoot.string().c_str(),
               ec.message().c_str());
      ec.clear();
    }
  } else {
    LOG_DEBUG("Skipping UI temp root cleanup for this run: %s", state->tempRoot.string().c_str());
  }

  fs::create_directories(state->tempRoot, ec);
  if (ec) {
    LOG_ERROR("UI temp root creation error at '%s': %s",
              state->tempRoot.string().c_str(),
              ec.message().c_str());
    return;
  }
#ifndef _WIN32
  fs::permissions(state->tempRoot, fs::perms::owner_all, fs::perm_options::replace, ec);
  if (ec) {
    LOG_WARN("UI temp root permissions update failed at '%s': %s",
             state->tempRoot.string().c_str(),
             ec.message().c_str());
    ec.clear();
  }
#endif

  const fs::path homePath = state->tempRoot / "home";
  fs::create_directories(homePath, ec);
  if (ec) {
    LOG_WARN("UI temp home creation error at '%s': %s", homePath.string().c_str(), ec.message().c_str());
    ec.clear();
  }

  if (state->captureEnabled)
    fs::create_directories(state->uiCaptureOutputDir, ec);
  if (ec) {
    LOG_WARN("UI output directory creation error at '%s': %s",
             state->uiCaptureOutputDir.string().c_str(),
             ec.message().c_str());
  }
}

void ConfigureUiVideoCapture(UiAutomationRunState* state, ImGuiTestEngineIO* testIo) {
  if (!state || !testIo || !state->captureEnabled || !state->videoEnabled)
    return;

  const std::string ffmpegPath = ReadEnvString("MONOLITH_UI_TEST_FFMPEG_PATH");
  const fs::path encoderPath = !ffmpegPath.empty() ? fs::path(ffmpegPath) : fs::path("/usr/bin/ffmpeg");
  LOG_DEBUG("UI video encoder candidate path: '%s'", encoderPath.string().c_str());
  if (std::error_code ffmpegEc; !fs::exists(encoderPath, ffmpegEc) || ffmpegEc) {
    state->videoEnabled = false;
    LOG_WARN("UI test video requested but ffmpeg is missing at: %s", encoderPath.string().c_str());
    return;
  }

  ImStrncpy(testIo->VideoCaptureEncoderPath, encoderPath.string().c_str(),
            IM_ARRAYSIZE(testIo->VideoCaptureEncoderPath));
  ImStrncpy(testIo->VideoCaptureEncoderParams, kFfmpegVideoParams,
            IM_ARRAYSIZE(testIo->VideoCaptureEncoderParams));
  ImStrncpy(testIo->GifCaptureEncoderParams, kFfmpegGifParams,
            IM_ARRAYSIZE(testIo->GifCaptureEncoderParams));
  ImStrncpy(testIo->VideoCaptureExtension, ".mp4", IM_ARRAYSIZE(testIo->VideoCaptureExtension));
}

struct UiHeartbeatSnapshot {
  int frameCount = 0;
  int maxFrames = 0;
  int heartbeatInterval = 0;
  GLFWwindow* nativeWindowHandle = nullptr;
  double elapsedSec = 0.0;
  double frameDeltaSec = 0.0;
  bool running = false;
  bool queueEmpty = false;
};

void LogUiHeartbeat(const UiHeartbeatSnapshot& snapshot) {
  const int frameCount = snapshot.frameCount;
  if (frameCount != 1 && (frameCount % snapshot.heartbeatInterval) != 0)
    return;

  int focused = -1;
  int iconified = -1;
  int visible = -1;
  int shouldClose = -1;
  if (snapshot.nativeWindowHandle) {
    focused = glfwGetWindowAttrib(snapshot.nativeWindowHandle, GLFW_FOCUSED);
    iconified = glfwGetWindowAttrib(snapshot.nativeWindowHandle, GLFW_ICONIFIED);
    visible = glfwGetWindowAttrib(snapshot.nativeWindowHandle, GLFW_VISIBLE);
    shouldClose = glfwWindowShouldClose(snapshot.nativeWindowHandle);
  }
  LOG_DEBUG(
      "UI automation heartbeat: frame=%d/%d elapsed=%.2fs frame_dt=%.4fs avg_fps=%.2f running=%d queue_empty=%d focused=%d iconified=%d visible=%d should_close=%d",
      frameCount,
      snapshot.maxFrames,
      snapshot.elapsedSec,
      snapshot.frameDeltaSec,
      snapshot.elapsedSec > 0.0 ? static_cast<double>(frameCount) / snapshot.elapsedSec : 0.0,
      snapshot.running ? 1 : 0,
      snapshot.queueEmpty ? 1 : 0,
      focused,
      iconified,
      visible,
      shouldClose);
  if (snapshot.frameDeltaSec > 1.0) {
    LOG_WARN("UI automation large frame delta detected: frame=%d frame_dt=%.3fs",
             frameCount, snapshot.frameDeltaSec);
  }
}

void LogUiEnvVar(const char* name) {
  const std::string value = ReadEnvString(name);
  LOG_DEBUG("UI automation env: %s=%s", name, value.empty() ? "<unset>" : value.c_str());
}

bool UiScreenCaptureFunc(ImGuiID viewport_id,
                         int x,
                         int y,
                         int w,
                         int h,
                         unsigned int* pixels,
                         void* user_data) {
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
    unsigned int* top = pixels + static_cast<size_t>(row) * static_cast<size_t>(w);
    unsigned int* bottom = pixels + static_cast<size_t>(h - 1 - row) * static_cast<size_t>(w);
    for (int col = 0; col < w; ++col) {
      const unsigned int tmp = top[col];
      top[col] = bottom[col];
      bottom[col] = tmp;
    }
  }
  return true;
}

}  // namespace
#endif

struct UiAutomationRunner::Impl {
#ifdef MONOLITH_STANDALONE_UI_AUTOMATION
  static constexpr int kDefaultUiMaxFrames = 1200;
  static constexpr int kCapturedUiMaxFrames = 3600;
  static constexpr int kHeartbeatFrameInterval = 30;
  bool active = false;
  bool passed = true;
  bool timedOut = false;
  int frameCount = 0;
  int maxFrames = kDefaultUiMaxFrames;
  int testsRun = 0;
  int testsSucceeded = 0;
  bool lastRunningState = false;
  bool lastQueueEmptyState = true;
  double startTimeSeconds = 0.0;
  double lastFrameTimeSeconds = 0.0;
  UiAutomationRunState state{};
  ImGuiTestEngine* engine = nullptr;
#endif
};

UiAutomationRunner::UiAutomationRunner() {
  m_impl = std::make_unique<Impl>();
}

UiAutomationRunner::~UiAutomationRunner() = default;

void UiAutomationRunner::PrepareEnvironmentBeforeAppStart(bool runUiAutomation) {
#ifdef MONOLITH_STANDALONE_UI_AUTOMATION
  if (!runUiAutomation)
    return;

  static std::unique_ptr<const HomeDirGuard> s_homeGuard = nullptr;
  const fs::path tempRoot = UiAutomationTempRoot();
  const fs::path homeRoot = tempRoot / "home";
  LOG_INFO("UI automation pre-start: cwd='%s', temp_root='%s', home_root='%s'",
           fs::current_path().string().c_str(),
           tempRoot.string().c_str(),
           homeRoot.string().c_str());

  std::error_code ec;
  fs::create_directories(homeRoot, ec);
  if (ec) {
    LOG_WARN("UI automation could not create home root '%s': %s",
             homeRoot.string().c_str(),
             ec.message().c_str());
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

void UiAutomationRunner::StartIfRequested(bool runUiAutomation, Launcher::LauncherEditorShell* shellContext) {
#ifdef MONOLITH_STANDALONE_UI_AUTOMATION
  if (!runUiAutomation)
    return;

  m_impl->active = true;
  m_impl->state.tempRoot = UiAutomationTempRoot();
  m_impl->state.projectRoot = m_impl->state.tempRoot / "UiSmokeGame";
  LOG_DEBUG("UI automation start requested: shell_context=%p", shellContext);
  LogUiAutomationEnv();
  const bool recordingEnabled = ParseBoolEnvDefaultTrue("MONOLITH_UI_TEST_RECORDING");
  m_impl->state.captureEnabled = recordingEnabled && ParseBoolEnv("MONOLITH_UI_TEST_CAPTURE");
  m_impl->state.videoEnabled = recordingEnabled && ParseBoolEnv("MONOLITH_UI_TEST_VIDEO");
  const std::string uiFilter = ReadEnvString("MONOLITH_UI_TEST_FILTER");
  const int uiDelayMs = ParseNonNegativeIntEnv("MONOLITH_UI_TEST_DELAY_MS", 0);
  const std::string outputDirEnv = ReadEnvString("MONOLITH_UI_TEST_OUTPUT_DIR");
  m_impl->state.uiCaptureOutputDir = ResolveCaptureOutputDir(m_impl->state.captureEnabled, outputDirEnv);
  m_impl->state.shellContext = shellContext;
  LOG_INFO("UI automation config: filter='%s', recording=%d, capture=%d, video=%d",
           uiFilter.c_str(),
           recordingEnabled ? 1 : 0,
           m_impl->state.captureEnabled ? 1 : 0,
           m_impl->state.videoEnabled ? 1 : 0);
  LOG_DEBUG("UI automation paths: temp_root='%s', project_root='%s', output_dir='%s'",
           m_impl->state.tempRoot.string().c_str(),
           m_impl->state.projectRoot.string().c_str(),
           m_impl->state.uiCaptureOutputDir.empty() ? "<disabled>"
                                                    : m_impl->state.uiCaptureOutputDir.string().c_str());
  PrepareUiAutomationDirectories(&m_impl->state);
  m_impl->maxFrames = (uiDelayMs > 0 || m_impl->state.videoEnabled) ? Impl::kCapturedUiMaxFrames
                                                                    : Impl::kDefaultUiMaxFrames;
  m_impl->frameCount = 0;
  m_impl->timedOut = false;
  m_impl->startTimeSeconds = glfwGetTime();
  m_impl->lastFrameTimeSeconds = m_impl->startTimeSeconds;
  LOG_DEBUG("UI automation frame budget: max_frames=%d, heartbeat_interval=%d", m_impl->maxFrames,
           Impl::kHeartbeatFrameInterval);

  m_impl->engine = ImGuiTestEngine_CreateContext();
  if (!m_impl->engine)
    throw UiAutomationInitException("Failed to create ImGui test engine context");

  ImGuiTestEngineIO& testIo = ImGuiTestEngine_GetIO(m_impl->engine);
  testIo.ConfigCaptureEnabled = m_impl->state.captureEnabled;
  testIo.ScreenCaptureFunc = m_impl->state.captureEnabled ? &UiScreenCaptureFunc : nullptr;
  ConfigureUiVideoCapture(&m_impl->state, &testIo);
  testIo.ConfigFixedDeltaTime = 1.0f / 60.0f;
  testIo.ConfigRunSpeed = uiDelayMs > 0 ? ImGuiTestRunSpeed_Normal : ImGuiTestRunSpeed_Fast;
  if (uiDelayMs > 0) {
    const float delaySec = static_cast<float>(uiDelayMs) / 1000.0f;
    testIo.ActionDelayShort = delaySec;
    testIo.ActionDelayStandard = delaySec;
  }
  testIo.ConfigVerboseLevel = ImGuiTestVerboseLevel_Error;
  testIo.ConfigVerboseLevelOnError = ImGuiTestVerboseLevel_Debug;
  LOG_DEBUG("UI engine IO config: capture=%d run_speed=%d fixed_dt=%.4f delay_short=%.3f delay_std=%.3f verbose=%d verbose_on_error=%d",
           testIo.ConfigCaptureEnabled ? 1 : 0,
           static_cast<int>(testIo.ConfigRunSpeed),
           testIo.ConfigFixedDeltaTime,
           testIo.ActionDelayShort,
           testIo.ActionDelayStandard,
           static_cast<int>(testIo.ConfigVerboseLevel),
           static_cast<int>(testIo.ConfigVerboseLevelOnError));

  ImGuiTestEngine_Start(m_impl->engine, ImGui::GetCurrentContext());
  m_impl->lastRunningState = testIo.IsRunningTests;
  m_impl->lastQueueEmptyState = ImGuiTestEngine_IsTestQueueEmpty(m_impl->engine);
  LOG_DEBUG("UI automation engine started: is_running=%d, queue_empty=%d",
           m_impl->lastRunningState ? 1 : 0,
           m_impl->lastQueueEmptyState ? 1 : 0);
  int queuedCount = 0;
  if (!QueueRegisteredUiScenarios(m_impl->engine, &m_impl->state, uiFilter, &queuedCount)) {
    m_impl->passed = false;
    LOG_ERROR("No UI scenarios were queued (filter='%s').", uiFilter.c_str());
    ImGuiTestEngine_Stop(m_impl->engine);
    return;
  }
  LOG_INFO("Queued %d UI scenario(s) with filter '%s'.", queuedCount, uiFilter.c_str());
  LOG_INFO("Running Dear ImGui Test Suite in %s mode (delay=%dms) with full rendering enabled.",
           uiDelayMs > 0 ? "Normal" : "Fast", uiDelayMs);
#else
  (void)runUiAutomation;
  (void)shellContext;
#endif
}

void UiAutomationRunner::PostRenderFrame(GLFWwindow* nativeWindowHandle) {
#ifdef MONOLITH_STANDALONE_UI_AUTOMATION
  if (!m_impl->active || m_impl->engine == nullptr)
    return;

  ImGuiTestEngine_PostSwap(m_impl->engine);
  ++m_impl->frameCount;

  const ImGuiTestEngineIO& testIo = ImGuiTestEngine_GetIO(m_impl->engine);
  const bool running = testIo.IsRunningTests;
  const bool queueEmpty = ImGuiTestEngine_IsTestQueueEmpty(m_impl->engine);
  const bool done = !running && queueEmpty;
  const bool timeout = m_impl->frameCount >= m_impl->maxFrames;
  const double nowSec = glfwGetTime();
  const double elapsedSec = nowSec - m_impl->startTimeSeconds;
  const double frameDeltaSec = nowSec - m_impl->lastFrameTimeSeconds;
  m_impl->lastFrameTimeSeconds = nowSec;
  if (running != m_impl->lastRunningState || queueEmpty != m_impl->lastQueueEmptyState) {
    LOG_DEBUG("UI automation state change: frame=%d elapsed=%.2fs running=%d queue_empty=%d",
             m_impl->frameCount, elapsedSec, running ? 1 : 0, queueEmpty ? 1 : 0);
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
      LOG_ERROR("UI automation timed out after %d frames (elapsed=%.2fs).", m_impl->frameCount, elapsedSec);
    }
    if (nativeWindowHandle != nullptr) {
      LOG_DEBUG("UI automation requesting window close: done=%d timeout=%d frame=%d elapsed=%.2fs",
               done ? 1 : 0,
               timeout ? 1 : 0,
               m_impl->frameCount,
               elapsedSec);
      glfwSetWindowShouldClose(nativeWindowHandle, GLFW_TRUE);
    } else {
      LOG_WARN("UI automation finished but native window handle is null; skipping window close request.");
    }
  }
#else
  (void)nativeWindowHandle;
#endif
}

void UiAutomationRunner::Shutdown() {
#ifdef MONOLITH_STANDALONE_UI_AUTOMATION
  if (m_impl->engine == nullptr)
    return;

  const ImGuiTestEngineIO& testIo = ImGuiTestEngine_GetIO(m_impl->engine);
  const double elapsedSec = glfwGetTime() - m_impl->startTimeSeconds;
  LOG_INFO("UI automation shutdown begin: running=%d, queue_empty=%d, capture_enabled=%d, video_capture_open=%d, frame=%d/%d, elapsed=%.2fs",
           testIo.IsRunningTests ? 1 : 0,
           ImGuiTestEngine_IsTestQueueEmpty(m_impl->engine) ? 1 : 0,
           testIo.ConfigCaptureEnabled ? 1 : 0,
           m_impl->state.videoCaptureOpen ? 1 : 0,
           m_impl->frameCount,
           m_impl->maxFrames,
           elapsedSec);
  if (m_impl->state.videoCaptureOpen)
    LOG_WARN("Video capture still marked open during shutdown; letting test engine close capture on stop.");
  if (const bool stillRunning = testIo.IsRunningTests || !ImGuiTestEngine_IsTestQueueEmpty(m_impl->engine);
      m_impl->timedOut || stillRunning) {
    m_impl->passed = false;
    LOG_ERROR("UI automation did not finish cleanly before shutdown: timed_out=%d, running=%d, queued=%d",
              m_impl->timedOut ? 1 : 0,
              testIo.IsRunningTests ? 1 : 0,
              ImGuiTestEngine_IsTestQueueEmpty(m_impl->engine) ? 0 : 1);
    LOG_DEBUG("UI automation stopping engine after timeout/incomplete run.");
    ImGuiTestEngine_Stop(m_impl->engine);
    m_impl->active = false;
    return;
  }

  ImGuiTestEngine_GetResult(m_impl->engine, m_impl->testsRun, m_impl->testsSucceeded);
  m_impl->passed = (m_impl->testsRun > 0) && (m_impl->testsRun == m_impl->testsSucceeded);
  LOG_INFO("UI automation results: tests_run=%d, tests_succeeded=%d",
           m_impl->testsRun, m_impl->testsSucceeded);

  LOG_DEBUG("UI automation stopping engine after successful completion.");
  ImGuiTestEngine_Stop(m_impl->engine);
  m_impl->active = false;
#endif
}

void UiAutomationRunner::DestroyContext() {
#ifdef MONOLITH_STANDALONE_UI_AUTOMATION
  if (m_impl->engine == nullptr)
    return;
  LOG_DEBUG("Destroying UI automation engine context.");
  ImGuiTestEngine_DestroyContext(m_impl->engine);
  m_impl->engine = nullptr;
#endif
}

bool UiAutomationRunner::DidPass() const {
#ifdef MONOLITH_STANDALONE_UI_AUTOMATION
  return m_impl->passed;
#else
  return true;
#endif
}

bool UiAutomationRunner::IsActive() const {
#ifdef MONOLITH_STANDALONE_UI_AUTOMATION
  return m_impl->active;
#else
  return false;
#endif
}

}  // namespace Monolith
