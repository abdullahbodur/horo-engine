#include <array>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

#include <GLFW/glfw3.h>
#include <glad/glad.h>

#include "core/Application.h"
#include "core/EngineLaunchArgs.h"
#include "core/ProjectPath.h"
#include "editor/EditorLayer.h"
#include "renderer/DebugDraw.h"
#include "renderer/RenderViewUtils.h"
#include "renderer/Renderer.h"
#include "scene/Scene.h"
#include "scene/SceneReferenceRuntime.h"
#include "scene/systems/BehaviorSystem.h"
#include "scene/systems/PhysicsSystem.h"
#include "scene/systems/RenderSystem.h"
#include "launcher/StandaloneEditorShell.h"

#ifdef MONOLITH_STANDALONE_UI_AUTOMATION
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_test_engine/imgui_te_context.h>
#include <imgui_test_engine/imgui_te_engine.h>
#endif

namespace {

using namespace Monolith;

#ifdef MONOLITH_STANDALONE_UI_AUTOMATION
namespace fs = std::filesystem;

struct StandaloneUiAutomationState {
  fs::path tempRoot;
  fs::path projectRoot;
  Standalone::StandaloneEditorShell* shell = nullptr;
};

struct HomeDirGuard {
  std::string previousUserProfile;
  std::string previousHomeDrive;
  std::string previousHomePath;

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
        previousHomePath(ReadEnv("HOMEPATH")) {
#ifdef _WIN32
    _putenv_s("USERPROFILE", nextHome.string().c_str());
    _putenv_s("HOMEDRIVE", "");
    _putenv_s("HOMEPATH", "");
#endif
  }

