#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <format>
#include <string>
#include <string_view>

#include "editor/SceneDocument.h"
#include "editor/SceneSerializer.h"
#include "scene/SceneReferenceRuntime.h"
#include "scene/components/BehaviorComponent.h"
#include "scene/components/MeshComponent.h"
#include "scene/components/TransformComponent.h"
#include "tests/TestTempPaths.h"

using namespace Horo;
using namespace Horo::Editor;
using Catch::Approx;

namespace {
std::string TmpPath(const std::string &name) {
  return (Horo::Tests::SecureTempBase() / "horo_scene_reference_loading" /
          name)
      .string();
}

class ProbeBehavior : public Behavior {
public:
  void OnUpdate(Entity, Registry &, float) override {
    /* Intentionally no-op for behavior attachment/lifecycle tests. */
  }
};

SceneDocument
MakeReferenceSceneDocument(std::string_view filePath, std::string_view sceneId,
                           int panelCount, int propCount, int lightCount,
                           bool includeCamera,
                           std::string_view assetId = "crate_asset") {
  SceneDocument doc;
  doc.sceneId = std::string(sceneId);
  doc.sceneName = std::string(sceneId);
  doc.filePath = std::string(filePath);
  doc.settings["spawnPoint"] =
      (sceneId == "scene_b") ? "9.0,0.5,1.0" : "4.0,0.5,1.0";
  doc.settings["gravity"] =
      (sceneId == "scene_b") ? "0.0,-3.0,0.0" : "0.0,-9.81,0.0";
  doc.assets[std::string(assetId)] =
      AssetDef{"crate.obj", "1.0000,1.0000,1.0000", "crate.png"};

  for (int i = 0; i < panelCount; ++i) {
    SceneObject panel;
    panel.id = std::format("panel_{}", i);
    panel.type = SceneObjectType::Panel;
    panel.position = {static_cast<float>(i * 2), 1.0f, 0.0f};
    panel.scale = {1.0f + static_cast<float>(i), 1.0f, 2.0f};
    doc.objects.push_back(panel);
  }

  for (int i = 0; i < propCount; ++i) {
    SceneObject prop;
    prop.id = std::format("prop_{}", i);
    prop.type = SceneObjectType::Prop;
    prop.position = {1.0f + static_cast<float>(i), 2.0f, 3.0f};
    prop.scale = {1.0f, 1.0f, 1.0f};
    prop.assetId = std::string(assetId);
    if (i == 0)
      prop.components.push_back({"script", {{"behaviorTag", "Inspect"}}});
    doc.objects.push_back(prop);
  }

  for (int i = 0; i < lightCount; ++i) {
    SceneObject light;
    light.id = std::format("light_{}", i);
    light.type = SceneObjectType::Light;
    light.position = {0.0f, 4.0f + static_cast<float>(i), -2.0f};
    light.props["intensity"] = (sceneId == "scene_b") ? "2.5" : "1.5";
    light.props["color"] =
        (sceneId == "scene_b") ? "0.5,0.6,1.0" : "1.0,1.0,1.0";
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
} // namespace

TEST_CASE("SceneReferenceRuntime loads, reloads, and unloads temp-authored scenes", "[scene][reference-runtime]") {
  const std::string scenePath = TmpPath("reference_scene.json");
  std::filesystem::create_directories(
      std::filesystem::path(scenePath).parent_path());

  const SceneDocument initial =
      MakeReferenceSceneDocument(scenePath, "scene_a", 1, 1, 1, true);
  SceneSerializer::SaveToFile(initial, scenePath);

  Scene scene;
  SceneReferenceRuntime runtime(&scene);
  runtime.SetBehaviorFactory(
      [](std::string_view tag) -> std::unique_ptr<Behavior> {
        if (tag == "Inspect")
          return std::make_unique<ProbeBehavior>();
        return nullptr;
      });
  runtime.SetPropEntityCreatedCallback(
      [](const RuntimeSceneProp &prop, Entity entity, Scene &sceneRef) {
        MeshComponent mesh;
        mesh.meshTag = prop.meshTag;
        sceneRef.GetRegistry().Add<MeshComponent>(entity, std::move(mesh));
      });

  REQUIRE(runtime.GetCoordinator().GetLifecycle().GetState() ==
          SceneLifecycleState::Uninitialized);

  const SceneDocument loaded = SceneSerializer::LoadFromFile(scenePath);
  const SceneRuntimeOperationResult load = runtime.LoadDocument(loaded);

  REQUIRE(load.ok);
  REQUIRE(runtime.GetCoordinator().IsActive());
  REQUIRE(runtime.GetCoordinator().GetLifecycle().GetState() ==
          SceneLifecycleState::Active);
  REQUIRE(runtime.GetStats().panelCount == 1);
  REQUIRE(runtime.GetStats().propCount == 1);
  REQUIRE(runtime.GetStats().entityCount == 1);
  REQUIRE(runtime.GetStats().staticBodyCount == 1);
  REQUIRE(runtime.GetStats().behaviorCount == 1);
  REQUIRE(runtime.GetPanels().size() == 1);
  REQUIRE(runtime.GetLights().size() == 1);
  REQUIRE(runtime.GetSceneCamera().has_value());
  REQUIRE(runtime.GetSceneCamera()->fovY == Approx(60.0f));
  REQUIRE(scene.GetPhysics().GetGravity().y == Approx(-9.81f));
  REQUIRE(scene.GetPhysics().GetBodies().size() == 1);
  REQUIRE(scene.GetRegistry().GetEntities<TransformComponent>().size() == 1);
  REQUIRE(scene.GetRegistry().GetEntities<BehaviorComponent>().size() == 1);
  REQUIRE(scene.GetRegistry().GetEntities<MeshComponent>().size() == 1);
  const Entity propEntity = scene.GetRegistry().GetEntities<MeshComponent>()[0];
  REQUIRE(scene.GetRegistry().Get<MeshComponent>(propEntity).meshTag ==
          "crate.obj");
  REQUIRE(runtime.GetCoordinator().GetCurrentDefinition() != nullptr);
  REQUIRE(runtime.GetCoordinator()
              .GetCurrentDefinition()
              ->rooms[0]
              .props[0]
              .scriptTag == "Inspect");

  const SceneDocument updated =
      MakeReferenceSceneDocument(scenePath, "scene_b", 2, 2, 2, true);
  SceneSerializer::SaveToFile(updated, scenePath);
  const SceneDocument reloaded = SceneSerializer::LoadFromFile(scenePath);
  const SceneRuntimeOperationResult reload = runtime.ReloadDocument(reloaded);

  REQUIRE(reload.ok);
  REQUIRE(runtime.GetCoordinator().GetLifecycle().GetState() ==
          SceneLifecycleState::Active);
  REQUIRE(runtime.GetStats().panelCount == 2);
  REQUIRE(runtime.GetStats().propCount == 2);
  REQUIRE(runtime.GetStats().entityCount == 2);
  REQUIRE(runtime.GetStats().staticBodyCount == 2);
  REQUIRE(runtime.GetLights().size() == 2);
  REQUIRE(runtime.GetSceneCamera().has_value());
  REQUIRE(runtime.GetSceneCamera()->fovY == Approx(70.0f));
  REQUIRE(scene.GetPhysics().GetGravity().y == Approx(-3.0f));
  REQUIRE(scene.GetPhysics().GetBodies().size() == 2);
  REQUIRE(scene.GetRegistry().GetEntities<TransformComponent>().size() == 2);
  REQUIRE(scene.GetRegistry().GetEntities<MeshComponent>().size() == 2);
  REQUIRE(runtime.GetCoordinator().GetCurrentDefinition() != nullptr);
  REQUIRE(runtime.GetCoordinator().GetCurrentDefinition()->rooms[0].id ==
          "scene_b");

  const SceneRuntimeOperationResult unload = runtime.Unload();
  REQUIRE(unload.ok);
  REQUIRE_FALSE(runtime.GetCoordinator().IsActive());
  REQUIRE(runtime.GetCoordinator().GetLifecycle().GetState() ==
          SceneLifecycleState::Uninitialized);
  REQUIRE(runtime.GetStats().entityCount == 0);
  REQUIRE(runtime.GetPanels().empty());
  REQUIRE(runtime.GetLights().empty());
  REQUIRE_FALSE(runtime.GetSceneCamera().has_value());
  REQUIRE(scene.GetPhysics().GetBodies().empty());
  REQUIRE(scene.GetRegistry().GetEntities<TransformComponent>().empty());
}

TEST_CASE("SceneReferenceRuntime keeps active scene on build failure and does not require a behavior factory", "[scene][reference-runtime]") {
  const std::string scenePath = TmpPath("reference_scene_broken.json");
  std::filesystem::create_directories(
      std::filesystem::path(scenePath).parent_path());

  const SceneDocument valid =
      MakeReferenceSceneDocument(scenePath, "scene_valid", 1, 1, 1, true);
  SceneSerializer::SaveToFile(valid, scenePath);

  Scene scene;
  SceneReferenceRuntime runtime(&scene);

  const SceneRuntimeOperationResult initialLoad =
      runtime.LoadDocument(SceneSerializer::LoadFromFile(scenePath));
  REQUIRE(initialLoad.ok);
  REQUIRE(runtime.GetCoordinator().IsActive());
  REQUIRE(runtime.GetStats().behaviorCount == 0);
  REQUIRE(scene.GetRegistry().GetEntities<BehaviorComponent>().empty());
  REQUIRE(runtime.GetLights().size() == 1);
  REQUIRE(runtime.GetCoordinator().GetCurrentDefinition() != nullptr);
  REQUIRE(runtime.GetCoordinator().GetCurrentDefinition()->rooms[0].id ==
          "scene_valid");

  SceneDocument broken = valid;
  broken.sceneId = "scene_broken";
  broken.objects[1].assetId = "missing_asset";
  SceneSerializer::SaveToFile(broken, scenePath);

  const SceneRuntimeOperationResult failedReload =
      runtime.ReloadDocument(SceneSerializer::LoadFromFile(scenePath));
  REQUIRE_FALSE(failedReload.ok);
  REQUIRE(failedReload.error.find("assetId") != std::string::npos);
  REQUIRE(runtime.GetCoordinator().IsActive());
  REQUIRE(runtime.GetCoordinator().GetLifecycle().GetState() ==
          SceneLifecycleState::Active);
  REQUIRE(runtime.GetStats().panelCount == 1);
  REQUIRE(runtime.GetStats().propCount == 1);
  REQUIRE(runtime.GetLights().size() == 1);
  REQUIRE(runtime.GetCoordinator().GetCurrentDefinition() != nullptr);
  REQUIRE(runtime.GetCoordinator().GetCurrentDefinition()->rooms[0].id ==
          "scene_valid");
}

TEST_CASE("SceneReferenceRuntime updates live light position and props without reload", "[scene][reference-runtime]") {
  Scene scene;
  SceneReferenceRuntime runtime(&scene);

  SceneDocument document;
  document.sceneId = "scene_live_light";
  SceneObject light;
  light.id = "light_live";
  light.type = SceneObjectType::Light;
  light.position = {1.0f, 2.0f, 3.0f};
  light.yaw = 15.0f;
  light.pitch = -10.0f;
  light.props["intensity"] = "1.5000";
  light.props["color"] = "1.0000,0.8000,0.6000";
  light.props["radius"] = "8.0000";
  document.objects.push_back(light);

  const SceneRuntimeOperationResult load = runtime.LoadDocument(document);
  REQUIRE(load.ok);
  REQUIRE(runtime.GetLights().size() == 1);
  REQUIRE(runtime.GetLights()[0].position.x == Approx(1.0f));
  REQUIRE(runtime.GetLights()[0].intensity == Approx(1.5f));
  REQUIRE(runtime.GetLights()[0].radius == Approx(8.0f));

  SceneObject liveUpdate = light;
  liveUpdate.position = {4.0f, 5.0f, 6.0f};
  liveUpdate.props["intensity"] = "2.7500";
  liveUpdate.props["color"] = "0.2000,0.4000,1.0000";
  liveUpdate.props["radius"] = "12.5000";

  std::string error;
  REQUIRE(runtime.UpdateLiveLight(liveUpdate, &error));
  REQUIRE(error.empty());
  REQUIRE(runtime.GetCoordinator().IsActive());
  REQUIRE(runtime.GetLights().size() == 1);
  REQUIRE(runtime.GetLights()[0].position.x == Approx(4.0f));
  REQUIRE(runtime.GetLights()[0].position.y == Approx(5.0f));
  REQUIRE(runtime.GetLights()[0].position.z == Approx(6.0f));
  REQUIRE(runtime.GetLights()[0].intensity == Approx(2.75f));
  REQUIRE(runtime.GetLights()[0].radius == Approx(12.5f));
  REQUIRE(runtime.GetLights()[0].color.x == Approx(0.2f));
  REQUIRE(runtime.GetLights()[0].color.y == Approx(0.4f));
  REQUIRE(runtime.GetLights()[0].color.z == Approx(1.0f));
}

// ===========================================================================
// Error-path tests — SceneReferenceRuntime.cpp
// ===========================================================================

TEST_CASE("SceneReferenceRuntime UpdateLiveLight falls back on invalid float strings (ParseFloat coverage)", "[scene][reference-runtime]") {
  // Covers lines 33-39: ParseFloat catching std::invalid_argument and
  // std::out_of_range exceptions and returning the fallback value.
  Scene scene;
  SceneReferenceRuntime runtime(&scene);

  SceneDocument document;
  document.sceneId = "scene_parsefloat";
  SceneObject light;
  light.id = "light_pf";
  light.type = SceneObjectType::Light;
  light.position = {0.0f, 1.0f, 0.0f};
  light.props["intensity"] = "1.5";
  light.props["radius"] = "8.0";
  document.objects.push_back(light);

  REQUIRE(runtime.LoadDocument(document).ok);
  REQUIRE(runtime.GetLights()[0].intensity == Approx(1.5f));
  REQUIRE(runtime.GetLights()[0].radius == Approx(8.0f));

  // std::invalid_argument path: non-numeric string — value must stay at 1.5
  SceneObject bad = light;
  bad.props["intensity"] = "not_a_float";
  std::string error;
  REQUIRE(runtime.UpdateLiveLight(bad, &error));
  REQUIRE(error.empty());
  REQUIRE(runtime.GetLights()[0].intensity == Approx(1.5f));

  // std::out_of_range path: exponent too large for float — value must stay
  // at 1.5
  bad.props["intensity"] = "1e99999";
  REQUIRE(runtime.UpdateLiveLight(bad, &error));
  REQUIRE(error.empty());
  REQUIRE(runtime.GetLights()[0].intensity == Approx(1.5f));
}

TEST_CASE("SceneReferenceRuntime UpdateLiveLight sets directional light type and computes direction via ForwardFromYawPitch", "[scene][reference-runtime]") {
  // Covers lines 49-54 (ForwardFromYawPitch) and lines 153-163
  // (lightType "directional"/"point" dispatch).
  Scene scene;
  SceneReferenceRuntime runtime(&scene);

  SceneDocument document;
  document.sceneId = "scene_lighttype";
  SceneObject light;
  light.id = "light_lt";
  light.type = SceneObjectType::Light;
  light.position = {0.0f, 2.0f, 0.0f};
  light.props["intensity"] = "1.0";
  document.objects.push_back(light);

  REQUIRE(runtime.LoadDocument(document).ok);
  REQUIRE(runtime.GetLights().size() == 1);

  // -- directional branch: triggers ForwardFromYawPitch and normalised dir --
  SceneObject dir = light;
  dir.yaw = 45.0f;
  dir.pitch = -30.0f;
  dir.props["lightType"] = "directional";
  std::string error;
  REQUIRE(runtime.UpdateLiveLight(dir, &error));
  REQUIRE(error.empty());
  {
    const Light &l = runtime.GetLights()[0];
    const float len = std::sqrt(l.direction.x * l.direction.x +
                                l.direction.y * l.direction.y +
                                l.direction.z * l.direction.z);
    REQUIRE(len == Approx(1.0f).margin(0.001f));
  }

  // -- point branch: overrides type to Point --
  SceneObject pt = light;
  pt.props["lightType"] = "point";
  REQUIRE(runtime.UpdateLiveLight(pt, &error));
  REQUIRE(error.empty());
}

TEST_CASE("SceneReferenceRuntime UpdateLiveLight returns error when light id is not found", "[scene][reference-runtime]") {
  // Covers lines 121-129: idIt == m_lightObjectIds.end() early-return path.
  Scene scene;
  SceneReferenceRuntime runtime(&scene);

  SceneDocument document;
  document.sceneId = "scene_notfound";
  SceneObject light;
  light.id = "real_light";
  light.type = SceneObjectType::Light;
  light.position = {0.0f, 1.0f, 0.0f};
  light.props["intensity"] = "1.0";
  document.objects.push_back(light);

  REQUIRE(runtime.LoadDocument(document).ok);

  SceneObject unknown = light;
  unknown.id = "does_not_exist";
  std::string error;
  REQUIRE_FALSE(runtime.UpdateLiveLight(unknown, &error));
  REQUIRE_FALSE(error.empty());
}
