#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <memory>
#include <string>

#include "ui/editor/SceneDocument.h"
#include "ui/editor/SceneProjectBridge.h"
#include "scene/SceneProjectModel.h"

using namespace Horo;
using namespace Horo::Editor;
using Catch::Approx;

namespace {
const SceneNodeDefinition *FindNode(const SceneProjectModel &model,
                                    const std::string &id) {
  const auto it = std::ranges::find_if(
      model.scene.nodes,
      [&id](const SceneNodeDefinition &node) { return node.id == id; });
  return (it != model.scene.nodes.end()) ? std::to_address(it) : nullptr;
}

const SceneAssetDefinition *FindAsset(const SceneProjectModel &model,
                                      const std::string &id) {
  const auto it = std::ranges::find_if(
      model.scene.assets,
      [&id](const SceneAssetDefinition &asset) { return asset.id == id; });
  return (it != model.scene.assets.end()) ? std::to_address(it) : nullptr;
}
} // namespace

TEST_CASE("SceneProjectModel: defaults validate cleanly", "[scene][project-model]") {
  SceneProjectModel model;

  const SceneProjectValidationResult validation =
      ValidateSceneProjectModel(model);

  REQUIRE_FALSE(validation.HasErrors());
  REQUIRE(validation.WarningCount() == 0);
  REQUIRE(model.scene.metadata.schemaVersion == 1);
  REQUIRE(model.project.schemaVersion == 1);
  REQUIRE(model.scene.settings.spawnPoint.x == Approx(2.0f));
  REQUIRE(model.scene.settings.spawnPoint.y == Approx(0.9f));
  REQUIRE(model.scene.settings.spawnPoint.z == Approx(3.0f));
}

