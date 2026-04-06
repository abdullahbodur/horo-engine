#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>

#include "editor/SceneDocument.h"
#include "editor/SceneProjectBridge.h"
#include "editor/SceneRuntimeBridge.h"
#include "scene/RuntimeSceneDefinition.h"
#include "scene/SceneProjectModel.h"
#include "scene/SceneRuntimeConversion.h"

using namespace Monolith;
using namespace Monolith::Editor;
using Catch::Approx;

namespace {

class DummyBehavior : public Behavior {
 public:
  void OnUpdate(Entity, Registry&, float) override {}
};

}  // namespace

TEST_CASE("SceneRuntimeConversion: typed model converts to runtime scene definition",
          "[scene][runtime-conversion]") {
  SceneProjectModel model;
  model.scene.metadata.sceneId = "world";
  model.scene.settings.spawnPoint = {1.0f, 2.0f, 3.0f};
  model.scene.settings.extraSettings["gravity"] = "0.0,-4.0,0.0";
  model.scene.assets.push_back({"stone", "stone.obj", {2.0f, 1.5f, 0.5f}, "stone.png"});

  SceneNodeDefinition panel;
  panel.id = "panel_000";
  panel.kind = SceneNodeKind::Panel;
  panel.position = {4.0f, 5.0f, 6.0f};
  panel.scale = {7.0f, 8.0f, 9.0f};

  SceneNodeDefinition prop;
  prop.id = "prop_000";
  prop.kind = SceneNodeKind::Prop;
  prop.position = {2.0f, 4.0f, 6.0f};
  prop.scale = {1.0f, 2.0f, 3.0f};
  prop.assetId = "stone";
  prop.script = SceneScriptProperties{"OpenDoor", {}};
  prop.light = SceneLightProperties{};

  SceneNodeDefinition inlineProp;
  inlineProp.id = "prop_001";
  inlineProp.kind = SceneNodeKind::Prop;
  inlineProp.position = {5.0f, 6.0f, 7.0f};
  inlineProp.scale = {2.0f, 2.0f, 2.0f};
  inlineProp.extraProps["mesh"] = "cylinder";
  inlineProp.extraProps["renderScale"] = "1.5,1.0,0.5";

  SceneNodeDefinition light;
  light.id = "light_000";
  light.kind = SceneNodeKind::Light;
  light.position = {3.0f, 4.0f, 5.0f};
  light.light = SceneLightProperties{SceneLightKind::Directional, 4.5f, {0.2f, 0.3f, 0.4f}, 25.0f, {}};

  SceneNodeDefinition camera;
  camera.id = "cam_000";
  camera.kind = SceneNodeKind::Camera;
  camera.position = {8.0f, 9.0f, 10.0f};
  camera.yaw = 45.0f;
  camera.pitch = 95.0f;
  camera.camera = SceneCameraProperties{70.0f, 0.25f, 500.0f, {}};

  model.scene.nodes = {panel, prop, inlineProp, light, camera};

  const RuntimeSceneBuildResult build = Monolith::BuildRuntimeSceneDefinition(model);

  REQUIRE_FALSE(build.HasErrors());
  REQUIRE(build.definition.rooms.size() == 1);
  REQUIRE(build.definition.spawnPoint.x == Approx(1.0f));
  REQUIRE(build.definition.spawnPoint.y == Approx(2.0f));
  REQUIRE(build.definition.spawnPoint.z == Approx(3.0f));
  REQUIRE(build.definition.rooms[0].id == "world");
  REQUIRE(build.definition.rooms[0].gravity.y == Approx(-4.0f));
  REQUIRE(build.definition.rooms[0].panels.size() == 1);
  REQUIRE(build.definition.rooms[0].panels[0].center.x == Approx(4.0f));
  REQUIRE(build.definition.rooms[0].panels[0].half.z == Approx(9.0f));
  REQUIRE(build.definition.rooms[0].props.size() == 2);

  const RuntimeSceneProp& assetProp = build.definition.rooms[0].props[0];
  REQUIRE(assetProp.id == "prop_000");
  REQUIRE(assetProp.meshTag == "stone.obj");
  REQUIRE(assetProp.albedoMap == "stone.png");
  REQUIRE(assetProp.scale.x == Approx(2.0f));
  REQUIRE(assetProp.scale.y == Approx(3.0f));
  REQUIRE(assetProp.scale.z == Approx(1.5f));
  REQUIRE(assetProp.isLight);
  REQUIRE(assetProp.scriptTag == "OpenDoor");

  const RuntimeSceneProp& basicProp = build.definition.rooms[0].props[1];
  REQUIRE(basicProp.meshTag == "cylinder");
  REQUIRE(basicProp.scale.x == Approx(3.0f));
  REQUIRE(basicProp.scale.y == Approx(2.0f));
  REQUIRE(basicProp.scale.z == Approx(1.0f));

  REQUIRE(build.definition.lights.size() == 1);
  REQUIRE(build.definition.lights[0].type == Light::Type::Directional);
  REQUIRE(build.definition.lights[0].intensity == Approx(4.5f));
  REQUIRE(build.definition.sceneCamera.has_value());
  REQUIRE(build.definition.sceneCamera->fovY == Approx(70.0f));
  REQUIRE(build.definition.sceneCamera->nearClip == Approx(0.25f));
  REQUIRE(build.definition.sceneCamera->farClip == Approx(500.0f));
  REQUIRE(build.definition.sceneCamera->pitch == Approx(89.0f));
}

