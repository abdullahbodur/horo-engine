#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

#include <GLFW/glfw3.h>

#include "core/Application.h"
#include "core/EngineLaunchArgs.h"
#include "core/Logger.h"
#include "core/ProjectPath.h"
#include "editor/EditorLayer.h"
#include "launcher/LauncherEditorShell.h"
#include "launcher/UiAutomationConfig.h"
#include "launcher/UiAutomationRunner.h"
#include "renderer/DebugDraw.h"
#include "renderer/RenderViewUtils.h"
#include "renderer/Renderer.h"
#if defined(HORO_RENDERER_NULL)
#include "renderer/null/NullRenderBackend.h"
#endif
#include "scene/Scene.h"
#include "scene/SceneReferenceRuntime.h"
#include "scene/systems/BehaviorSystem.h"
#include "scene/systems/PhysicsSystem.h"
#include "scene/systems/RenderSystem.h"

#if defined(__GNUC__) || defined(__clang__)
// Forward-declare the gcov flush symbol so we can call it before std::_Exit.
extern "C" void __gcov_dump();
#endif

namespace {
using namespace Horo;

#ifdef HORO_STANDALONE_UI_AUTOMATION
bool HasArg(int argc, char **argv, const char *expected) {
  if (!expected || !*expected)
    return false;
  for (int i = 1; i < argc; ++i) {
    if (argv[i] && std::string(argv[i]) == expected)
      return true;
  }
  return false;
}

std::string ReadEnvString(const char *name) {
  if (!name || !*name)
    return {};
  const char *value = std::getenv(name);
  return value ? std::string(value) : std::string();
}

bool IsRenderHeartbeatEnabled() {
  const std::string value = ReadEnvString("HORO_RENDER_HEARTBEAT");
  return ParseUiAutomationBoolValue(value, false);
}
#endif

class RendererBackendInitException final : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

class HoroEditorApp final : public Application {
public:
  explicit HoroEditorApp(const EngineLaunchOptions &launchOptions
#ifdef HORO_STANDALONE_UI_AUTOMATION
                         ,
                         const bool runUiAutomation
#endif
                         )
      : Application(BuildSpec()), m_launchOptions(launchOptions),
        m_runtime(std::make_unique<SceneReferenceRuntime>(&m_scene))
#ifdef HORO_STANDALONE_UI_AUTOMATION
        ,
        m_runUiAutomation(runUiAutomation)
#endif
  {
    m_scene.AddSystem(std::make_unique<BehaviorSystem>());
    m_scene.AddSystem(std::make_unique<PhysicsSystem>(m_scene.GetPhysics()));
    m_scene.AddRenderSystem(
        std::make_unique<RenderSystem>(m_camera, m_renderAlpha));
  }

private:
  static AppSpec BuildSpec() {
    AppSpec spec;
    spec.name = "Horo Editor";
    spec.width = 1440;
    spec.height = 920;
    spec.vsync = true;
    spec.iconFile = "assets/launcher/logo.png";
#if defined(HORO_RENDERER_NULL)
    // Null renderer: no OpenGL context needed; GLFW_NO_API keeps GLFW
    // from creating one.
    spec.graphicsApi = WindowGraphicsApi::Vulkan;
#else
    spec.graphicsApi = WindowGraphicsApi::OpenGL;
#endif
    return spec;
  }

  void OnInit() override {
    LogInfo("HoroEditorApp::OnInit begin");
    const std::filesystem::path exeDir = std::filesystem::current_path();
    const std::array<std::filesystem::path, 3> sdkCandidates = {
        exeDir.parent_path() / "sdk",
        exeDir / "sdk",
        exeDir.parent_path().parent_path() / "sdk",
    };
    for (const std::filesystem::path &candidate : sdkCandidates) {
      std::error_code ec;
      const std::filesystem::path normalized =
          std::filesystem::weakly_canonical(candidate, ec);
      if (!ec && std::filesystem::is_directory(normalized)) {
        ProjectPath::SetSdkRoot(normalized);
        break;
      }
    }

#if defined(HORO_RENDERER_NULL)
    Renderer::UseBackend(&m_nullBackend);
#else
    if (const RenderBackendInitResult backendInit = Renderer::InitializeBackend(
            {.requested = RenderBackendId::OpenGL,
             .nativeWindowHandle = GetWindow().GetNativeHandle()});
        !backendInit.ok) {
      throw RendererBackendInitException(
          "Failed to initialize renderer backend: " + backendInit.error);
    }
#endif

    DebugDraw::Init();

    m_editor.Init(GetWindow().GetNativeHandle());
    m_editor.SetLiveRegistry(&m_scene.GetRegistry());
    m_shell.Attach(&m_editor, &m_scene, m_runtime.get(), &m_camera);
    m_shell.Initialize();

    GetWindow().SetFileDropCallback([this](int pathCount,
                                           const char **utf8Paths) {
      if (!m_editor.IsActive() || !utf8Paths)
        return;
      double dropX = 0.0;
      double dropY = 0.0;
      glfwGetCursorPos(GetWindow().GetNativeHandle(), &dropX, &dropY);
      m_editor.OnPathsDropped(pathCount, utf8Paths, static_cast<float>(dropX),
                              static_cast<float>(dropY));
    });

    if (!m_launchOptions.projectPath.empty()) {
      std::string openError;
      if (!m_shell.OpenProject(m_launchOptions.projectPath, &openError))
        m_shell.SetLauncherError(openError);
    }

#ifdef HORO_STANDALONE_UI_AUTOMATION
    if (m_runUiAutomation) {
      // CI runners may heavily throttle vsynced, unfocused windows and make
      // frame-based UI tests appear stalled. Disable vsync for automation.
      GetWindow().SetVSync(false);
      LogInfo("UI automation mode: vsync disabled for test run.");
    }
    if (m_runUiAutomation) {
      m_uiAutomation->StartIfRequested(m_runUiAutomation, &m_shell, &m_editor);
    }
#endif
    LogInfo("HoroEditorApp::OnInit end");
  }