TEST_CASE("SceneProjectBridge: builds typed scene/project model from authoring document", "[scene][project-model][bridge]") {
  SceneDocument doc;
  doc.version = 3;
  doc.sceneId = "world";
  doc.sceneName = "World";
  doc.filePath = "assets/scenes/world.json";
  doc.settings["spawnPoint"] = "1.0,2.0,3.0";
  doc.settings["ambient"] = "dusk";

  doc.assets["stone"] =
      AssetDef{"stone.obj", "2.0000,1.5000,0.5000", "stone.png"};

  SceneObject root;
  root.id = "root_000";
  root.type = SceneObjectType::Panel;
  root.position = {0.0f, 0.0f, 0.0f};
  root.scale = {4.0f, 1.0f, 4.0f};

  SceneObject camera;
  camera.id = "cam_000";
  camera.type = SceneObjectType::Camera;
  camera.position = {0.0f, 5.0f, -7.0f};
  camera.props["parentId"] = "root_000";
  camera.props["fov"] = "65";
  camera.props["nearClip"] = "0.2";
  camera.props["farClip"] = "400";
  camera.props["tag"] = "hero";

  SceneObject prop;
  prop.id = "prop_001";
  prop.type = SceneObjectType::Prop;
  prop.position = {1.0f, 2.0f, 3.0f};
  prop.scale = {1.0f, 2.0f, 1.0f};
  prop.assetId = "stone";
  prop.props["parentId"] = "root_000";
  prop.props["category"] = "pickup";
  prop.components.push_back(
      {"script", {{"behaviorTag", "OpenDoor"}, {"phase", "start"}}});
  prop.components.push_back(
      {"rigidbody", {{"mass", "2.5"}, {"isKinematic", "true"}}});
  prop.components.push_back(
      {"light", {{"intensity", "3.0"}, {"color", "1.0,0.8,0.5"}}});

  SceneObject sun;
  sun.id = "sun_000";
  sun.type = SceneObjectType::Light;
  sun.position = {5.0f, 6.0f, 7.0f};
  sun.props["lightType"] = "directional";
  sun.props["intensity"] = "4.5";
  sun.props["color"] = "0.2,0.3,0.4";
  sun.props["radius"] = "25";

  doc.objects = {root, camera, prop, sun};

  const SceneProjectModel model = BuildSceneProjectModel(doc);
  const SceneProjectValidationResult validation =
      ValidateSceneProjectModel(model);

  REQUIRE_FALSE(validation.HasErrors());
  REQUIRE(validation.WarningCount() == 0);

  REQUIRE(model.scene.metadata.schemaVersion == 3);
  REQUIRE(model.scene.metadata.sceneId == "world");
  REQUIRE(model.scene.metadata.sceneName == "World");
  REQUIRE(model.scene.metadata.sourcePath == "assets/scenes/world.json");
  REQUIRE(model.project.defaultSceneId == "world");
  REQUIRE(model.project.scenes.size() == 1);
  REQUIRE(model.project.scenes[0].scenePath == "assets/scenes/world.json");

  REQUIRE(model.scene.settings.spawnPoint.x == Approx(1.0f));
  REQUIRE(model.scene.settings.spawnPoint.y == Approx(2.0f));
  REQUIRE(model.scene.settings.spawnPoint.z == Approx(3.0f));
  REQUIRE(model.scene.settings.extraSettings.at("ambient") == "dusk");

  const SceneAssetDefinition *stone = FindAsset(model, "stone");
  REQUIRE(stone != nullptr);
  REQUIRE(stone->mesh == "stone.obj");
  REQUIRE(stone->renderScale.x == Approx(2.0f));
  REQUIRE(stone->renderScale.y == Approx(1.5f));
  REQUIRE(stone->renderScale.z == Approx(0.5f));
  REQUIRE_FALSE(stone->guid.empty());
  REQUIRE(stone->displayName == "stone");

  const SceneNodeDefinition *cam = FindNode(model, "cam_000");
  REQUIRE(cam != nullptr);
  REQUIRE(cam->kind == SceneNodeKind::Camera);
  REQUIRE(cam->parentId.has_value());
  REQUIRE(*cam->parentId == "root_000");
  REQUIRE(cam->camera.has_value());
  REQUIRE(cam->camera->fovY == Approx(65.0f));
  REQUIRE(cam->camera->nearClip == Approx(0.2f));
  REQUIRE(cam->camera->farClip == Approx(400.0f));
  REQUIRE(cam->extraProps.at("tag") == "hero");

  const SceneNodeDefinition *typedProp = FindNode(model, "prop_001");
  REQUIRE(typedProp != nullptr);
  REQUIRE(typedProp->kind == SceneNodeKind::Prop);
  REQUIRE(typedProp->assetId == "stone");
  REQUIRE(typedProp->parentId.has_value());
  REQUIRE(typedProp->script.has_value());
  REQUIRE(typedProp->script->behaviorTag == "OpenDoor");
  REQUIRE(typedProp->script->extraProps.at("phase") == "start");
  REQUIRE(typedProp->rigidbody.has_value());
  REQUIRE(typedProp->rigidbody->mass == Approx(2.5f));
  REQUIRE(typedProp->rigidbody->isKinematic);
  REQUIRE(typedProp->light.has_value());
  REQUIRE(typedProp->light->intensity == Approx(3.0f));
  REQUIRE(typedProp->extraProps.at("category") == "pickup");

  const SceneNodeDefinition *typedSun = FindNode(model, "sun_000");
  REQUIRE(typedSun != nullptr);
  REQUIRE(typedSun->kind == SceneNodeKind::Light);
  REQUIRE(typedSun->light.has_value());
  REQUIRE(typedSun->light->kind == SceneLightKind::Directional);
  REQUIRE(typedSun->light->intensity == Approx(4.5f));
  REQUIRE(typedSun->light->color.x == Approx(0.2f));
  REQUIRE(typedSun->light->color.y == Approx(0.3f));
  REQUIRE(typedSun->light->color.z == Approx(0.4f));
}

