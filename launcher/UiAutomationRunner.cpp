#include "launcher/UiAutomationRunner.h"

#include <cstdlib>
#include <filesystem>
#include <optional>
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
  std::string previousUserProfile;
  std::string previousHomeDrive;
  std::string previousHomePath;
  std::string previousHome;

  static std::string ReadEnv(const char* name) {
    if (!name || !*name)
      return {};
#ifdef _WIN32
    char* value = nullptr;
    size_t len = 0;
    if (_dupenv_s(&value, &len, name) != 0 || !value)
      return {};
    std::string out(value);
    free(value);
    return out;
#else
    const char* value = std::getenv(name);
    return value ? std::string(value) : std::string();
#endif
  }

  explicit HomeDirGuard(const fs::path& nextHome)
      : previousUserProfile(ReadEnv("USERPROFILE")),
        previousHomeDrive(ReadEnv("HOMEDRIVE")),
        previousHomePath(ReadEnv("HOMEPATH")),
        previousHome(ReadEnv("HOME")) {
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
  char* value = nullptr;
  size_t len = 0;
  if (_dupenv_s(&value, &len, name) != 0 || !value)
    return false;
  const std::string out(value);
  free(value);
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
  char* value = nullptr;
  size_t len = 0;
  if (_dupenv_s(&value, &len, name) != 0 || !value)
    return true;
  const std::string out(value);
  free(value);
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
  char* value = nullptr;
  size_t len = 0;
  if (_dupenv_s(&value, &len, name) != 0 || !value)
    return fallback;
  char* end = nullptr;
  const long parsed = std::strtol(value, &end, 10);
  const bool valid = (end != value && *end == '\0');
  free(value);
#else
  const char* value = std::getenv(name);
  if (!value || !*value)
    return fallback;
  char* end = nullptr;
  const long parsed = std::strtol(value, &end, 10);
  const bool valid = (end != value && *end == '\0');
#endif
  if (!valid)
    return fallback;
  return parsed > 0 ? static_cast<int>(parsed) : 0;
}

std::string ReadEnvString(const char* name) {
  if (!name || !*name)
    return {};
#ifdef _WIN32
  char* value = nullptr;
  size_t len = 0;
  if (_dupenv_s(&value, &len, name) != 0 || !value)
    return {};
  const std::string out(value);
  free(value);
  return out;
#else
  const char* value = std::getenv(name);
  return value ? std::string(value) : std::string();
#endif
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
  GLint viewport[4] = {0, 0, 0, 0};
  glGetIntegerv(GL_VIEWPORT, viewport);
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
  bool active = false;
  bool passed = true;
  bool timedOut = false;
  int frameCount = 0;
  int maxFrames = kDefaultUiMaxFrames;
  int testsRun = 0;
  int testsSucceeded = 0;
  UiAutomationRunState state{};
  ImGuiTestEngine* engine = nullptr;
#endif
};

UiAutomationRunner::UiAutomationRunner()
    : m_impl(std::make_unique<Impl>()) {}

UiAutomationRunner::~UiAutomationRunner() = default;

void UiAutomationRunner::PrepareEnvironmentBeforeAppStart(bool runUiAutomation) {
#ifdef MONOLITH_STANDALONE_UI_AUTOMATION
  if (!runUiAutomation)
    return;

  static std::optional<HomeDirGuard> s_homeGuard;
  const fs::path tempRoot = fs::temp_directory_path() / "horo_editor_ui_automation";
  const fs::path homeRoot = tempRoot / "home";

  std::error_code ec;
  fs::create_directories(homeRoot, ec);
  s_homeGuard.emplace(homeRoot);
#else
  (void)runUiAutomation;
#endif
}

