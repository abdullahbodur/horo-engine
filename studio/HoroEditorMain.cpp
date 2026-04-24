// HoroEditorMain.cpp
//
// Backend engine process: renders via OpenGL, streams frames over WebSocket
// (port 39282), and serves MCP commands (port 39281).
// All editor UI is handled by horo-studio via MCP + WebSocket.

#include <array>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>

#include <GLFW/glfw3.h>

#include "core/Application.h"
#include "core/EngineLaunchArgs.h"
#include "core/Logger.h"
#include "core/ProjectPath.h"
#include "EditorLayer.h"
#include "FramebufferStream.h"
#include "LauncherEditorShell.h"
#include "renderer/DebugDraw.h"
#include "renderer/RenderViewUtils.h"
#include "renderer/Renderer.h"
#include "scene/Scene.h"
#include "scene/SceneReferenceRuntime.h"
#include "scene/systems/BehaviorSystem.h"
#include "scene/systems/PhysicsSystem.h"
#include "scene/systems/RenderSystem.h"

#ifdef __APPLE__
namespace Monolith { void SuppressMacOSDockIcon(); }
#endif

namespace {
using namespace Monolith;

class RendererBackendInitException final : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

class HoroEditorApp final : public Application {
 public:
  explicit HoroEditorApp(const EngineLaunchOptions& launchOptions)
      : Application(BuildSpec()),
        m_launchOptions(launchOptions),
        m_runtime(std::make_unique<SceneReferenceRuntime>(&m_scene)) {
    m_scene.AddSystem(std::make_unique<BehaviorSystem>());
    m_scene.AddSystem(std::make_unique<PhysicsSystem>(m_scene.physics));
    m_scene.AddRenderSystem(std::make_unique<RenderSystem>(m_camera, m_renderAlpha));
  }

 private:
  static AppSpec BuildSpec() {
    AppSpec spec;
    spec.name = "Horo Engine Backend";
    spec.width = 1440;
    spec.height = 920;
    spec.vsync = true;
    spec.graphicsApi = WindowGraphicsApi::OpenGL;
    return spec;
  }

  void OnInit() override {
    LOG_INFO("HoroEditorApp::OnInit begin");

#ifdef __APPLE__
    // GLFW registers the process as a foreground macOS application during
    // window creation. Override that so the engine doesn't appear in the dock
    // or the Cmd-Tab switcher when running as a headless backend.
    SuppressMacOSDockIcon();
#endif

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

    if (const RenderBackendInitResult backendInit =
            Renderer::InitializeBackend({.requested = RenderBackendId::OpenGL,
                                         .nativeWindowHandle = GetWindow().GetNativeHandle()});
        !backendInit.ok) {
      throw RendererBackendInitException("Failed to initialize renderer backend: " + backendInit.error);
    }

    DebugDraw::Init();

    m_editor.Init(GetWindow().GetNativeHandle());
    m_editor.SetLiveRegistry(&m_scene.registry);
    m_shell.Attach(&m_editor, &m_scene, m_runtime.get(), &m_camera);
    m_shell.Initialize();

    {
      Editor::EditorLayer::LauncherCallbacks launcherCallbacks;
      launcherCallbacks.openProject = [this](const std::filesystem::path& p, std::string* err) {
        return m_shell.OpenProject(p, err);
      };
      launcherCallbacks.createProject = [this](const std::string& name, const std::filesystem::path& p, std::string* err) {
        return m_shell.CreateProject(name, p, err);
      };
      launcherCallbacks.closeProject = [this]() {
        m_shell.CloseProject();
      };
      launcherCallbacks.hasProject = [this]() {
        return m_shell.HasActiveProject();
      };
      launcherCallbacks.getProjectPath = [this]() {
        return m_shell.GetProjectRoot().string();
      };
      launcherCallbacks.getProjectName = [this]() {
        return m_shell.GetProjectName();
      };
      m_editor.SetLauncherCallbacks(std::move(launcherCallbacks));
    }

#ifdef MONOLITH_FRAMEBUFFER_STREAM
    m_framebufferStream.Start(39282);
    LOG_INFO("FramebufferStream started on ws://127.0.0.1:39282");
#endif

    if (!m_launchOptions.projectPath.empty()) {
      std::string openError;
      if (!m_shell.OpenProject(m_launchOptions.projectPath, &openError))
        m_shell.SetLauncherError(openError);
    }

    LOG_INFO("HoroEditorApp::OnInit end");
  }