TEST_CASE("SceneProjectBridge: minimal authoring data round-trips through typed model", "[scene][project-model][bridge]") {
  SceneDocument doc;
  doc.version = 2;
  doc.sceneId = "test_scene";
  doc.sceneName = "Test Scene";
  doc.filePath = "assets/scenes/test_scene.json";
  doc.settings["spawnPoint"] = "10.0,0.5,-4.0";
  doc.settings["fog"] = "light";
  doc.assets["crate_asset"] =
      AssetDef{"crate.obj", "1.0000,2.0000,3.0000", "crate.png"};

  SceneObject prop;
  prop.id = "crate_000";
  prop.type = SceneObjectType::Prop;
  prop.position = {4.0f, 5.0f, 6.0f};
  prop.scale = {1.5f, 1.5f, 1.5f};
  prop.assetId = "crate_asset";
  prop.props["parentId"] = "root_000";
  prop.components.push_back({"script", {{"behaviorTag", "Inspect"}}});

  SceneObject root;
  root.id = "root_000";
  root.type = SceneObjectType::Panel;

  doc.objects = {root, prop};

  const SceneProjectModel model = BuildSceneProjectModel(doc);
  const SceneDocument roundTrip = BuildSceneDocument(model);

  REQUIRE(roundTrip.version == 2);
  REQUIRE(roundTrip.sceneId == "test_scene");
  REQUIRE(roundTrip.sceneName == "Test Scene");
  REQUIRE(roundTrip.filePath == "assets/scenes/test_scene.json");
  REQUIRE(roundTrip.settings.at("fog") == "light");
  REQUIRE(roundTrip.settings.at("spawnPoint") == "10.0000,0.5000,-4.0000");
  REQUIRE(roundTrip.assets.at("crate_asset").mesh == "crate.obj");
  REQUIRE(roundTrip.assets.at("crate_asset").renderScale ==
          "1.0000,2.0000,3.0000");
  REQUIRE_FALSE(roundTrip.assets.at("crate_asset").guid.empty());
  REQUIRE(roundTrip.assets.at("crate_asset").displayName == "crate_asset");
  REQUIRE(roundTrip.objects.size() == 2);
  REQUIRE(roundTrip.objects[1].id == "crate_000");
  REQUIRE(roundTrip.objects[1].assetId == "crate_asset");
  REQUIRE(roundTrip.objects[1].props.at("parentId") == "root_000");
  REQUIRE(roundTrip.objects[1].components.size() == 1);
  REQUIRE(roundTrip.objects[1].components[0].type == "script");
  REQUIRE(roundTrip.objects[1].components[0].props.at("behaviorTag") ==
          "Inspect");
}

TEST_CASE("SceneProjectModel: validation catches broken references and invalid versions", "[scene][project-model][validation]") {
  SceneProjectModel model;
  model.scene.metadata.schemaVersion = 0;
  model.project.schemaVersion = 0;
  model.scene.metadata.sceneId.clear();
  model.scene.nodes.emplace_back();
  model.scene.nodes.back().id = "dup";
  model.scene.nodes.back().assetId = "missing";
  model.scene.nodes.back().parentId = "ghost";
  model.scene.nodes.emplace_back();
  model.scene.nodes.back().id = "dup";
  model.project.defaultSceneId = "missing_scene";
  model.project.scenes.emplace_back("scene_a", "assets/scenes/a.json");

  const SceneProjectValidationResult validation =
      ValidateSceneProjectModel(model);

  REQUIRE(validation.HasErrors());
  REQUIRE(validation.ErrorCount() >= 5);
  REQUIRE(validation.WarningCount() >= 1);
}

TEST_CASE("SceneProjectModel: duplicate asset GUID is an error", "[scene][project-model][validation]") {
  SceneProjectModel model;
  SceneAssetDefinition a1;
  a1.id = "asset_a";
  a1.guid = "same-guid-0001";
  a1.mesh = "a.obj";
  SceneAssetDefinition a2;
  a2.id = "asset_b";
  a2.guid = "same-guid-0001"; // duplicate!
  a2.mesh = "b.obj";
  model.scene.assets.push_back(a1);
  model.scene.assets.push_back(a2);

  const SceneProjectValidationResult v = ValidateSceneProjectModel(model);
  REQUIRE(v.HasErrors());
  REQUIRE(v.ErrorCount() >= 1);
}

TEST_CASE("SceneProjectModel: empty asset id is an error", "[scene][project-model][validation]") {
  SceneProjectModel model;
  SceneAssetDefinition asset;
  asset.id = ""; // empty!
  asset.guid = "valid-guid";
  asset.mesh = "mesh.obj";
  model.scene.assets.push_back(asset);

  const SceneProjectValidationResult v = ValidateSceneProjectModel(model);
  REQUIRE(v.HasErrors());
}

