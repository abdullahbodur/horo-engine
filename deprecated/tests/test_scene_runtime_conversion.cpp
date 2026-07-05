#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>

#include "ui/editor/SceneDocument.h"
#include "ui/editor/SceneRuntimeBridge.h"
#include "scene/RuntimeSceneDefinition.h"
#include "scene/SceneProjectModel.h"
#include "scene/SceneRuntimeConversion.h"

using namespace Horo;
using namespace Horo::Editor;
using Catch::Approx;

namespace {
class DummyBehavior : public Behavior {
public:
  void OnUpdate(Entity, Registry &, float) override {
    // no-op: behavior logic is not under test
  }
};
} // namespace

TEST_CASE("SceneRuntimeConversion: typed model converts to runtime scene definition", "[scene][runtime-conversion]") {
  SceneProjectModel model;
  model.scene.metadata.sceneId = "world";
  model.scene.settings.spawnPoint = {1.0f, 2.0f, 3.0f};
  model.scene.settings.extraSettings["gravity"] = "0.0,-4.0,0.0";
  SceneAssetDefinition stoneAsset;
  stoneAsset.id = "stone";
  stoneAsset.mesh = "stone.obj";
  stoneAsset.renderScale = {2.0f, 1.5f, 0.5f};
  stoneAsset.albedoMap = "stone.png";
  stoneAsset.normalMap = "stone_normal.png";
  stoneAsset.metallicRoughnessMap = "stone_mr.png";
  stoneAsset.emissiveMap = "stone_emissive.png";
  stoneAsset.occlusionMap = "stone_occlusion.png";
  stoneAsset.guid = "guid_stone";
  stoneAsset.displayName = "Stone";
  model.scene.assets.push_back(std::move(stoneAsset));

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
  SceneRigidBodyProperties rbProps;
  rbProps.mass = 2.0f;
  rbProps.isKinematic = true;
  rbProps.useGravity = false;
  prop.rigidbody = rbProps;

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
  light.light = SceneLightProperties{
      SceneLightKind::Directional, 4.5f, {0.2f, 0.3f, 0.4f}, 25.0f, {}};

  SceneNodeDefinition camera;
  camera.id = "cam_000";
  camera.kind = SceneNodeKind::Camera;
  camera.position = {8.0f, 9.0f, 10.0f};
  camera.yaw = 45.0f;
  camera.pitch = 95.0f;
  camera.camera = SceneCameraProperties{70.0f, 0.25f, 500.0f, {}};

  model.scene.nodes = {panel, prop, inlineProp, light, camera};

  const RuntimeSceneBuildResult build =
      Horo::BuildRuntimeSceneDefinition(model);

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

  const RuntimeSceneProp &assetProp = build.definition.rooms[0].props[0];
  REQUIRE(assetProp.id == "prop_000");
  REQUIRE(assetProp.meshTag == "stone.obj");
  REQUIRE(assetProp.albedoMap == "stone.png");
  REQUIRE(assetProp.normalMap == "stone_normal.png");
  REQUIRE(assetProp.metallicRoughnessMap == "stone_mr.png");
  REQUIRE(assetProp.emissiveMap == "stone_emissive.png");
  REQUIRE(assetProp.occlusionMap == "stone_occlusion.png");
  REQUIRE(assetProp.scale.x == Approx(2.0f));
  REQUIRE(assetProp.scale.y == Approx(3.0f));
  REQUIRE(assetProp.scale.z == Approx(1.5f));
  REQUIRE(assetProp.isLight);
  REQUIRE(assetProp.scriptTag == "OpenDoor");
  REQUIRE(assetProp.rigidbody.has_value());
  REQUIRE(assetProp.rigidbody->mass == Approx(2.0f));
  REQUIRE(assetProp.rigidbody->isKinematic);
  REQUIRE_FALSE(assetProp.rigidbody->useGravity);

  const RuntimeSceneProp &basicProp = build.definition.rooms[0].props[1];
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

TEST_CASE("SceneRuntimeConversion maps extended light kinds and orientation", "[scene][runtime]") {
  SceneProjectModel model;

  SceneNodeDefinition spot;
  spot.id = "spot";
  spot.kind = SceneNodeKind::Light;
  spot.position = {1.0f, 2.0f, 3.0f};
  spot.yaw = 90.0f;
  spot.pitch = -30.0f;
  spot.light = SceneLightProperties{
      SceneLightKind::Spot, 2.0f, {1.0f, 0.8f, 0.6f}, 12.0f, {}};
  model.scene.nodes = {spot};

  const RuntimeSceneBuildResult build = Horo::BuildRuntimeSceneDefinition(model);

  REQUIRE_FALSE(build.HasErrors());
  REQUIRE(build.definition.lights.size() == 1u);
  const Light &light = build.definition.lights.front();
  CHECK(light.type == Light::Type::Spot);
  CHECK(light.radius == Approx(12.0f));
  CHECK(light.direction.x == Approx(-0.8660254f).margin(0.0001f));
  CHECK(light.direction.y == Approx(-0.5f).margin(0.0001f));
}

TEST_CASE("SceneRuntimeBridge: authoring document follows canonical runtime pipeline", "[scene][runtime-conversion][bridge]") {
  SceneDocument doc;
  doc.sceneId = "bridge_scene";
  doc.sceneName = "Bridge Scene";
  doc.filePath = "assets/scenes/bridge_scene.json";
  doc.settings["spawnPoint"] = "10.0,0.5,-4.0";
  AssetDef crateAsset{"crate.obj", "1.0000,2.0000,3.0000", "crate.png"};
  crateAsset.normalMap = "crate_normal.png";
  crateAsset.metallicRoughnessMap = "crate_mr.png";
  crateAsset.emissiveMap = "crate_emissive.png";
  crateAsset.occlusionMap = "crate_occlusion.png";
  doc.assets["crate_asset"] = crateAsset;

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

  const RuntimeSceneBuildResult build =
      Editor::BuildRuntimeSceneDefinition(doc);

  REQUIRE_FALSE(build.HasErrors());
  REQUIRE(build.definition.rooms.size() == 1);
  REQUIRE(build.definition.rooms[0].panels.size() == 1);
  REQUIRE(build.definition.rooms[0].props.size() == 1);
  REQUIRE(build.definition.rooms[0].props[0].meshTag == "crate.obj");
  REQUIRE(build.definition.rooms[0].props[0].albedoMap == "crate.png");
  REQUIRE(build.definition.rooms[0].props[0].normalMap == "crate_normal.png");
  REQUIRE(build.definition.rooms[0].props[0].metallicRoughnessMap ==
          "crate_mr.png");
  REQUIRE(build.definition.rooms[0].props[0].emissiveMap ==
          "crate_emissive.png");
  REQUIRE(build.definition.rooms[0].props[0].occlusionMap ==
          "crate_occlusion.png");
  REQUIRE(build.definition.rooms[0].props[0].scale.x == Approx(1.5f));
  REQUIRE(build.definition.rooms[0].props[0].scale.y == Approx(3.0f));
  REQUIRE(build.definition.rooms[0].props[0].scale.z == Approx(4.5f));
  REQUIRE(build.definition.rooms[0].props[0].scriptTag == "Inspect");
  REQUIRE(build.definition.spawnPoint.x == Approx(10.0f));
  REQUIRE(build.definition.spawnPoint.y == Approx(0.5f));
  REQUIRE(build.definition.spawnPoint.z == Approx(-4.0f));
}

TEST_CASE("SceneRuntimeConversion: missing asset references surface deterministic errors", "[scene][runtime-conversion][validation]") {
  SceneProjectModel model;
  model.scene.metadata.sceneId = "broken_scene";

  SceneNodeDefinition prop;
  prop.id = "prop_000";
  prop.kind = SceneNodeKind::Prop;
  prop.assetId = "missing_asset";
  model.scene.nodes.push_back(prop);

  const RuntimeSceneBuildResult build =
      Horo::BuildRuntimeSceneDefinition(model);

  REQUIRE(build.HasErrors());
  REQUIRE(build.ErrorCount() >= 1);
  REQUIRE(build.definition.rooms.size() == 1);
  REQUIRE(build.definition.rooms[0].props.size() == 1);
  REQUIRE(build.definition.rooms[0].props[0].meshTag == "box");
}

TEST_CASE("RuntimeBehaviorFactory: script resolution stays consumer-owned", "[scene][runtime-conversion][behavior]") {
  RuntimeBehaviorFactory factory =
      [](std::string_view tag) -> std::unique_ptr<Behavior> {
    if (tag == "Inspect")
      return std::make_unique<DummyBehavior>();
    return nullptr;
  };

  std::unique_ptr<Behavior> behavior = factory("Inspect");
  std::unique_ptr<Behavior> missing = factory("Unknown");

  REQUIRE(behavior != nullptr);
  REQUIRE(missing == nullptr);
}

TEST_CASE("RuntimeSceneBuildResult: WarningCount counts severity Warning only", "[scene][runtime-conversion][warnings]") {
  using enum RuntimeSceneBuildIssue::Severity;
  RuntimeSceneBuildResult result;
  CHECK(result.WarningCount() == 0);
  CHECK(result.ErrorCount() == 0);

  result.issues.emplace_back(Warning, "node_w", "minor warning");
  CHECK(result.WarningCount() == 1);
  CHECK(result.ErrorCount() == 0);

  result.issues.emplace_back(Error, "node_e", "critical error");
  CHECK(result.WarningCount() == 1);
  CHECK(result.ErrorCount() == 1);

  result.issues.emplace_back(Warning, "node_w2", "another warn");
  CHECK(result.WarningCount() == 2);
  CHECK(result.ErrorCount() == 1);
}
