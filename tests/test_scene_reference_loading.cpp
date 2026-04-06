#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>

#include "editor/SceneDocument.h"
#include "editor/SceneSerializer.h"
#include "scene/SceneReferenceRuntime.h"
#include "scene/components/BehaviorComponent.h"
#include "scene/components/MeshComponent.h"
#include "scene/components/TransformComponent.h"

using namespace Monolith;
using namespace Monolith::Editor;
using Catch::Approx;

namespace {

std::string TmpPath(const std::string& name) {
  return (std::filesystem::temp_directory_path() / "horo_scene_reference_loading" / name).string();
}

class ProbeBehavior : public Behavior {
 public:
  void OnUpdate(Entity, Registry&, float) override {}
};

SceneDocument MakeReferenceSceneDocument(const std::string& filePath,
                                         const std::string& sceneId,
                                         int panelCount,
                                         int propCount,
                                         int lightCount,
                                         bool includeCamera,
                                         const std::string& assetId = "crate_asset") {
  SceneDocument doc;
  doc.sceneId = sceneId;
  doc.sceneName = sceneId;
  doc.filePath = filePath;
  doc.settings["spawnPoint"] = (sceneId == "scene_b") ? "9.0,0.5,1.0" : "4.0,0.5,1.0";
  doc.settings["gravity"] = (sceneId == "scene_b") ? "0.0,-3.0,0.0" : "0.0,-9.81,0.0";
  doc.assets[assetId] = AssetDef{"crate.obj", "1.0000,1.0000,1.0000", "crate.png"};

  for (int i = 0; i < panelCount; ++i) {
    SceneObject panel;
    panel.id = "panel_" + std::to_string(i);
    panel.type = SceneObjectType::Panel;
    panel.position = {static_cast<float>(i * 2), 1.0f, 0.0f};
    panel.scale = {1.0f + static_cast<float>(i), 1.0f, 2.0f};
    doc.objects.push_back(panel);
  }

  for (int i = 0; i < propCount; ++i) {
    SceneObject prop;
    prop.id = "prop_" + std::to_string(i);
    prop.type = SceneObjectType::Prop;
    prop.position = {1.0f + static_cast<float>(i), 2.0f, 3.0f};
    prop.scale = {1.0f, 1.0f, 1.0f};
    prop.assetId = assetId;
    if (i == 0)
      prop.components.push_back({"script", {{"behaviorTag", "Inspect"}}});
    doc.objects.push_back(prop);
  }

  for (int i = 0; i < lightCount; ++i) {
    SceneObject light;
    light.id = "light_" + std::to_string(i);
    light.type = SceneObjectType::Light;
    light.position = {0.0f, 4.0f + static_cast<float>(i), -2.0f};
    light.props["intensity"] = (sceneId == "scene_b") ? "2.5" : "1.5";
    light.props["color"] = (sceneId == "scene_b") ? "0.5,0.6,1.0" : "1.0,1.0,1.0";
    light.props["radius"] = "8.0";
    doc.objects.push_back(light);
  }

  if (includeCamera) {
    SceneObject camera;
    camera.id = "cam_000";
    camera.type = SceneObjectType::Camera;
    camera.position = {0.0f, 5.0f, -8.0f};
    camera.yaw = 10.0f;
    camera.pitch = -12.0f;
    camera.props["fov"] = (sceneId == "scene_b") ? "70.0" : "60.0";
    camera.props["nearClip"] = "0.2";
    camera.props["farClip"] = "250.0";
    doc.objects.push_back(camera);
  }

  return doc;
}

}  // namespace