void UiAutomationRunner::StartIfRequested(bool runUiAutomation, void* shellContext) {
#ifdef MONOLITH_STANDALONE_UI_AUTOMATION
  if (!runUiAutomation)
    return;

  m_impl->active = true;
  m_impl->state.tempRoot = fs::temp_directory_path() / "horo_editor_ui_automation";
  m_impl->state.projectRoot = m_impl->state.tempRoot / "UiSmokeGame";
  const bool recordingEnabled = ParseBoolEnvDefaultTrue("MONOLITH_UI_TEST_RECORDING");
  m_impl->state.captureEnabled = recordingEnabled && ParseBoolEnv("MONOLITH_UI_TEST_CAPTURE");
  m_impl->state.videoEnabled = recordingEnabled && ParseBoolEnv("MONOLITH_UI_TEST_VIDEO");
  const std::string uiFilter = ReadEnvString("MONOLITH_UI_TEST_FILTER");
  const int uiDelayMs = ParseNonNegativeIntEnv("MONOLITH_UI_TEST_DELAY_MS", 0);
  const std::string outputDirEnv = ReadEnvString("MONOLITH_UI_TEST_OUTPUT_DIR");
  m_impl->state.uiCaptureOutputDir =
      m_impl->state.captureEnabled
          ? (!outputDirEnv.empty()
                 ? fs::path(outputDirEnv)
                 : (std::filesystem::current_path() / "ui_test_output"))
          : fs::path();
  m_impl->state.shellContext = shellContext;
  LOG_INFO("UI automation config: filter='%s', recording=%d, capture=%d, video=%d",
           uiFilter.c_str(),
           recordingEnabled ? 1 : 0,
           m_impl->state.captureEnabled ? 1 : 0,
           m_impl->state.videoEnabled ? 1 : 0);
  std::error_code ec;
  fs::remove_all(m_impl->state.tempRoot, ec);
  fs::create_directories(m_impl->state.tempRoot / "home", ec);
  if (m_impl->state.captureEnabled)
    fs::create_directories(m_impl->state.uiCaptureOutputDir, ec);
  m_impl->maxFrames = (uiDelayMs > 0 || m_impl->state.videoEnabled) ? Impl::kCapturedUiMaxFrames
                                                                    : Impl::kDefaultUiMaxFrames;

  m_impl->engine = ImGuiTestEngine_CreateContext();
  if (!m_impl->engine)
    throw std::runtime_error("Failed to create ImGui test engine context");

  ImGuiTestEngineIO& testIo = ImGuiTestEngine_GetIO(m_impl->engine);
  testIo.ConfigCaptureEnabled = m_impl->state.captureEnabled;
  testIo.ScreenCaptureFunc = m_impl->state.captureEnabled ? UiScreenCaptureFunc : nullptr;
  if (m_impl->state.captureEnabled && m_impl->state.videoEnabled) {
    const std::string ffmpegPath = ReadEnvString("MONOLITH_UI_TEST_FFMPEG_PATH");
    const fs::path encoderPath = !ffmpegPath.empty() ? fs::path(ffmpegPath) : fs::path("/usr/bin/ffmpeg");
    std::error_code ffmpegEc;
    if (fs::exists(encoderPath, ffmpegEc) && !ffmpegEc) {
      ImStrncpy(testIo.VideoCaptureEncoderPath, encoderPath.string().c_str(),
                IM_ARRAYSIZE(testIo.VideoCaptureEncoderPath));
      ImStrncpy(testIo.VideoCaptureEncoderParams, kFfmpegVideoParams,
                IM_ARRAYSIZE(testIo.VideoCaptureEncoderParams));
      ImStrncpy(testIo.GifCaptureEncoderParams, kFfmpegGifParams,
                IM_ARRAYSIZE(testIo.GifCaptureEncoderParams));
      ImStrncpy(testIo.VideoCaptureExtension, ".mp4", IM_ARRAYSIZE(testIo.VideoCaptureExtension));
    } else {
      m_impl->state.videoEnabled = false;
      LOG_WARN("UI test video requested but ffmpeg is missing at: %s", encoderPath.string().c_str());
    }
  }
  testIo.ConfigFixedDeltaTime = 1.0f / 60.0f;
  testIo.ConfigRunSpeed = uiDelayMs > 0 ? ImGuiTestRunSpeed_Normal : ImGuiTestRunSpeed_Fast;
  if (uiDelayMs > 0) {
    const float delaySec = static_cast<float>(uiDelayMs) / 1000.0f;
    testIo.ActionDelayShort = delaySec;
    testIo.ActionDelayStandard = delaySec;
  }
  testIo.ConfigVerboseLevel = ImGuiTestVerboseLevel_Error;
  testIo.ConfigVerboseLevelOnError = ImGuiTestVerboseLevel_Debug;

  ImGuiTestEngine_Start(m_impl->engine, ImGui::GetCurrentContext());
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

void UiAutomationRunner::PostRenderFrame(void* nativeWindowHandle) {
#ifdef MONOLITH_STANDALONE_UI_AUTOMATION
  if (!m_impl->active || m_impl->engine == nullptr)
    return;

  ImGuiTestEngine_PostSwap(m_impl->engine);
  ++m_impl->frameCount;

  ImGuiTestEngineIO& testIo = ImGuiTestEngine_GetIO(m_impl->engine);
  const bool done = !testIo.IsRunningTests && ImGuiTestEngine_IsTestQueueEmpty(m_impl->engine);
  const bool timeout = m_impl->frameCount >= m_impl->maxFrames;
  if (done || timeout) {
    if (timeout) {
      m_impl->timedOut = true;
      m_impl->passed = false;
      ImGuiTestEngine_TryAbortEngine(m_impl->engine);
      LOG_ERROR("UI automation timed out after %d frames.", m_impl->frameCount);
    }
    if (nativeWindowHandle != nullptr) {
      glfwSetWindowShouldClose(static_cast<GLFWwindow*>(nativeWindowHandle), GLFW_TRUE);
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

  ImGuiTestEngineIO& testIo = ImGuiTestEngine_GetIO(m_impl->engine);
  LOG_INFO("UI automation shutdown begin: running=%d, queue_empty=%d, capture_enabled=%d, video_capture_open=%d",
           testIo.IsRunningTests ? 1 : 0,
           ImGuiTestEngine_IsTestQueueEmpty(m_impl->engine) ? 1 : 0,
           testIo.ConfigCaptureEnabled ? 1 : 0,
           m_impl->state.videoCaptureOpen ? 1 : 0);
  if (m_impl->state.videoCaptureOpen)
    LOG_WARN("Video capture still marked open during shutdown; letting test engine close capture on stop.");
  const bool stillRunning = testIo.IsRunningTests || !ImGuiTestEngine_IsTestQueueEmpty(m_impl->engine);
  if (m_impl->timedOut || stillRunning) {
    m_impl->passed = false;
    LOG_ERROR("UI automation did not finish cleanly before shutdown: timed_out=%d, running=%d, queued=%d",
              m_impl->timedOut ? 1 : 0,
              testIo.IsRunningTests ? 1 : 0,
              ImGuiTestEngine_IsTestQueueEmpty(m_impl->engine) ? 0 : 1);
    LOG_INFO("UI automation stopping engine after timeout/incomplete run.");
    ImGuiTestEngine_Stop(m_impl->engine);
    m_impl->active = false;
    return;
  }

  ImGuiTestEngine_GetResult(m_impl->engine, m_impl->testsRun, m_impl->testsSucceeded);
  m_impl->passed = (m_impl->testsRun > 0) && (m_impl->testsRun == m_impl->testsSucceeded);
  LOG_INFO("UI automation results: tests_run=%d, tests_succeeded=%d",
           m_impl->testsRun, m_impl->testsSucceeded);

  LOG_INFO("UI automation stopping engine after successful completion.");
  ImGuiTestEngine_Stop(m_impl->engine);
  m_impl->active = false;
#endif
}

void UiAutomationRunner::DestroyContext() {
#ifdef MONOLITH_STANDALONE_UI_AUTOMATION
  if (m_impl->engine == nullptr)
    return;
  LOG_INFO("Destroying UI automation engine context.");
  ImGuiTestEngine_DestroyContext(m_impl->engine);
  m_impl->engine = nullptr;
#endif
}

bool UiAutomationRunner::DidPass() const {
#ifdef MONOLITH_STANDALONE_UI_AUTOMATION
  return !m_impl->active || m_impl->passed;
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