  void OnUpdate(float dt) override {
    m_shell.Update();
    m_editor.OnUpdate(dt, m_camera, GetWindow().GetWidth(), GetWindow().GetHeight());
  }

  void OnFixedUpdate(float dt) override {
    // Always update systems when a project is loaded.
    if (m_shell.HasActiveProject())
      m_scene.UpdateSystems(dt);
  }

  void OnRender(float alpha) override {
    m_renderAlpha = alpha;
    RenderFrameConfig frameConfig;
    frameConfig.debugLabel = "horo-engine-frame";
    if (m_shell.HasActiveProject())
      frameConfig.lights = m_runtime->GetLights();

    Renderer::BeginFrame(frameConfig);

    if (m_runtime && m_runtime->GetCoordinator().IsActive()) {
      Renderer::BeginPass(
          {RenderPassId::OpaqueScene, BuildRenderView(m_camera), "horo-engine-scene"});
      m_scene.RenderSystems(alpha);
      Renderer::EndPass();
    }

    Renderer::EndFrame();

#ifdef MONOLITH_FRAMEBUFFER_STREAM
    m_framebufferStream.BroadcastFrame(GetWindow().GetWidth(), GetWindow().GetHeight());
#endif
  }

  void OnShutdown() override {
    LOG_INFO("HoroEditorApp::OnShutdown begin");
    m_shell.Shutdown();
#ifdef MONOLITH_FRAMEBUFFER_STREAM
    m_framebufferStream.Stop();
#endif
    m_editor.Shutdown();
    if (m_runtime)
      m_runtime->Unload();
    LOG_INFO("HoroEditorApp::OnShutdown end");
  }

 public:
  EngineLaunchOptions m_launchOptions;
  Editor::EditorLayer m_editor;
  Scene m_scene;
  std::unique_ptr<SceneReferenceRuntime> m_runtime;
  Launcher::LauncherEditorShell m_shell;
  Camera m_camera;
  float m_renderAlpha = 0.0f;
#ifdef MONOLITH_FRAMEBUFFER_STREAM
  FramebufferStream m_framebufferStream;
#endif
};

}  // namespace

// Hide the GLFW window so the backend runs headless (horo-studio shows the
// rendered output via WebSocket). Respects MONOLITH_GLFW_VISIBLE=1 override.
static void ConfigureBackendProcessEnvironment() {
#ifdef _WIN32
  char* rawValue = nullptr;
  size_t valueLen = 0;
  const errno_t envResult = _dupenv_s(&rawValue, &valueLen, "MONOLITH_GLFW_VISIBLE");
  const bool hasValue = (envResult == 0 && rawValue && rawValue[0] != '\0');
  if (!hasValue)
    _putenv_s("MONOLITH_GLFW_VISIBLE", "0");
  if (rawValue)
    std::free(rawValue);
#else
  if (const char* existing = std::getenv("MONOLITH_GLFW_VISIBLE"); !existing || !*existing)
    setenv("MONOLITH_GLFW_VISIBLE", "0", 0);
#endif
}

int main(int argc, char** argv) {
  ConfigureBackendProcessEnvironment();
  const Monolith::EngineLaunchOptions launchOptions = Monolith::ParseEngineLaunchOptions(argc, argv);
  HoroEditorApp app(launchOptions);
  app.ParseArgs(argc, argv);
  app.Run();
  return 0;
}