TEST_CASE("SceneReferenceRuntime loads, reloads, and unloads temp-authored scenes",
          "[scene][reference-runtime]") {
  const std::string scenePath = TmpPath("reference_scene.json");
  std::filesystem::create_directories(std::filesystem::path(scenePath).parent_path());

  const SceneDocument initial = MakeReferenceSceneDocument(scenePath, "scene_a", 1, 1, 1, true);
  SceneSerializer::SaveToFile(initial, scenePath);

  Scene scene;
  SceneReferenceRuntime runtime(&scene);
  runtime.SetBehaviorFactory([](const std::string& tag) -> std::unique_ptr<Behavior> {
    if (tag == "Inspect")
      return std::make_unique<ProbeBehavior>();
    return nullptr;
  });
  runtime.SetPropEntityCreatedCallback([](const RuntimeSceneProp& prop, Entity entity, Scene& sceneRef) {
    MeshComponent mesh;
    mesh.meshTag = prop.meshTag;
    sceneRef.registry.Add<MeshComponent>(entity, std::move(mesh));
  });

  REQUIRE(runtime.GetCoordinator().GetLifecycle().GetState() == SceneLifecycleState::Uninitialized);

  const SceneDocument loaded = SceneSerializer::LoadFromFile(scenePath);
  const SceneRuntimeOperationResult load = runtime.LoadDocument(loaded);

  REQUIRE(load.ok);
  REQUIRE(runtime.GetCoordinator().IsActive());
  REQUIRE(runtime.GetCoordinator().GetLifecycle().GetState() == SceneLifecycleState::Active);
  REQUIRE(runtime.GetStats().panelCount == 1);
  REQUIRE(runtime.GetStats().propCount == 1);
  REQUIRE(runtime.GetStats().entityCount == 1);
  REQUIRE(runtime.GetStats().staticBodyCount == 1);
  REQUIRE(runtime.GetStats().behaviorCount == 1);
  REQUIRE(runtime.GetPanels().size() == 1);
  REQUIRE(runtime.GetLights().size() == 1);
  REQUIRE(runtime.GetSceneCamera().has_value());
  REQUIRE(runtime.GetSceneCamera()->fovY == Approx(60.0f));
  REQUIRE(scene.physics.gravity.y == Approx(-9.81f));
  REQUIRE(scene.physics.GetBodies().size() == 1);
  REQUIRE(scene.registry.GetEntities<TransformComponent>().size() == 1);
  REQUIRE(scene.registry.GetEntities<BehaviorComponent>().size() == 1);
  REQUIRE(scene.registry.GetEntities<MeshComponent>().size() == 1);
  const Entity propEntity = scene.registry.GetEntities<MeshComponent>()[0];
  REQUIRE(scene.registry.Get<MeshComponent>(propEntity).meshTag == "crate.obj");
  REQUIRE(runtime.GetCoordinator().GetCurrentDefinition() != nullptr);
  REQUIRE(runtime.GetCoordinator().GetCurrentDefinition()->rooms[0].props[0].scriptTag == "Inspect");

  const SceneDocument updated = MakeReferenceSceneDocument(scenePath, "scene_b", 2, 2, 2, true);
  SceneSerializer::SaveToFile(updated, scenePath);
  const SceneDocument reloaded = SceneSerializer::LoadFromFile(scenePath);
  const SceneRuntimeOperationResult reload = runtime.ReloadDocument(reloaded);

  REQUIRE(reload.ok);
  REQUIRE(runtime.GetCoordinator().GetLifecycle().GetState() == SceneLifecycleState::Active);
  REQUIRE(runtime.GetStats().panelCount == 2);
  REQUIRE(runtime.GetStats().propCount == 2);
  REQUIRE(runtime.GetStats().entityCount == 2);
  REQUIRE(runtime.GetStats().staticBodyCount == 2);
  REQUIRE(runtime.GetLights().size() == 2);
  REQUIRE(runtime.GetSceneCamera().has_value());
  REQUIRE(runtime.GetSceneCamera()->fovY == Approx(70.0f));
  REQUIRE(scene.physics.gravity.y == Approx(-3.0f));
  REQUIRE(scene.physics.GetBodies().size() == 2);
  REQUIRE(scene.registry.GetEntities<TransformComponent>().size() == 2);
  REQUIRE(scene.registry.GetEntities<MeshComponent>().size() == 2);
  REQUIRE(runtime.GetCoordinator().GetCurrentDefinition() != nullptr);
  REQUIRE(runtime.GetCoordinator().GetCurrentDefinition()->rooms[0].id == "scene_b");

  const SceneRuntimeOperationResult unload = runtime.Unload();
  REQUIRE(unload.ok);
  REQUIRE_FALSE(runtime.GetCoordinator().IsActive());
  REQUIRE(runtime.GetCoordinator().GetLifecycle().GetState() == SceneLifecycleState::Uninitialized);
  REQUIRE(runtime.GetStats().entityCount == 0);
  REQUIRE(runtime.GetPanels().empty());
  REQUIRE(runtime.GetLights().empty());
  REQUIRE_FALSE(runtime.GetSceneCamera().has_value());
  REQUIRE(scene.physics.GetBodies().empty());
  REQUIRE(scene.registry.GetEntities<TransformComponent>().empty());
}

TEST_CASE("SceneReferenceRuntime keeps active scene on build failure and does not require a behavior factory",
          "[scene][reference-runtime]") {
  const std::string scenePath = TmpPath("reference_scene_broken.json");
  std::filesystem::create_directories(std::filesystem::path(scenePath).parent_path());

  const SceneDocument valid = MakeReferenceSceneDocument(scenePath, "scene_valid", 1, 1, 1, true);
  SceneSerializer::SaveToFile(valid, scenePath);

  Scene scene;
  SceneReferenceRuntime runtime(&scene);

  const SceneRuntimeOperationResult initialLoad = runtime.LoadDocument(SceneSerializer::LoadFromFile(scenePath));
  REQUIRE(initialLoad.ok);
  REQUIRE(runtime.GetCoordinator().IsActive());
  REQUIRE(runtime.GetStats().behaviorCount == 0);
  REQUIRE(scene.registry.GetEntities<BehaviorComponent>().empty());
  REQUIRE(runtime.GetLights().size() == 1);
  REQUIRE(runtime.GetCoordinator().GetCurrentDefinition() != nullptr);
  REQUIRE(runtime.GetCoordinator().GetCurrentDefinition()->rooms[0].id == "scene_valid");

  SceneDocument broken = valid;
  broken.sceneId = "scene_broken";
  broken.objects[1].assetId = "missing_asset";
  SceneSerializer::SaveToFile(broken, scenePath);

  const SceneRuntimeOperationResult failedReload = runtime.ReloadDocument(SceneSerializer::LoadFromFile(scenePath));
  REQUIRE_FALSE(failedReload.ok);
  REQUIRE(failedReload.error.find("assetId") != std::string::npos);
  REQUIRE(runtime.GetCoordinator().IsActive());
  REQUIRE(runtime.GetCoordinator().GetLifecycle().GetState() == SceneLifecycleState::Active);
  REQUIRE(runtime.GetStats().panelCount == 1);
  REQUIRE(runtime.GetStats().propCount == 1);
  REQUIRE(runtime.GetLights().size() == 1);
  REQUIRE(runtime.GetCoordinator().GetCurrentDefinition() != nullptr);
  REQUIRE(runtime.GetCoordinator().GetCurrentDefinition()->rooms[0].id == "scene_valid");
}
