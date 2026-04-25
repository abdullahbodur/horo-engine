#pragma once
#include <memory>
#include <stdexcept>

/*
 * STARTER TEMPLATE GUIDE
 *
 * This demonstrates the engine-owned scene loading path from authoring JSON
 * into a plain Monolith scene runtime.
 *
 * FILE STRUCTURE:
 *   my-game/
 *   ├── CMakeLists.txt
 *   ├── src/
 *   │   ├── main.cpp
 *   │   └── MyGame.h/cpp
 *   ├── assets/
 *   │   ├── scenes/
 *   │   │   └── level.json
 *   │   └── models/
 *   └── build/
 */

#include "core/Application.h"
#include "editor/SceneSerializer.h"
#include "renderer/DebugDraw.h"
#include "renderer/RenderBackend.h"
#include "renderer/RenderViewUtils.h"
#include "renderer/Renderer.h"
#include "scene/Scene.h"
#include "scene/SceneReferenceRuntime.h"

namespace MyGame {
class StarterTemplateBackendInitException : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

class StarterTemplateSceneLoadException : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

// STEP 1: Create a minimal app class.
class MyGameApp : public Monolith::Application {
public:
  MyGameApp() : Application(BuildAppSpec()) {}

protected:
  // STEP 2: Setup (called once at startup).
  void OnInit() override {
    Monolith::RenderBackendSelection backendSelection;
    backendSelection.requested = Monolith::RenderBackendId::OpenGL;
    backendSelection.nativeWindowHandle = GetWindow().GetNativeHandle();
    if (const Monolith::RenderBackendInitResult backendInit =
            Monolith::Renderer::InitializeBackend(backendSelection);
        !backendInit.ok)
      throw StarterTemplateBackendInitException(
          "Failed to initialize renderer backend: " + backendInit.error);

    Monolith::DebugDraw::Init();
    m_referenceRuntime =
        std::make_unique<Monolith::SceneReferenceRuntime>(&m_scene);

    if (!GetDefaultSceneFilePath().empty())
      LoadSceneFromFile(GetDefaultSceneFilePath());
  }

  // STEP 3: Game loop (called every frame).
  void OnUpdate(float dt) override { UpdateCamera(dt); }

  // STEP 4: Physics simulation (fixed timestep).
  void OnFixedUpdate(float dt) override { m_scene.UpdateSystems(dt); }

  // STEP 5: Rendering (variable framerate).
  void OnRender(float alpha) override {
    Monolith::Renderer::BeginFrame({{}, "starter-template-frame"});
    Monolith::Renderer::BeginPass({Monolith::RenderPassId::OpaqueScene,
                                   Monolith::BuildRenderView(m_camera),
                                   "starter-template-scene"});
    m_scene.RenderSystems(alpha);
    Monolith::Renderer::EndPass();
    Monolith::Renderer::EndFrame();
  }

  void OnShutdown() override {
    if (m_referenceRuntime)
      m_referenceRuntime->Unload();
  }

private:
  Monolith::Scene m_scene;
  std::unique_ptr<Monolith::SceneReferenceRuntime> m_referenceRuntime;
  Monolith::Camera m_camera;

  static Monolith::AppSpec BuildAppSpec() {
    Monolith::AppSpec spec;
    spec.name = "My Game";
    spec.width = 1280;
    spec.height = 720;
    spec.vsync = true;
    spec.graphicsApi = Monolith::WindowGraphicsApi::OpenGL;
    spec.defaultSceneFile = "assets/scenes/level.json";
    return spec;
  }

  void LoadSceneFromFile(const std::string &path) {
    const Monolith::Editor::SceneDocument doc =
        Monolith::Editor::SceneSerializer::LoadFromFile(path);
    const Monolith::SceneRuntimeOperationResult result =
        m_referenceRuntime->LoadDocument(doc);
    if (!result.ok) {
      throw StarterTemplateSceneLoadException("Failed to load scene: " +
                                              result.error);
    }
  }

  void UpdateCamera(float dt) {
    (void)dt;
    const auto &coordinator = m_referenceRuntime->GetCoordinator();
    if (!coordinator.IsActive())
      return;

    if (const auto &sceneCamera = m_referenceRuntime->GetSceneCamera();
        sceneCamera.has_value()) {
      m_camera.position = sceneCamera->position;
      m_camera.yaw = sceneCamera->yaw;
      m_camera.pitch = sceneCamera->pitch;
      m_camera.fov = sceneCamera->fovY;
    }
  }
};
} // namespace MyGame

/* STEP 6: Create a main entry point in src/main.cpp that constructs
 * MyGameApp, parses arguments, runs the app, and returns the process code.
 */

/*
 * STEP 7: CMakeLists.txt (minimal example)
 *
 *   cmake_minimum_required(VERSION 3.16)
 *   project(MyGame)
 *
 *   # Link against horo-engine
 *   add_executable(MyGame
 *       src/main.cpp
 *       src/MyGame.h
 *       src/MyGame.cpp
 *   )
 *   target_link_libraries(MyGame PRIVATE MonolithEngine)
 *
 *   # Copy assets to build directory
 *   add_custom_command(TARGET MyGame POST_BUILD
 *       COMMAND ${CMAKE_COMMAND} -E copy_directory
 *       ${CMAKE_SOURCE_DIR}/assets
 *       $<CONFIG>/../assets
 *   )
 */

/*
 * STEP 8: Runtime control
 *
 * The engine-owned path is:
 *   SceneSerializer::LoadFromFile(...)
 *   -> SceneReferenceRuntime::LoadDocument(...)
 *
 * For hot reload:
 *   auto updated = SceneSerializer::LoadFromFile(path);
 *   auto reloadResult = m_referenceRuntime->ReloadDocument(updated);
 *
 * Lifecycle state is available from:
 *   m_referenceRuntime->GetCoordinator().GetLifecycle()
 */
