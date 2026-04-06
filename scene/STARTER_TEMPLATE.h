#pragma once
/*
 * STARTER TEMPLATE GUIDE
 *
 * This demonstrates how to create a minimal Monolith-based game from scratch.
 * Follow this pattern to scaffold new projects quickly.
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
#include "game/GameScene.h"
#include "scene/Scene.h"

namespace MyGame {

// STEP 1: Create a minimal app class
class MyGameApp : public Monolith::Application {
 public:
  MyGameApp() : Application(AppSpec{
      "My Game", 1280, 720, true, "assets/scenes/level.json"
  }) {}

 protected:
  // STEP 2: Setup (called once at startup)
  void OnInit() override {
    // Initialize core rendering and physics
    Monolith::RenderContext::Init();
    Monolith::DebugDraw::Init();

    // Initialize game-specific systems
    m_gameScene.Init(m_scene, m_camera);

    // Load initial level from JSON or code
    if (!GetDefaultSceneFilePath().empty()) {
      LoadSceneFromFile(GetDefaultSceneFilePath());
    } else {
      LoadDefaultLevel();
    }
  }

  // STEP 3: Game loop (called every frame)
  void OnUpdate(float dt) override {
    // Update camera, input, player movement, etc.
    UpdateCamera(dt);
    UpdatePlayer(dt);
  }

  // STEP 4: Physics simulation (fixed 120Hz timestep)
  void OnFixedUpdate(float dt) override {
    m_scene.UpdateSystems(dt);
  }

  // STEP 5: Rendering (variable framerate)
  void OnRender(float alpha) override {
    Monolith::RenderContext::BeginFrame();
    m_gameScene.Render(m_camera, alpha);
    Monolith::RenderContext::EndFrame();
  }

  void OnShutdown() override {
    // Cleanup
  }

 private:
  Monolith::Scene m_scene;
  Monolith::GameScene m_gameScene;
  Monolith::Camera m_camera;

  void LoadSceneFromFile(const std::string& path) {
    // Load level from JSON: SceneSerializer → EditorAdapter → LevelDef
    // See monolith/game/EditorAdapter.h for conversion pipeline
  }

  void LoadDefaultLevel() {
    // Or create level programmatically
    // See monolith/game/levels/Dungeon.cpp for example
  }

  void UpdateCamera(float dt) {
    // Implement camera follow/control here
  }

  void UpdatePlayer(float dt) {
    // Handle input and player entity updates
  }
};

}  // namespace MyGame

/*
 * STEP 6: Create main entry point (src/main.cpp)
 *
 *   #include "MyGame.h"
 *   using namespace MyGame;
 *
 *   int main(int argc, char** argv) {
 *       MyGameApp app;
 *       app.ParseArgs(argc, argv);  // Handle --editor, --play flags
 *       app.Run();  // Main game loop
 *       return 0;
 *   }
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
 * STEP 8: Level Definition (assets/scenes/level.json or code)
 *
 * If using code (see levels/Dungeon.cpp pattern):
 *   LevelDef level = MakeDungeonLevel();
 *   m_gameScene.LoadLevel(level, m_scene);
 *
 * If using JSON (see assets/scenes/world.json):
 *   auto doc = SceneSerializer::LoadFromFile("assets/scenes/level.json");
 *   auto levelDef = EditorAdapter::ToLevelDef(doc, schema);
 *   m_gameScene.LoadLevel(levelDef, m_scene);
 */