  void OnUpdate(float dt) override {
    m_shell.Update();
    m_editor.OnUpdate(dt, m_camera, GetWindow().GetWidth(),
                      GetWindow().GetHeight());
  }

  void OnFixedUpdate(float dt) override {
    if (!m_editor.IsActive() || m_editor.IsPlayMode())
      m_scene.UpdateSystems(dt);
  }

  void OnRender(float alpha) override {
    ++m_renderFrameCount;
    m_renderAlpha = alpha;
    RenderFrameConfig frameConfig;
    frameConfig.debugLabel = "horo-editor-frame";
    if (m_shell.HasActiveProject())
      frameConfig.lights = m_runtime->GetLights();

    Renderer::BeginFrame(frameConfig);
    if (m_shell.HasActiveProject()) {
      Renderer::BeginPass({RenderPassId::OpaqueScene, BuildRenderView(m_camera),
                           "horo-editor-scene"});
      m_scene.RenderSystems(alpha);
      Renderer::EndPass();
    }
    m_editor.Render(m_camera, GetWindow().GetWidth(), GetWindow().GetHeight());

#ifdef HORO_STANDALONE_UI_AUTOMATION
    if (m_runUiAutomation && m_uiAutomation)
      m_uiAutomation->PostRenderFrame(GetWindow().GetNativeHandle());
    if (m_runUiAutomation &&
        ShouldLogEditorRenderHeartbeat(m_renderHeartbeatEnabled,
                                       m_renderFrameCount)) {
      LogInfo("HoroEditorApp render heartbeat: frame={} width={} height={} "
              "active_project={}",
              m_renderFrameCount, GetWindow().GetWidth(),
              GetWindow().GetHeight(), m_shell.HasActiveProject() ? 1 : 0);
    }
#endif
    Renderer::EndFrame();
  }

  void OnShutdown() override {
    LogInfo("HoroEditorApp::OnShutdown begin");
#ifdef HORO_STANDALONE_UI_AUTOMATION
    if (m_uiAutomation) {
      // Keep a valid GL context current while Dear ImGui test engine finalizes.
      glfwMakeContextCurrent(GetWindow().GetNativeHandle());
      m_uiAutomation->Shutdown();
      m_uiAutomationPassed = m_uiAutomation->DidPass();
      LogInfo("UI automation pass state at shutdown: {}",
              m_uiAutomationPassed ? 1 : 0);
    }
#endif
    m_shell.Shutdown();
    m_editor.Shutdown();

#ifdef HORO_STANDALONE_UI_AUTOMATION
    if (m_uiAutomation) {
      // ImGui test engine expects ImGui context to be destroyed first.
      m_uiAutomation->DestroyContext();
      m_uiAutomation.reset();
    }
#endif

    if (m_runtime)
      m_runtime->Unload();
    LogInfo("HoroEditorApp::OnShutdown end");
  }

public:
#ifdef HORO_STANDALONE_UI_AUTOMATION
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
  Launcher::LauncherEditorShell m_shell;
  Camera m_camera;
  float m_renderAlpha = 0.0f;
  int m_renderFrameCount = 0;
#if defined(HORO_RENDERER_NULL)
  NullRenderBackend m_nullBackend;
#endif

#ifdef HORO_STANDALONE_UI_AUTOMATION
  bool m_runUiAutomation = false;
  bool m_uiAutomationPassed = true;
  bool m_renderHeartbeatEnabled = IsRenderHeartbeatEnabled();
  std::unique_ptr<UiAutomationRunner> m_uiAutomation =
      std::make_unique<UiAutomationRunner>();
#endif
};
} // namespace

int main(int argc, char **argv) {
  const Horo::EngineLaunchOptions launchOptions =
      Horo::ParseEngineLaunchOptions(argc, argv);
#ifdef HORO_STANDALONE_UI_AUTOMATION
  const bool runUiAutomation = HasArg(argc, argv, "--run-ui-tests");
  Horo::UiAutomationRunner::PrepareEnvironmentBeforeAppStart(runUiAutomation);
  HoroEditorApp app(launchOptions, runUiAutomation);
#else
  HoroEditorApp app(launchOptions);
#endif
  app.ParseArgs(argc, argv);
  app.Run();
  const int exitCode = app.DidUiAutomationPass() ? 0 : 1;
#ifdef HORO_STANDALONE_UI_AUTOMATION
  if (runUiAutomation) {
    // CI UI automation mode: avoid late process-teardown crashes from external
    // teardown chains.
    std::fflush(nullptr);
#if defined(__GNUC__) || defined(__clang__)
#if HORO_ENGINE_COVERAGE
    // Flush gcov/llvm-profdata coverage data before bypassing atexit.
    __gcov_dump();
#endif
#endif
    std::_Exit(exitCode);
  }
#endif
  return exitCode;
}
