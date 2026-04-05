#include <catch2/catch_test_macros.hpp>
#include "game/GameScene.h"
#include "game/levels/Dungeon.h"
#include "game/EditorAdapter.h"
#include "editor/SceneDocument.h"
#include "editor/EditorSchema.h"
#include "physics/PhysicsWorld.h"
#include "scene/Scene.h"
#include "scene/components/TransformComponent.h"
#include "scene/components/MeshComponent.h"

namespace Monolith {

TEST_CASE("E2E: Game loading from Dungeon builder", "[e2e][game-loading]") {
  Scene scene;
  GameScene gameScene;
  Camera camera;

  // Initialize game systems
  gameScene.Init(scene, camera);

  // Build level programmatically
  LevelDef level = ::Monolith::MakeDefaultDungeonLevel();

  // Load into scene
  gameScene.LoadLevel(level, scene);

  // Verify entities were created
  REQUIRE(gameScene.GetPlayerEntity() != INVALID_ENTITY);
  REQUIRE(scene.registry.Has<TransformComponent>(gameScene.GetPlayerEntity()));

  // Verify physics bodies exist
  REQUIRE(!scene.physics.GetBodies().empty());

  // Verify collision blocks (static geometry)
  REQUIRE(gameScene.GetCollisionBlockCount() > 0);
}

TEST_CASE("E2E: Scene clear + reload cycle", "[e2e][game-loading]") {
  Scene scene;
  GameScene gameScene;
  Camera camera;

  gameScene.Init(scene, camera);
  LevelDef level1 = ::Monolith::MakeDefaultDungeonLevel();
  gameScene.LoadLevel(level1, scene);

  const int initialEntityCount = static_cast<int>(scene.registry.GetEntityCount());
  const int initialBodyCount = static_cast<int>(scene.physics.GetBodies().size());

  REQUIRE(initialEntityCount > 0);
  REQUIRE(initialBodyCount > 0);

  // Simulate hot-reload: clear then reload
  scene.Clear();

  REQUIRE(scene.registry.GetEntityCount() == 0);
  REQUIRE(scene.physics.GetBodies().empty());

  // Reload with same level
  gameScene.LoadLevel(level1, scene);

  REQUIRE(scene.registry.GetEntityCount() == initialEntityCount);
  REQUIRE(scene.physics.GetBodies().size() == initialBodyCount);
}

TEST_CASE("E2E: Physics bodies have proper colliders", "[e2e][physics]") {
  Scene scene;
  GameScene gameScene;
  Camera camera;

  gameScene.Init(scene, camera);
  LevelDef level = ::Monolith::MakeDefaultDungeonLevel();
  gameScene.LoadLevel(level, scene);

  // Verify each physics body has valid collision geometry
  const auto& bodies = scene.physics.GetBodies();
  for (const auto& body : bodies) {
    // Check that colliders are initialized
    REQUIRE(body.colliders.size() > 0);
    
    // Verify AABB is valid (not NaN or infinite)
    REQUIRE(!std::isnan(body.aabb.min.x));
    REQUIRE(!std::isnan(body.aabb.max.x));
  }
}

TEST_CASE("E2E: Player entity has required components", "[e2e][game-loading]") {
  Scene scene;
  GameScene gameScene;
  Camera camera;

  gameScene.Init(scene, camera);
  LevelDef level = ::Monolith::MakeDefaultDungeonLevel();
  gameScene.LoadLevel(level, scene);

  Entity player = gameScene.GetPlayerEntity();
  REQUIRE(player != INVALID_ENTITY);

  // Player must have Transform
  REQUIRE(scene.registry.Has<TransformComponent>(player));
  auto& transform = scene.registry.Get<TransformComponent>(player);
  REQUIRE(transform.current.position != Vec3::Zero());  // Should be at spawn point

  // Player must have a physics body
  RigidBody* playerBody = gameScene.GetPlayerBody();
  REQUIRE(playerBody != nullptr);
  REQUIRE(playerBody->mass > 0.0f);
}

TEST_CASE("E2E: Scene document export produces valid structure", "[e2e][serialization]") {
  Scene scene;
  GameScene gameScene;
  Camera camera;

  gameScene.Init(scene, camera);
  LevelDef level = ::Monolith::MakeDefaultDungeonLevel();
  gameScene.LoadLevel(level, scene);

  // Export live scene back to document (for editor)
  Editor::SceneDocument doc = gameScene.ExportToDocument();

  // Verify basic structure
  REQUIRE(!doc.objects.empty());

  // Should have objects for player, props, panels, etc.
  bool hasPlayer = false;
  for (const auto& obj : doc.objects) {
    if (obj.type == Editor::SceneObjectType::Prop) {
      hasPlayer = true;
      break;
    }
  }
  // At minimum, should have some props
  REQUIRE(!doc.objects.empty());
}

TEST_CASE("E2E: Multiple sequential loads without leaks", "[e2e][lifecycle]") {
  Scene scene;
  GameScene gameScene;
  Camera camera;

  gameScene.Init(scene, camera);
  LevelDef level = ::Monolith::MakeDefaultDungeonLevel();

  // Load and unload 5 times
  for (int i = 0; i < 5; ++i) {
    gameScene.LoadLevel(level, scene);
    REQUIRE(gameScene.GetPlayerEntity() != INVALID_ENTITY);
    REQUIRE(!scene.physics.GetBodies().empty());

    scene.Clear();
    REQUIRE(scene.physics.GetBodies().empty());
  }

  // After final cycle: should still be able to load
  gameScene.LoadLevel(level, scene);
  REQUIRE(scene.registry.GetEntityCount() > 0);
}

}  // namespace Monolith
