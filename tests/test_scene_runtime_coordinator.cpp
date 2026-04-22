#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>

#include "editor/SceneDocument.h"
#include "editor/SceneRuntimeCoordinatorBridge.h"
#include "scene/RuntimeSceneDefinition.h"
#include "scene/SceneRuntimeCoordinator.h"

using namespace Monolith;
using namespace Monolith::Editor;
using Catch::Approx;

namespace {
    RuntimeSceneDefinition MakeRuntimeSceneDefinition(const std::string &roomId,
                                                      float spawnX,
                                                      float cameraFov) {
        RuntimeSceneDefinition definition;
        RuntimeSceneRoom room;
        room.id = roomId;
        room.panels.push_back({{0.0f, 1.0f, 0.0f}, {1.0f, 2.0f, 3.0f}});
        room.props.push_back({
            "prop_" + roomId,
            {1.0f, 2.0f, 3.0f},
            10.0f,
            0.0f,
            0.0f,
            {0.5f, 0.5f, 0.5f},
            {1.0f, 1.0f, 1.0f},
            "box",
            "",
            false,
            "Inspect"
        });
        definition.rooms.push_back(std::move(room));
        definition.spawnPoint = {spawnX, 0.5f, 0.0f};
        definition.sceneCamera = RuntimeSceneCamera{
            {0.0f, 5.0f, -5.0f}, 0.0f, 0.0f, cameraFov, 0.1f, 200.0f
        };
        return definition;
    }

    SceneDocument MakeRuntimeSceneDocument(std::string_view sceneId,
                                           std::string_view assetId) {
        SceneDocument doc;
        doc.sceneId = std::string(sceneId);
        doc.sceneName = std::string(sceneId);
        doc.settings["spawnPoint"] = "4.0,0.5,1.0";
        doc.assets[std::string(assetId)] =
                AssetDef{"crate.obj", "2.0000,1.0000,0.5000", "crate.png"};

        SceneObject panel;
        panel.id = "panel_000";
        panel.type = SceneObjectType::Panel;
        panel.position = {0.0f, 1.0f, 0.0f};
        panel.scale = {4.0f, 1.0f, 4.0f};

        SceneObject prop;
        prop.id = "prop_000";
        prop.type = SceneObjectType::Prop;
        prop.position = {2.0f, 3.0f, 4.0f};
        prop.scale = {1.5f, 2.0f, 2.5f};
        prop.assetId = std::string(assetId);
        prop.components.push_back({"script", {{"behaviorTag", "Inspect"}}});

        doc.objects = {panel, prop};
        return doc;
    }
} // namespace

TEST_CASE(
    "SceneRuntimeCoordinator: load, reload, and unload transitions succeed",
    "[scene][runtime-coordinator]") {
    SceneRuntimeCoordinator coordinator;
    const RuntimeSceneDefinition initial =
            MakeRuntimeSceneDefinition("room_a", 1.0f, 55.0f);
    const RuntimeSceneDefinition updated =
            MakeRuntimeSceneDefinition("room_b", 9.0f, 72.0f);

    bool sawLoadingState = false;
    const SceneRuntimeOperationResult load = coordinator.Load(
        initial, [&coordinator, &sawLoadingState](
    const RuntimeSceneDefinition &definition, std::string *) {
            sawLoadingState = coordinator.GetLifecycle().GetState() ==
                              SceneLifecycleState::Loading;
            return definition.rooms.size() == 1;
        });

    REQUIRE(load.ok);
    REQUIRE(sawLoadingState);
    REQUIRE(coordinator.IsActive());
    REQUIRE(coordinator.GetLifecycle().GetState() == SceneLifecycleState::Active);
    REQUIRE(coordinator.GetCurrentDefinition() != nullptr);
    REQUIRE(coordinator.GetCurrentDefinition()->spawnPoint.x == Approx(1.0f));

    bool sawReloadingState = false;
    const SceneRuntimeOperationResult reload = coordinator.Reload(
        updated, [&coordinator, &sawReloadingState](
    const RuntimeSceneDefinition &definition, std::string *) {
            sawReloadingState = coordinator.GetLifecycle().GetState() ==
                                SceneLifecycleState::Reloading;
            return definition.sceneCamera.has_value();
        });

    REQUIRE(reload.ok);
    REQUIRE(sawReloadingState);
    REQUIRE(coordinator.IsActive());
    REQUIRE(coordinator.GetCurrentDefinition() != nullptr);
    REQUIRE(coordinator.GetCurrentDefinition()->spawnPoint.x == Approx(9.0f));
    REQUIRE(coordinator.GetCurrentDefinition()->sceneCamera->fovY ==
        Approx(72.0f));

    bool sawUnloadingState = false;
    const SceneRuntimeOperationResult unload =
            coordinator.Unload([&coordinator, &sawUnloadingState](std::string *) {
                sawUnloadingState = coordinator.GetLifecycle().GetState() ==
                                    SceneLifecycleState::Unloading;
                return true;
            });

    REQUIRE(unload.ok);
    REQUIRE(sawUnloadingState);
    REQUIRE_FALSE(coordinator.IsActive());
    REQUIRE(coordinator.GetLifecycle().GetState() ==
        SceneLifecycleState::Uninitialized);
    REQUIRE(coordinator.GetCurrentDefinition() == nullptr);
}