TEST_CASE("SceneRuntimeBridge: authoring document follows canonical runtime pipeline",
          "[scene][runtime-conversion][bridge]") {
  SceneDocument doc;
  doc.sceneId = "bridge_scene";
  doc.sceneName = "Bridge Scene";
  doc.filePath = "assets/scenes/bridge_scene.json";
  doc.settings["spawnPoint"] = "10.0,0.5,-4.0";
  doc.assets["crate_asset"] = AssetDef{"crate.obj", "1.0000,2.0000,3.0000", "crate.png"};

  SceneObject panel;
  panel.id = "panel_000";
  panel.type = SceneObjectType::Panel;
  panel.position = {0.0f, 1.0f, 2.0f};
  panel.scale = {3.0f, 4.0f, 5.0f};

  SceneObject prop;
  prop.id = "crate_000";
  prop.type = SceneObjectType::Prop;
  prop.position = {4.0f, 5.0f, 6.0f};
  prop.scale = {1.5f, 1.5f, 1.5f};
  prop.assetId = "crate_asset";
  prop.components.push_back({"script", {{"behaviorTag", "Inspect"}}});

  doc.objects = {panel, prop};

  const RuntimeSceneBuildResult build = Editor::BuildRuntimeSceneDefinition(doc);

  REQUIRE_FALSE(build.HasErrors());
  REQUIRE(build.definition.rooms.size() == 1);
  REQUIRE(build.definition.rooms[0].panels.size() == 1);
  REQUIRE(build.definition.rooms[0].props.size() == 1);
  REQUIRE(build.definition.rooms[0].props[0].meshTag == "crate.obj");
  REQUIRE(build.definition.rooms[0].props[0].albedoMap == "crate.png");
  REQUIRE(build.definition.rooms[0].props[0].scale.x == Approx(1.5f));
  REQUIRE(build.definition.rooms[0].props[0].scale.y == Approx(3.0f));
  REQUIRE(build.definition.rooms[0].props[0].scale.z == Approx(4.5f));
  REQUIRE(build.definition.rooms[0].props[0].scriptTag == "Inspect");
  REQUIRE(build.definition.spawnPoint.x == Approx(10.0f));
  REQUIRE(build.definition.spawnPoint.y == Approx(0.5f));
  REQUIRE(build.definition.spawnPoint.z == Approx(-4.0f));
}

TEST_CASE("SceneRuntimeConversion: missing asset references surface deterministic errors",
          "[scene][runtime-conversion][validation]") {
  SceneProjectModel model;
  model.scene.metadata.sceneId = "broken_scene";

  SceneNodeDefinition prop;
  prop.id = "prop_000";
  prop.kind = SceneNodeKind::Prop;
  prop.assetId = "missing_asset";
  model.scene.nodes.push_back(prop);

  const RuntimeSceneBuildResult build = Monolith::BuildRuntimeSceneDefinition(model);

  REQUIRE(build.HasErrors());
  REQUIRE(build.ErrorCount() >= 1);
  REQUIRE(build.definition.rooms.size() == 1);
  REQUIRE(build.definition.rooms[0].props.size() == 1);
  REQUIRE(build.definition.rooms[0].props[0].meshTag == "box");
}

TEST_CASE("RuntimeBehaviorFactory: script resolution stays consumer-owned",
          "[scene][runtime-conversion][behavior]") {
  RuntimeBehaviorFactory factory = [](const std::string& tag) -> std::unique_ptr<Behavior> {
    if (tag == "Inspect")
      return std::make_unique<DummyBehavior>();
    return nullptr;
  };

  std::unique_ptr<Behavior> behavior = factory("Inspect");
  std::unique_ptr<Behavior> missing = factory("Unknown");

  REQUIRE(behavior != nullptr);
  REQUIRE(missing == nullptr);
}