TEST_CASE("SceneProjectModel: Camera node without camera props is a warning", "[scene][project-model][validation]") {
  SceneProjectModel model;
  SceneNodeDefinition cam;
  cam.id = "cam_no_props";
  cam.kind = SceneNodeKind::Camera;
  // No camera optional set
  model.scene.nodes.push_back(cam);

  const SceneProjectValidationResult v = ValidateSceneProjectModel(model);
  REQUIRE(v.WarningCount() >= 1);
}

TEST_CASE("SceneProjectModel: Camera node with camera props has no warning", "[scene][project-model][validation]") {
  SceneProjectModel model;
  SceneNodeDefinition cam;
  cam.id = "cam_with_props";
  cam.kind = SceneNodeKind::Camera;
  SceneCameraProperties cp;
  cp.fovY = 60.0f;
  cam.camera = cp;
  model.scene.nodes.push_back(cam);

  const SceneProjectValidationResult v = ValidateSceneProjectModel(model);
  // No warnings expected for a well-formed camera
  REQUIRE(v.WarningCount() == 0);
  REQUIRE_FALSE(v.HasErrors());
}

TEST_CASE("SceneProjectModel: prefab instance with empty prefabId is an error", "[scene][project-model][validation]") {
  SceneProjectModel model;
  SceneNodeDefinition node;
  node.id = "prefab_node";
  node.kind = SceneNodeKind::Panel;
  node.prefabInstance = ScenePrefabReference{};
  node.prefabInstance->prefabId = "";            // invalid!
  node.prefabInstance->sourcePath = "path.json"; // valid
  model.scene.nodes.push_back(node);

  const SceneProjectValidationResult v = ValidateSceneProjectModel(model);
  REQUIRE(v.HasErrors());
}

TEST_CASE("SceneProjectModel: prefab instance with empty sourcePath is an error", "[scene][project-model][validation]") {
  SceneProjectModel model;
  SceneNodeDefinition node;
  node.id = "prefab_node2";
  node.kind = SceneNodeKind::Panel;
  node.prefabInstance = ScenePrefabReference{};
  node.prefabInstance->prefabId = "pf_001";
  node.prefabInstance->sourcePath = ""; // invalid!
  model.scene.nodes.push_back(node);

  const SceneProjectValidationResult v = ValidateSceneProjectModel(model);
  REQUIRE(v.HasErrors());
}

TEST_CASE("SceneProjectModel: parentId resolving to known node is valid", "[scene][project-model][validation]") {
  SceneProjectModel model;
  SceneNodeDefinition parent;
  parent.id = "parent_000";
  parent.kind = SceneNodeKind::Panel;
  SceneNodeDefinition child;
  child.id = "child_000";
  child.kind = SceneNodeKind::Panel;
  child.parentId = "parent_000"; // resolves to parent above
  model.scene.nodes.push_back(parent);
  model.scene.nodes.push_back(child);

  const SceneProjectValidationResult v = ValidateSceneProjectModel(model);
  REQUIRE_FALSE(v.HasErrors());
}

TEST_CASE("SceneProjectModel: ErrorCount and WarningCount are precise", "[scene][project-model][validation]") {
  SceneProjectModel model;
  // One error: empty node id; one warning: Camera without props
  SceneNodeDefinition emptyId;
  emptyId.id = "";
  emptyId.kind = SceneNodeKind::Panel;
  SceneNodeDefinition camNoProps;
  camNoProps.id = "cam_np";
  camNoProps.kind = SceneNodeKind::Camera;
  model.scene.nodes.push_back(emptyId);
  model.scene.nodes.push_back(camNoProps);

  const SceneProjectValidationResult v = ValidateSceneProjectModel(model);
  REQUIRE(v.ErrorCount() >= 1);
  REQUIRE(v.WarningCount() >= 1);
  REQUIRE(v.ErrorCount() + v.WarningCount() == v.issues.size());
}
