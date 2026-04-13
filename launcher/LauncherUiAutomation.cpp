#include "launcher/LauncherUiAutomation.h"

#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include "core/Logger.h"
#include "launcher/StandaloneEditorShell.h"

#ifdef MONOLITH_STANDALONE_UI_AUTOMATION
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_test_engine/imgui_te_context.h>
#include <imgui_test_engine/imgui_te_engine.h>
#endif

namespace Monolith::Standalone {

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

struct AutomationState {
  fs::path tempRoot;
  fs::path projectRoot;
  fs::path uiCaptureOutputDir;
  bool captureEnabled = false;
  bool videoEnabled = false;
  bool videoCaptureOpen = false;
  StandaloneEditorShell* shell = nullptr;
};

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

ImGuiWindow* FindWindowContaining(const char* token) {
  if (!token || !*token || GImGui == nullptr)
    return nullptr;

  ImGuiContext& context = *GImGui;
  for (ImGuiWindow* window : context.Windows) {
    if (!window || !window->Name)
      continue;
    if (std::string(window->Name).find(token) != std::string::npos)
      return window;
  }
  return nullptr;
}

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

bool LauncherUiScreenCaptureFunc(ImGuiID viewport_id,
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
  glReadPixels(x, y, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
  if (glGetError() != GL_NO_ERROR)
    return false;

  // OpenGL readback is bottom-up; test engine expects top-left origin.
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

void CaptureScreenshotTo(ImGuiTestContext* ctx, const fs::path& dir, const char* filename) {
  if (!ctx || !ctx->CaptureArgs || filename == nullptr || dir.empty())
    return;
  const std::string full = (dir / filename).string();
  if (full.size() >= IM_ARRAYSIZE(ctx->CaptureArgs->InOutputFile))
    return;
  ImStrncpy(ctx->CaptureArgs->InOutputFile, full.c_str(), IM_ARRAYSIZE(ctx->CaptureArgs->InOutputFile));
  ctx->CaptureScreenshot(0);
}

bool BeginVideoCapture(ImGuiTestContext* ctx, const AutomationState* state, const char* filename) {
  if (!ctx || !state || !state->videoEnabled || filename == nullptr || state->uiCaptureOutputDir.empty())
    return false;
  if (!ctx->CaptureArgs)
    return false;
  const std::string full = (state->uiCaptureOutputDir / filename).string();
  if (full.size() >= IM_ARRAYSIZE(ctx->CaptureArgs->InOutputFile))
    return false;
  ctx->CaptureReset();
  ImStrncpy(ctx->CaptureArgs->InOutputFile, full.c_str(), IM_ARRAYSIZE(ctx->CaptureArgs->InOutputFile));
  return ctx->CaptureBeginVideo();
}

bool BeginSuiteVideoCaptureIfNeeded(ImGuiTestContext* ctx, AutomationState* state) {
  if (!ctx || !state || !state->videoEnabled)
    return false;
  if (state->videoCaptureOpen)
    return true;
  const bool started = BeginVideoCapture(ctx, state, "launcher_ui__full_suite__run.mp4");
  state->videoCaptureOpen = started;
  return started;
}

void EnsureProjectCreatedFromLauncher(ImGuiTestContext* ctx,
                                      AutomationState* state,
                                      bool allowScreenshot = true) {
  IM_CHECK(ctx != nullptr);
  IM_CHECK(state != nullptr);
  IM_CHECK(state->shell != nullptr);

  if (state->shell->HasActiveProject())
    return;

  ctx->SetRef("Horo Launcher");
  IM_CHECK(ctx->ItemExists("Open Existing Project"));
  IM_CHECK(ctx->ItemExists("Create New Project"));

  ImGuiWindow* launcherPanel = FindWindowContaining("LauncherPanel");
  IM_CHECK(launcherPanel != nullptr);
  ctx->SetRef(launcherPanel);
  ctx->ItemInputValue("##new-project-name", "UiSmokeGame");
  ctx->ItemInputValue("##new-project-path", state->projectRoot.string().c_str());
  ctx->ItemClick("Create Project");
  ctx->Yield(3);
  IM_CHECK(state->shell->HasActiveProject());
  if (state->captureEnabled && allowScreenshot)
    CaptureScreenshotTo(ctx, state->uiCaptureOutputDir,
                        "launcher_ui__create_project_from_launcher__expect_project_created.png");
}

ImGuiTest* RegisterLauncherSmokeTest(ImGuiTestEngine* engine, AutomationState* state) {
  ImGuiTest* test = IM_REGISTER_TEST(engine, "launcher_ui", "create_project_from_launcher");
  test->UserData = state;
  test->TestFunc = [](ImGuiTestContext* ctx) {
    auto* testState = static_cast<AutomationState*>(ctx->Test->UserData);
    IM_CHECK(testState != nullptr);
    BeginSuiteVideoCaptureIfNeeded(ctx, testState);
    EnsureProjectCreatedFromLauncher(ctx, testState, !testState->videoEnabled);

    ctx->SetRef("Standalone Project");
    IM_CHECK(ctx->ItemExists("Configure"));
    IM_CHECK(ctx->ItemExists("Build"));
    IM_CHECK(ctx->ItemExists("Run Game"));
  };
  return test;
}

ImGuiTest* RegisterLauncherBackToHomeTest(ImGuiTestEngine* engine, AutomationState* state) {
  ImGuiTest* test = IM_REGISTER_TEST(engine, "launcher_ui", "back_to_home_returns_launcher");
  test->UserData = state;
  test->TestFunc = [](ImGuiTestContext* ctx) {
    auto* testState = static_cast<AutomationState*>(ctx->Test->UserData);
    IM_CHECK(testState != nullptr);
    BeginSuiteVideoCaptureIfNeeded(ctx, testState);
    EnsureProjectCreatedFromLauncher(ctx, testState, !testState->videoEnabled);

    ctx->SetRef("Standalone Project");
    IM_CHECK(ctx->ItemExists("Back To Home"));
    ctx->ItemClick("Back To Home");
    ctx->Yield(2);
    IM_CHECK(!testState->shell->HasActiveProject());
    if (testState->captureEnabled && !testState->videoEnabled)
      CaptureScreenshotTo(
          ctx,
          testState->uiCaptureOutputDir,
          "launcher_ui__back_to_home_returns_launcher__expect_launcher_home_visible.png");

    ctx->SetRef("Horo Launcher");
    IM_CHECK(ctx->ItemExists("Create New Project"));
    ImGuiWindow* recentProjectsList = FindWindowContaining("RecentProjectsList");
    IM_CHECK(recentProjectsList != nullptr);
    ctx->SetRef(recentProjectsList);
    IM_CHECK(ctx->ItemExists("UiSmokeGame"));
    if (testState->captureEnabled && !testState->videoEnabled)
      CaptureScreenshotTo(
          ctx,
          testState->uiCaptureOutputDir,
          "launcher_ui__back_to_home_returns_launcher__expect_recent_project_listed.png");
  };
  return test;
}

ImGuiTest* RegisterLauncherRecentProjectsTest(ImGuiTestEngine* engine, AutomationState* state) {
  ImGuiTest* test = IM_REGISTER_TEST(engine, "launcher_ui", "open_project_from_recent_projects");
  test->UserData = state;
  test->TestFunc = [](ImGuiTestContext* ctx) {
    auto* testState = static_cast<AutomationState*>(ctx->Test->UserData);
    IM_CHECK(testState != nullptr);
    BeginSuiteVideoCaptureIfNeeded(ctx, testState);
    EnsureProjectCreatedFromLauncher(ctx, testState, !testState->videoEnabled);

    ctx->SetRef("Standalone Project");
    IM_CHECK(ctx->ItemExists("Back To Home"));
    ctx->ItemClick("Back To Home");
    ctx->Yield(2);
    IM_CHECK(!testState->shell->HasActiveProject());

    ctx->SetRef("Horo Launcher");
    ImGuiWindow* recentProjectsList = FindWindowContaining("RecentProjectsList");
    IM_CHECK(recentProjectsList != nullptr);
    ctx->SetRef(recentProjectsList);
    IM_CHECK(ctx->ItemExists("UiSmokeGame"));
    ctx->ItemClick("UiSmokeGame");
    ctx->Yield(3);

    IM_CHECK(testState->shell->HasActiveProject());
    ctx->SetRef("Standalone Project");
    IM_CHECK(ctx->ItemExists("Back To Home"));
    if (testState->captureEnabled && !testState->videoEnabled)
      CaptureScreenshotTo(
          ctx,
          testState->uiCaptureOutputDir,
          "launcher_ui__open_project_from_recent_projects__expect_project_reopened.png");
    if (testState->videoCaptureOpen) {
      ctx->CaptureEndVideo();
      ctx->CaptureReset();
      testState->videoCaptureOpen = false;
    }
  };
  return test;
}

}  // namespace
#endif

struct LauncherUiAutomationRunner::Impl {
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
  AutomationState state{};
  std::optional<HomeDirGuard> homeDirGuard;
  ImGuiTestEngine* engine = nullptr;
  ImGuiTest* smokeTest = nullptr;
  ImGuiTest* backToHomeTest = nullptr;
  ImGuiTest* recentProjectsTest = nullptr;
#endif
};

LauncherUiAutomationRunner::LauncherUiAutomationRunner()
    : m_impl(std::make_unique<Impl>()) {}

LauncherUiAutomationRunner::~LauncherUiAutomationRunner() = default;

void LauncherUiAutomationRunner::StartIfRequested(bool runUiAutomation, StandaloneEditorShell* shell) {
#ifdef MONOLITH_STANDALONE_UI_AUTOMATION
  if (!runUiAutomation)
    return;

  m_impl->active = true;
  m_impl->state.tempRoot = fs::temp_directory_path() / "horo_editor_ui_automation";
  m_impl->state.projectRoot = m_impl->state.tempRoot / "UiSmokeGame";
  m_impl->state.captureEnabled = ParseBoolEnv("MONOLITH_UI_TEST_CAPTURE");
  m_impl->state.videoEnabled = ParseBoolEnv("MONOLITH_UI_TEST_VIDEO");
  const int uiDelayMs = ParseNonNegativeIntEnv("MONOLITH_UI_TEST_DELAY_MS", 0);
  const std::string outputDirEnv = ReadEnvString("MONOLITH_UI_TEST_OUTPUT_DIR");
  m_impl->state.uiCaptureOutputDir =
      m_impl->state.captureEnabled
          ? (!outputDirEnv.empty()
                 ? fs::path(outputDirEnv)
                 : (std::filesystem::current_path() / "ui_test_output"))
          : fs::path();
  m_impl->state.shell = shell;
  std::error_code ec;
  fs::remove_all(m_impl->state.tempRoot, ec);
  fs::create_directories(m_impl->state.tempRoot / "home", ec);
  if (m_impl->state.captureEnabled)
    fs::create_directories(m_impl->state.uiCaptureOutputDir, ec);
  m_impl->homeDirGuard.emplace(m_impl->state.tempRoot / "home");
  m_impl->maxFrames = (uiDelayMs > 0 || m_impl->state.videoEnabled) ? Impl::kCapturedUiMaxFrames
                                                                    : Impl::kDefaultUiMaxFrames;

  m_impl->engine = ImGuiTestEngine_CreateContext();
  if (!m_impl->engine)
    throw std::runtime_error("Failed to create ImGui test engine context");

  ImGuiTestEngineIO& testIo = ImGuiTestEngine_GetIO(m_impl->engine);
  testIo.ConfigCaptureEnabled = m_impl->state.captureEnabled;
  testIo.ScreenCaptureFunc = m_impl->state.captureEnabled ? LauncherUiScreenCaptureFunc : nullptr;
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
  m_impl->smokeTest = RegisterLauncherSmokeTest(m_impl->engine, &m_impl->state);
  m_impl->backToHomeTest = RegisterLauncherBackToHomeTest(m_impl->engine, &m_impl->state);
  m_impl->recentProjectsTest = RegisterLauncherRecentProjectsTest(m_impl->engine, &m_impl->state);
  ImGuiTestEngine_QueueTest(m_impl->engine, m_impl->smokeTest);
  ImGuiTestEngine_QueueTest(m_impl->engine, m_impl->backToHomeTest);
  ImGuiTestEngine_QueueTest(m_impl->engine, m_impl->recentProjectsTest);
  LOG_INFO("Running Dear ImGui Test Suite in %s mode (delay=%dms) with full rendering enabled.",
           uiDelayMs > 0 ? "Normal" : "Fast", uiDelayMs);
#else
  (void)runUiAutomation;
  (void)shell;
#endif
}

void LauncherUiAutomationRunner::PostRenderFrame(void* nativeWindowHandle) {
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

void LauncherUiAutomationRunner::Shutdown() {
#ifdef MONOLITH_STANDALONE_UI_AUTOMATION
  if (m_impl->engine == nullptr)
    return;

  ImGuiTestEngineIO& testIo = ImGuiTestEngine_GetIO(m_impl->engine);
  const bool stillRunning = testIo.IsRunningTests || !ImGuiTestEngine_IsTestQueueEmpty(m_impl->engine);
  if (m_impl->timedOut || stillRunning) {
    m_impl->passed = false;
    LOG_ERROR("UI automation did not finish cleanly before shutdown: timed_out=%d, running=%d, queued=%d",
              m_impl->timedOut ? 1 : 0,
              testIo.IsRunningTests ? 1 : 0,
              ImGuiTestEngine_IsTestQueueEmpty(m_impl->engine) ? 0 : 1);
    ImGuiTestEngine_Stop(m_impl->engine);
    return;
  }

  ImGuiTestEngine_GetResult(m_impl->engine, m_impl->testsRun, m_impl->testsSucceeded);
  const int smokeStatus = m_impl->smokeTest ? static_cast<int>(m_impl->smokeTest->Output.Status) : -1;
  const int backToHomeStatus =
      m_impl->backToHomeTest ? static_cast<int>(m_impl->backToHomeTest->Output.Status) : -1;
  const int recentStatus =
      m_impl->recentProjectsTest ? static_cast<int>(m_impl->recentProjectsTest->Output.Status) : -1;
  m_impl->passed =
      (m_impl->testsRun == 3) && (m_impl->testsSucceeded == 3) &&
      (smokeStatus == ImGuiTestStatus_Success) &&
      (backToHomeStatus == ImGuiTestStatus_Success) &&
      (recentStatus == ImGuiTestStatus_Success);

  LOG_INFO("UI automation results: tests_run=%d, tests_succeeded=%d, smoke=%d, back_to_home=%d, recent=%d",
           m_impl->testsRun, m_impl->testsSucceeded, smokeStatus, backToHomeStatus, recentStatus);

  ImGuiTestEngine_Stop(m_impl->engine);
#endif
}

void LauncherUiAutomationRunner::DestroyContext() {
#ifdef MONOLITH_STANDALONE_UI_AUTOMATION
  if (m_impl->engine == nullptr)
    return;
  ImGuiTestEngine_DestroyContext(m_impl->engine);
  m_impl->engine = nullptr;
#endif
}

bool LauncherUiAutomationRunner::DidPass() const {
#ifdef MONOLITH_STANDALONE_UI_AUTOMATION
  return !m_impl->active || m_impl->passed;
#else
  return true;
#endif
}

bool LauncherUiAutomationRunner::IsActive() const {
#ifdef MONOLITH_STANDALONE_UI_AUTOMATION
  return m_impl->active;
#else
  return false;
#endif
}

}  // namespace Monolith::Standalone