  ~HomeDirGuard() {
#ifdef _WIN32
    _putenv_s("USERPROFILE", previousUserProfile.c_str());
    _putenv_s("HOMEDRIVE", previousHomeDrive.c_str());
    _putenv_s("HOMEPATH", previousHomePath.c_str());
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

void EnsureProjectCreatedFromLauncher(ImGuiTestContext* ctx, StandaloneUiAutomationState* state) {
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
}

ImGuiTest* RegisterStandaloneLauncherSmokeTest(ImGuiTestEngine* engine, StandaloneUiAutomationState* state) {
  ImGuiTest* test = IM_REGISTER_TEST(engine, "launcher_ui", "create_project_from_launcher");
  test->UserData = state;
  test->TestFunc = [](ImGuiTestContext* ctx) {
    auto* testState = static_cast<StandaloneUiAutomationState*>(ctx->Test->UserData);
    IM_CHECK(testState != nullptr);
    EnsureProjectCreatedFromLauncher(ctx, testState);

    ctx->SetRef("Standalone Project");
    IM_CHECK(ctx->ItemExists("Configure"));
    IM_CHECK(ctx->ItemExists("Build"));
    IM_CHECK(ctx->ItemExists("Run Game"));
  };
  return test;
}

ImGuiTest* RegisterStandaloneBackToHomeTest(ImGuiTestEngine* engine, StandaloneUiAutomationState* state) {
  ImGuiTest* test = IM_REGISTER_TEST(engine, "launcher_ui", "back_to_home_returns_launcher");
  test->UserData = state;
  test->TestFunc = [](ImGuiTestContext* ctx) {
    auto* testState = static_cast<StandaloneUiAutomationState*>(ctx->Test->UserData);
    IM_CHECK(testState != nullptr);
    EnsureProjectCreatedFromLauncher(ctx, testState);

    ctx->SetRef("Standalone Project");
    IM_CHECK(ctx->ItemExists("Back To Home"));
    ctx->ItemClick("Back To Home");
    ctx->Yield(2);
    IM_CHECK(!testState->shell->HasActiveProject());

    ctx->SetRef("Horo Launcher");
    IM_CHECK(ctx->ItemExists("Create New Project"));
    ImGuiWindow* recentProjectsList = FindWindowContaining("RecentProjectsList");
    IM_CHECK(recentProjectsList != nullptr);
    ctx->SetRef(recentProjectsList);
    IM_CHECK(ctx->ItemExists("UiSmokeGame"));
  };
  return test;
}

ImGuiTest* RegisterStandaloneRecentProjectsTest(ImGuiTestEngine* engine, StandaloneUiAutomationState* state) {
  ImGuiTest* test = IM_REGISTER_TEST(engine, "launcher_ui", "open_project_from_recent_projects");
  test->UserData = state;
  test->TestFunc = [](ImGuiTestContext* ctx) {
    auto* testState = static_cast<StandaloneUiAutomationState*>(ctx->Test->UserData);
    IM_CHECK(testState != nullptr);
    EnsureProjectCreatedFromLauncher(ctx, testState);

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
  };
  return test;
}

bool HasArg(int argc, char** argv, const char* expected) {
  if (!expected || !*expected)
    return false;
  for (int i = 1; i < argc; ++i) {
    if (argv[i] && std::string(argv[i]) == expected)
      return true;
  }
  return false;
}
#endif

class HoroEditorApp final : public Application {
 public:
  explicit HoroEditorApp(const EngineLaunchOptions& launchOptions
#ifdef MONOLITH_STANDALONE_UI_AUTOMATION
                         ,
                         const bool runUiAutomation
#endif
                         )
      : Application(BuildSpec()),
        m_launchOptions(launchOptions),
        m_runtime(std::make_unique<SceneReferenceRuntime>(&m_scene))
#ifdef MONOLITH_STANDALONE_UI_AUTOMATION
        ,
        m_runUiAutomation(runUiAutomation)
#endif
  {
    m_scene.AddSystem(std::make_unique<BehaviorSystem>());
    m_scene.AddSystem(std::make_unique<PhysicsSystem>(m_scene.physics));
    m_scene.AddRenderSystem(std::make_unique<RenderSystem>(m_camera, m_renderAlpha));
  }

 private:
  static AppSpec BuildSpec() {
    AppSpec spec;
    spec.name = "Horo Editor";
    spec.width = 1440;
    spec.height = 920;
    spec.vsync = true;
    spec.graphicsApi = WindowGraphicsApi::OpenGL;
    return spec;
  }

  void OnInit() override {
    const std::filesystem::path exeDir = std::filesystem::current_path();
    const std::array<std::filesystem::path, 3> sdkCandidates = {
        exeDir.parent_path() / "sdk",
        exeDir / "sdk",
        exeDir.parent_path().parent_path() / "sdk",
    };
    for (const std::filesystem::path& candidate : sdkCandidates) {
      std::error_code ec;
      const std::filesystem::path normalized = std::filesystem::weakly_canonical(candidate, ec);
      if (!ec && std::filesystem::is_directory(normalized)) {
        ProjectPath::SetSdkRoot(normalized);
        break;
      }
    }

    const RenderBackendInitResult backendInit =
        Renderer::InitializeBackend({.requested = RenderBackendId::OpenGL,
                                     .nativeWindowHandle = GetWindow().GetNativeHandle()});
    if (!backendInit.ok)
      throw std::runtime_error("Failed to initialize renderer backend: " + backendInit.error);

    DebugDraw::Init();

    m_editor.Init(GetWindow().GetNativeHandle());
    m_editor.SetLiveRegistry(&m_scene.registry);
    m_shell.Attach(&m_editor, &m_scene, m_runtime.get(), &m_camera);
    m_shell.Initialize();

    GetWindow().SetFileDropCallback([this](int pathCount, const char** utf8Paths) {
      if (!m_editor.IsActive() || !utf8Paths)
        return;
      double dropX = 0.0;
      double dropY = 0.0;
      glfwGetCursorPos(GetWindow().GetNativeHandle(), &dropX, &dropY);
      m_editor.OnPathsDropped(pathCount, utf8Paths, static_cast<float>(dropX), static_cast<float>(dropY));
    });

    if (!m_launchOptions.projectPath.empty()) {
      std::string openError;
      if (!m_shell.OpenProject(m_launchOptions.projectPath, &openError))
        m_shell.SetLauncherError(openError);
    }

#ifdef MONOLITH_STANDALONE_UI_AUTOMATION
    if (m_runUiAutomation) {
      m_uiAutomation.tempRoot = fs::temp_directory_path() / "horo_editor_ui_automation";
      m_uiAutomation.projectRoot = m_uiAutomation.tempRoot / "UiSmokeGame";
      m_uiAutomation.shell = &m_shell;
      std::error_code ec;
      fs::remove_all(m_uiAutomation.tempRoot, ec);
      fs::create_directories(m_uiAutomation.tempRoot / "home", ec);
      m_homeDirGuard.emplace(m_uiAutomation.tempRoot / "home");

      m_uiTestEngine = ImGuiTestEngine_CreateContext();
      if (!m_uiTestEngine)
        throw std::runtime_error("Failed to create ImGui test engine context");

      ImGuiTestEngineIO& testIo = ImGuiTestEngine_GetIO(m_uiTestEngine);
      testIo.ConfigCaptureEnabled = false;
      testIo.ConfigFixedDeltaTime = 1.0f / 60.0f;
      testIo.ConfigRunSpeed = ImGuiTestRunSpeed_Fast;
      testIo.ConfigVerboseLevel = ImGuiTestVerboseLevel_Error;
      testIo.ConfigVerboseLevelOnError = ImGuiTestVerboseLevel_Debug;

      ImGuiTestEngine_Start(m_uiTestEngine, ImGui::GetCurrentContext());
      m_uiSmokeTest = RegisterStandaloneLauncherSmokeTest(m_uiTestEngine, &m_uiAutomation);
      m_uiBackToHomeTest = RegisterStandaloneBackToHomeTest(m_uiTestEngine, &m_uiAutomation);
      m_uiRecentProjectsTest = RegisterStandaloneRecentProjectsTest(m_uiTestEngine, &m_uiAutomation);
      ImGuiTestEngine_QueueTest(m_uiTestEngine, m_uiSmokeTest);
      ImGuiTestEngine_QueueTest(m_uiTestEngine, m_uiBackToHomeTest);
      ImGuiTestEngine_QueueTest(m_uiTestEngine, m_uiRecentProjectsTest);
      LOG_INFO("Running Dear ImGui Test Suite in Fast mode with full rendering enabled.");
    }
#endif
  }

  void OnUpdate(float dt) override {
    m_shell.Update();
    m_editor.OnUpdate(dt, m_camera, GetWindow().GetWidth(), GetWindow().GetHeight());
  }

  void OnFixedUpdate(float dt) override {
    if (!m_editor.IsActive() || m_editor.IsPlayMode())
      m_scene.UpdateSystems(dt);
  }

  void OnRender(float alpha) override {
    m_renderAlpha = alpha;
    RenderFrameConfig frameConfig;
    frameConfig.debugLabel = "horo-editor-frame";
    if (m_shell.HasActiveProject())
      frameConfig.lights = m_runtime->GetLights();

    Renderer::BeginFrame(frameConfig);
    if (m_shell.HasActiveProject()) {
      Renderer::BeginPass(
          {RenderPassId::OpaqueScene, BuildRenderView(m_camera), "horo-editor-scene"});
      m_scene.RenderSystems(alpha);
      Renderer::EndPass();
    }
    m_editor.Render(m_camera, GetWindow().GetWidth(), GetWindow().GetHeight());

#ifdef MONOLITH_STANDALONE_UI_AUTOMATION
    if (m_runUiAutomation && m_uiTestEngine) {
      ImGuiTestEngine_PostSwap(m_uiTestEngine);
      ++m_uiFrameCount;

      ImGuiTestEngineIO& testIo = ImGuiTestEngine_GetIO(m_uiTestEngine);
      const bool done = !testIo.IsRunningTests && ImGuiTestEngine_IsTestQueueEmpty(m_uiTestEngine);
      const bool timeout = m_uiFrameCount >= kUiMaxFrames;
      if (done || timeout) {
        if (timeout) {
          ImGuiTestEngine_TryAbortEngine(m_uiTestEngine);
          LOG_ERROR("UI automation timed out after %d frames.", m_uiFrameCount);
        }
        glfwSetWindowShouldClose(GetWindow().GetNativeHandle(), GLFW_TRUE);
      }
    }
#endif
    Renderer::EndFrame();
  }

  void OnShutdown() override {
#ifdef MONOLITH_STANDALONE_UI_AUTOMATION
    if (m_uiTestEngine) {
      ImGuiTestEngine_GetResult(m_uiTestEngine, m_uiTestsRun, m_uiTestsSucceeded);
      const int smokeStatus = m_uiSmokeTest ? static_cast<int>(m_uiSmokeTest->Output.Status) : -1;
      const int backToHomeStatus = m_uiBackToHomeTest ? static_cast<int>(m_uiBackToHomeTest->Output.Status) : -1;
      const int recentStatus = m_uiRecentProjectsTest ? static_cast<int>(m_uiRecentProjectsTest->Output.Status) : -1;
      m_uiAutomationPassed =
          (m_uiTestsRun == 3) && (m_uiTestsSucceeded == 3) &&
          (smokeStatus == ImGuiTestStatus_Success) &&
          (backToHomeStatus == ImGuiTestStatus_Success) &&
          (recentStatus == ImGuiTestStatus_Success);

      LOG_INFO("UI automation results: tests_run=%d, tests_succeeded=%d, smoke=%d, back_to_home=%d, recent=%d",
               m_uiTestsRun, m_uiTestsSucceeded, smokeStatus, backToHomeStatus, recentStatus);

      ImGuiTestEngine_Stop(m_uiTestEngine);
    }
#endif
    m_shell.Shutdown();
    m_editor.Shutdown();
#ifdef MONOLITH_STANDALONE_UI_AUTOMATION
    if (m_uiTestEngine) {
      ImGuiTestEngine_DestroyContext(m_uiTestEngine);
      m_uiTestEngine = nullptr;
    }
#endif
    if (m_runtime)
      m_runtime->Unload();
  }

 public:
#ifdef MONOLITH_STANDALONE_UI_AUTOMATION
  bool DidUiAutomationPass() const {
    return !m_runUiAutomation || m_uiAutomationPassed;
  }
#else
  bool DidUiAutomationPass() const { return true; }
#endif

  EngineLaunchOptions m_launchOptions;
  Editor::EditorLayer m_editor;
  Scene m_scene;
  std::unique_ptr<SceneReferenceRuntime> m_runtime;
  Standalone::StandaloneEditorShell m_shell;
  Camera m_camera;
  float m_renderAlpha = 0.0f;

#ifdef MONOLITH_STANDALONE_UI_AUTOMATION
  static constexpr int kUiMaxFrames = 1200;
  bool m_runUiAutomation = false;
  bool m_uiAutomationPassed = true;
  int m_uiFrameCount = 0;
  int m_uiTestsRun = 0;
  int m_uiTestsSucceeded = 0;
  StandaloneUiAutomationState m_uiAutomation{};
  std::optional<HomeDirGuard> m_homeDirGuard;
  ImGuiTestEngine* m_uiTestEngine = nullptr;
  ImGuiTest* m_uiSmokeTest = nullptr;
  ImGuiTest* m_uiBackToHomeTest = nullptr;
  ImGuiTest* m_uiRecentProjectsTest = nullptr;
#endif
};

}  // namespace

int main(int argc, char** argv) {
  const Monolith::EngineLaunchOptions launchOptions = Monolith::ParseEngineLaunchOptions(argc, argv);
#ifdef MONOLITH_STANDALONE_UI_AUTOMATION
  const bool runUiAutomation = HasArg(argc, argv, "--run-ui-tests");
  HoroEditorApp app(launchOptions, runUiAutomation);
#else
  HoroEditorApp app(launchOptions);
#endif
  app.ParseArgs(argc, argv);
  app.Run();
  return app.DidUiAutomationPass() ? 0 : 1;
}