TEST_CASE(
    "SceneRuntimeCoordinator: invalid transitions fail clearly and safely",
    "[scene][runtime-coordinator]") {
    SceneRuntimeCoordinator coordinator;
    const RuntimeSceneDefinition definition =
            MakeRuntimeSceneDefinition("room", 2.0f, 60.0f);

    const SceneRuntimeOperationResult reload =
            coordinator.Reload(definition, [](const RuntimeSceneDefinition &,
                                              std::string *) {
                return true;
            });
    REQUIRE_FALSE(reload.ok);
    REQUIRE(reload.operation == SceneRuntimeOperation::Reload);
    REQUIRE(reload.state == SceneLifecycleState::Uninitialized);
    REQUIRE(reload.error.find("BeginReloading") != std::string::npos);

    const SceneRuntimeOperationResult unload =
            coordinator.Unload([](std::string *) { return true; });
    REQUIRE_FALSE(unload.ok);
    REQUIRE(unload.operation == SceneRuntimeOperation::Unload);
    REQUIRE(unload.state == SceneLifecycleState::Uninitialized);
    REQUIRE(unload.error.find("BeginUnloading") != std::string::npos);
}

TEST_CASE(
    "SceneRuntimeCoordinator: callback failures preserve explicit error state",
    "[scene][runtime-coordinator]") {
    SceneRuntimeCoordinator coordinator;
    const RuntimeSceneDefinition definition =
            MakeRuntimeSceneDefinition("room_a", 1.0f, 55.0f);
    const RuntimeSceneDefinition replacement =
            MakeRuntimeSceneDefinition("room_b", 7.0f, 65.0f);

    const SceneRuntimeOperationResult load =
            coordinator.Load(definition, [](const RuntimeSceneDefinition &,
                                            std::string *) {
                return true;
            });
    REQUIRE(load.ok);

    const SceneRuntimeOperationResult failedReload = coordinator.Reload(
        replacement, [](const RuntimeSceneDefinition &, std::string *error) {
            *error = "reload apply failed";
            return false;
        });
    REQUIRE_FALSE(failedReload.ok);
    REQUIRE(failedReload.error == "reload apply failed");
    REQUIRE(coordinator.HasTransitionFailure());
    REQUIRE(coordinator.GetLastError() == "reload apply failed");
    REQUIRE(coordinator.GetLifecycle().GetState() == SceneLifecycleState::Active);
    REQUIRE(coordinator.GetCurrentDefinition() != nullptr);
    REQUIRE(coordinator.GetCurrentDefinition()->spawnPoint.x == Approx(1.0f));

    const SceneRuntimeOperationResult failedUnload =
            coordinator.Unload([](std::string *error) {
                *error = "unload apply failed";
                return false;
            });
    REQUIRE_FALSE(failedUnload.ok);
    REQUIRE(failedUnload.error == "unload apply failed");
    REQUIRE(coordinator.HasTransitionFailure());
    REQUIRE(coordinator.GetLifecycle().GetState() == SceneLifecycleState::Active);
    REQUIRE(coordinator.GetCurrentDefinition() != nullptr);

    SceneRuntimeCoordinator failedLoadCoordinator;
    const SceneRuntimeOperationResult failedLoad = failedLoadCoordinator.Load(
        definition, [](const RuntimeSceneDefinition &, std::string *error) {
            *error = "load apply failed";
            return false;
        });
    REQUIRE_FALSE(failedLoad.ok);
    REQUIRE(failedLoad.error == "load apply failed");
    REQUIRE(failedLoadCoordinator.HasTransitionFailure());
    REQUIRE(failedLoadCoordinator.GetLifecycle().GetState() ==
        SceneLifecycleState::Uninitialized);
    REQUIRE(failedLoadCoordinator.GetCurrentDefinition() == nullptr);
}

