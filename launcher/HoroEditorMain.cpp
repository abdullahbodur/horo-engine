#include <array>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>

#include <GLFW/glfw3.h>

#include "core/Application.h"
#include "core/EngineLaunchArgs.h"
#include "core/ProjectPath.h"
#include "editor/EditorLayer.h"
#include "launcher/LauncherUiAutomation.h"
#include "launcher/StandaloneEditorShell.h"
#include "renderer/DebugDraw.h"
#include "renderer/RenderViewUtils.h"
#include "renderer/Renderer.h"
#include "scene/Scene.h"
#include "scene/SceneReferenceRuntime.h"
#include "scene/systems/BehaviorSystem.h"
#include "scene/systems/PhysicsSystem.h"
#include "scene/systems/RenderSystem.h"

namespace {
using namespace Monolith;

#ifdef MONOLITH_STANDALONE_UI_AUTOMATION
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
        m_runUiAutomation(runUiAutomation),
        m_uiAutomation(std::make_unique<Standalone::LauncherUiAutomationRunner>())
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
      m_uiAutomation->StartIfRequested(m_runUiAutomation, &m_shell);
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
    if (m_runUiAutomation && m_uiAutomation)
      m_uiAutomation->PostRenderFrame(GetWindow().GetNativeHandle());
#endif
    Renderer::EndFrame();
  }

  void OnShutdown() override {
#ifdef MONOLITH_STANDALONE_UI_AUTOMATION
    if (m_uiAutomation) {
      m_uiAutomation->Shutdown();
      m_uiAutomationPassed = m_uiAutomation->DidPass();
    }
#endif
    m_shell.Shutdown();
    m_editor.Shutdown();
    if (m_runtime)
      m_runtime->Unload();

#ifdef MONOLITH_STANDALONE_UI_AUTOMATION
    if (m_uiAutomation) {
      m_uiAutomation->DestroyContext();
    }
#endif
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
  bool m_runUiAutomation = false;
  bool m_uiAutomationPassed = true;
  std::unique_ptr<Standalone::LauncherUiAutomationRunner> m_uiAutomation;
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