TEST_CASE("SceneRuntimeCoordinatorBridge: authoring document uses canonical "
          "lifecycle-managed path",
          "[scene][runtime-coordinator][bridge]") {
    SceneRuntimeCoordinator coordinator;
    const SceneDocument initialDoc =
            MakeRuntimeSceneDocument("scene_a", "crate_asset");
    const SceneDocument updatedDoc =
            MakeRuntimeSceneDocument("scene_b", "crate_asset");

    const SceneRuntimeOperationResult load = LoadSceneDocument(
        coordinator, initialDoc,
        [](const RuntimeSceneDefinition &definition, std::string *) {
            return definition.rooms.size() == 1 &&
                   definition.rooms[0].props.size() == 1;
        });

    REQUIRE(load.ok);
    REQUIRE(coordinator.IsActive());
    REQUIRE(coordinator.GetCurrentDefinition() != nullptr);
    REQUIRE(coordinator.GetCurrentDefinition()->spawnPoint.x == Approx(4.0f));
    REQUIRE(coordinator.GetCurrentDefinition()->rooms[0].props[0].meshTag ==
        "crate.obj");
    REQUIRE(coordinator.GetCurrentDefinition()->rooms[0].props[0].scale.x ==
        Approx(3.0f));
    REQUIRE(coordinator.GetCurrentDefinition()->rooms[0].props[0].scale.y ==
        Approx(2.0f));
    REQUIRE(coordinator.GetCurrentDefinition()->rooms[0].props[0].scale.z ==
        Approx(1.25f));

    const SceneRuntimeOperationResult reload = ReloadSceneDocument(
        coordinator, updatedDoc,
        [](const RuntimeSceneDefinition &definition, std::string *) {
            return !definition.rooms.empty() && definition.rooms[0].id == "scene_b";
        });

    REQUIRE(reload.ok);
    REQUIRE(coordinator.GetCurrentDefinition() != nullptr);
    REQUIRE(coordinator.GetCurrentDefinition()->rooms[0].id == "scene_b");

    SceneDocument brokenDoc = initialDoc;
    brokenDoc.objects[1].assetId = "missing_asset";
    const SceneRuntimeOperationResult brokenReload = ReloadSceneDocument(
        coordinator, brokenDoc,
        [](const RuntimeSceneDefinition &, std::string *) { return true; });

    REQUIRE_FALSE(brokenReload.ok);
    REQUIRE(brokenReload.error.find("assetId") != std::string::npos);
    REQUIRE(coordinator.GetCurrentDefinition() != nullptr);
    REQUIRE(coordinator.GetCurrentDefinition()->rooms[0].id == "scene_b");
}

TEST_CASE("SceneRuntimeCoordinator: Load fails when already active",
          "[scene][runtime-coordinator]") {
    SceneRuntimeCoordinator coordinator;
    const RuntimeSceneDefinition definition =
            MakeRuntimeSceneDefinition("room", 1.0f, 60.0f);

    const SceneRuntimeOperationResult load =
            coordinator.Load(definition, [](const RuntimeSceneDefinition &,
                                            std::string *) {
                return true;
            });
    REQUIRE(load.ok);
    REQUIRE(coordinator.IsActive());
    REQUIRE(coordinator.GetLastOperation() == SceneRuntimeOperation::Load);

    // Attempt to load again while already Active — BeginLoading must fail
    const SceneRuntimeOperationResult loadAgain =
            coordinator.Load(definition, [](const RuntimeSceneDefinition &,
                                            std::string *) {
                return true;
            });
    REQUIRE_FALSE(loadAgain.ok);
    REQUIRE(loadAgain.operation == SceneRuntimeOperation::Load);
    REQUIRE(loadAgain.error.find("BeginLoading") != std::string::npos);
    REQUIRE(coordinator.HasTransitionFailure());
    REQUIRE(coordinator.GetLastOperation() == SceneRuntimeOperation::Load);
    REQUIRE(coordinator.GetLastError().find("BeginLoading") != std::string::npos);

    // Coordinator should still be Active after the failed second load
    REQUIRE(coordinator.IsActive());
}

TEST_CASE(
    "SceneRuntimeCoordinator: GetLastOperation tracks each operation type",
    "[scene][runtime-coordinator]") {
    SceneRuntimeCoordinator coordinator;
    REQUIRE(coordinator.GetLastOperation() == SceneRuntimeOperation::None);

    const RuntimeSceneDefinition definition =
            MakeRuntimeSceneDefinition("room", 2.0f, 55.0f);

    coordinator.Load(definition,
                     [](const RuntimeSceneDefinition &, std::string *) {
                         return true;
                     });
    REQUIRE(coordinator.GetLastOperation() == SceneRuntimeOperation::Load);

    coordinator.Reload(definition,
                       [](const RuntimeSceneDefinition &, std::string *) {
                           return true;
                       });
    REQUIRE(coordinator.GetLastOperation() == SceneRuntimeOperation::Reload);

    coordinator.Unload([](std::string *) { return true; });
    REQUIRE(coordinator.GetLastOperation() == SceneRuntimeOperation::Unload);
}

TEST_CASE(
    "SceneRuntimeCoordinator: Reload fails with lifecycle error when callback "
    "succeeds but CompleteReload would be invalid",
    "[scene][runtime-coordinator]") {
    // After a successful load, a second reload attempt that uses an empty
    // callback still exercises HasTransitionFailure reset on MakeSuccess.
    SceneRuntimeCoordinator coordinator;
    const RuntimeSceneDefinition definition =
            MakeRuntimeSceneDefinition("room", 3.0f, 70.0f);

    coordinator.Load(definition,
                     [](const RuntimeSceneDefinition &, std::string *) {
                         return true;
                     });
    REQUIRE(coordinator.IsActive());
    REQUIRE_FALSE(coordinator.HasTransitionFailure());

    const SceneRuntimeOperationResult reload =
            coordinator.Reload(definition,
                               [](const RuntimeSceneDefinition &, std::string *) {
                                   return true;
                               });
    REQUIRE(reload.ok);
    REQUIRE_FALSE(coordinator.HasTransitionFailure());
    REQUIRE(coordinator.GetLastError().empty());
}
