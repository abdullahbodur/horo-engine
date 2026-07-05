/**
 * @file test_scene_binary_serializer.cpp
 * @brief Unit tests for SceneBinarySerializer MessagePack round-trip.
 */
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <vector>

#include "scene/SceneBinarySerializer.h"
#include "scene/SceneProjectModel.h"

using namespace Horo;
using Catch::Approx;

namespace {

/** @brief Helper to compare two Vec3 values with approximate floating-point checks. */
void RequireVec3Equal(const Vec3 &a, const Vec3 &b,
                      float tolerance = 1e-5f) {
    REQUIRE(a.x == Approx(b.x).margin(tolerance));
    REQUIRE(a.y == Approx(b.y).margin(tolerance));
    REQUIRE(a.z == Approx(b.z).margin(tolerance));
}

/** @brief Helper to compare two string→string maps. */
void RequireMapEqual(
    const std::unordered_map<std::string, std::string, StringHash,
                             std::equal_to<>> &a,
    const std::unordered_map<std::string, std::string, StringHash,
                             std::equal_to<>> &b) {
    REQUIRE(a.size() == b.size());
    for (const auto &[k, v] : a) {
        INFO("Map key: " << k);
        REQUIRE(b.contains(k));
        REQUIRE(b.at(k) == v);
    }
}

/** @brief Helper to compare two SceneMetadata values. */
void RequireMetadataEqual(const SceneMetadata &a, const SceneMetadata &b) {
    REQUIRE(a.schemaVersion == b.schemaVersion);
    REQUIRE(a.sceneId == b.sceneId);
    REQUIRE(a.sceneName == b.sceneName);
    REQUIRE(a.sourcePath == b.sourcePath);
}

/** @brief Helper to compare two SceneAssetDefinitions. */
void RequireAssetEqual(const SceneAssetDefinition &a,
                        const SceneAssetDefinition &b) {
    REQUIRE(a.id == b.id);
    REQUIRE(a.mesh == b.mesh);
    RequireVec3Equal(a.renderScale, b.renderScale);
    REQUIRE(a.albedoMap == b.albedoMap);
    REQUIRE(a.normalMap == b.normalMap);
    REQUIRE(a.metallicRoughnessMap == b.metallicRoughnessMap);
    REQUIRE(a.emissiveMap == b.emissiveMap);
    REQUIRE(a.occlusionMap == b.occlusionMap);
    REQUIRE(a.guid == b.guid);
    REQUIRE(a.displayName == b.displayName);
}

/** @brief Helper to compare two SceneCameraProperties. */
void RequireCameraEqual(const SceneCameraProperties &a,
                         const SceneCameraProperties &b) {
    REQUIRE(a.fovY == Approx(b.fovY));
    REQUIRE(a.nearClip == Approx(b.nearClip));
    REQUIRE(a.farClip == Approx(b.farClip));
    RequireMapEqual(a.extraProps, b.extraProps);
}

/** @brief Helper to compare two SceneLightProperties. */
void RequireLightEqual(const SceneLightProperties &a,
                        const SceneLightProperties &b) {
    REQUIRE(a.kind == b.kind);
    REQUIRE(a.intensity == Approx(b.intensity));
    RequireVec3Equal(a.color, b.color);
    REQUIRE(a.radius == Approx(b.radius));
    RequireMapEqual(a.extraProps, b.extraProps);
}

/** @brief Helper to compare two SceneRigidBodyProperties. */
void RequireRigidBodyEqual(const SceneRigidBodyProperties &a,
                            const SceneRigidBodyProperties &b) {
    REQUIRE(a.mass == Approx(b.mass));
    REQUIRE(a.isKinematic == b.isKinematic);
    REQUIRE(a.useGravity == b.useGravity);
    RequireMapEqual(a.extraProps, b.extraProps);
}

/** @brief Helper to compare two SceneScriptProperties. */
void RequireScriptEqual(const SceneScriptProperties &a,
                         const SceneScriptProperties &b) {
    REQUIRE(a.behaviorTag == b.behaviorTag);
    RequireMapEqual(a.extraProps, b.extraProps);
}

/** @brief Helper to compare two SceneLooseComponent values. */
void RequireLooseComponentEqual(const SceneLooseComponent &a,
                                 const SceneLooseComponent &b) {
    REQUIRE(a.type == b.type);
    RequireMapEqual(a.props, b.props);
}

/** @brief Helper to compare two ScenePrefabReference values. */
void RequirePrefabRefEqual(const ScenePrefabReference &a,
                            const ScenePrefabReference &b) {
    REQUIRE(a.prefabId == b.prefabId);
    REQUIRE(a.sourcePath == b.sourcePath);
}

/** @brief Helper to compare two SceneNodeDefinition values. */
void RequireNodeEqual(const SceneNodeDefinition &a,
                       const SceneNodeDefinition &b) {
    REQUIRE(a.id == b.id);
    REQUIRE(a.kind == b.kind);
    RequireVec3Equal(a.position, b.position);
    RequireVec3Equal(a.scale, b.scale);
    REQUIRE(a.yaw == Approx(b.yaw));
    REQUIRE(a.pitch == Approx(b.pitch));
    REQUIRE(a.roll == Approx(b.roll));
    REQUIRE(a.assetId == b.assetId);

    REQUIRE(a.prefabInstance.has_value() == b.prefabInstance.has_value());
    if (a.prefabInstance.has_value())
        RequirePrefabRefEqual(*a.prefabInstance, *b.prefabInstance);

    REQUIRE(a.parentId.has_value() == b.parentId.has_value());
    if (a.parentId.has_value())
        REQUIRE(*a.parentId == *b.parentId);

    REQUIRE(a.camera.has_value() == b.camera.has_value());
    if (a.camera.has_value())
        RequireCameraEqual(*a.camera, *b.camera);

    REQUIRE(a.light.has_value() == b.light.has_value());
    if (a.light.has_value())
        RequireLightEqual(*a.light, *b.light);

    REQUIRE(a.rigidbody.has_value() == b.rigidbody.has_value());
    if (a.rigidbody.has_value())
        RequireRigidBodyEqual(*a.rigidbody, *b.rigidbody);

    REQUIRE(a.script.has_value() == b.script.has_value());
    if (a.script.has_value())
        RequireScriptEqual(*a.script, *b.script);

    RequireMapEqual(a.extraProps, b.extraProps);

    REQUIRE(a.extraComponents.size() == b.extraComponents.size());
    for (std::size_t i = 0; i < a.extraComponents.size(); ++i)
        RequireLooseComponentEqual(a.extraComponents[i], b.extraComponents[i]);
}

/** @brief Helper to compare two ProjectSceneReference values. */
void RequireSceneRefEqual(const ProjectSceneReference &a,
                           const ProjectSceneReference &b) {
    REQUIRE(a.sceneId == b.sceneId);
    REQUIRE(a.scenePath == b.scenePath);
}

/** @brief Full structural comparison of two SceneProjectModel values. */
void RequireModelEqual(const SceneProjectModel &a,
                        const SceneProjectModel &b) {
    // Scene metadata
    RequireMetadataEqual(a.scene.metadata, b.scene.metadata);

    // Scene settings
    RequireVec3Equal(a.scene.settings.spawnPoint, b.scene.settings.spawnPoint);
    RequireMapEqual(a.scene.settings.extraSettings, b.scene.settings.extraSettings);

    // Assets
    REQUIRE(a.scene.assets.size() == b.scene.assets.size());
    for (std::size_t i = 0; i < a.scene.assets.size(); ++i)
        RequireAssetEqual(a.scene.assets[i], b.scene.assets[i]);

    // Nodes
    REQUIRE(a.scene.nodes.size() == b.scene.nodes.size());
    for (std::size_t i = 0; i < a.scene.nodes.size(); ++i)
        RequireNodeEqual(a.scene.nodes[i], b.scene.nodes[i]);

    // Project metadata
    REQUIRE(a.project.schemaVersion == b.project.schemaVersion);
    REQUIRE(a.project.projectId == b.project.projectId);
    REQUIRE(a.project.projectName == b.project.projectName);
    REQUIRE(a.project.defaultSceneId == b.project.defaultSceneId);
    RequireMapEqual(a.project.extraSettings, b.project.extraSettings);
    REQUIRE(a.project.scenes.size() == b.project.scenes.size());
    for (std::size_t i = 0; i < a.project.scenes.size(); ++i)
        RequireSceneRefEqual(a.project.scenes[i], b.project.scenes[i]);
}

} // namespace

// =============================================================================
//  Tests
// =============================================================================

TEST_CASE("SceneBinarySerializer: default model round-trips",
          "[scene][binary-serializer]") {
    const SceneProjectModel original;

    const std::vector<uint8_t> data = SerializeSceneToBinary(original);
    REQUIRE_FALSE(data.empty());

    const SceneProjectModel restored = DeserializeSceneFromBinary(data);
    RequireModelEqual(original, restored);
}

TEST_CASE("SceneBinarySerializer: full model with all fields populated",
          "[scene][binary-serializer]") {
    SceneProjectModel model;

    // Metadata
    model.scene.metadata.schemaVersion = 3;
    model.scene.metadata.sceneId = "test_scene";
    model.scene.metadata.sceneName = "Test Scene";
    model.scene.metadata.sourcePath = "assets/scenes/test.json";

    // Settings
    model.scene.settings.spawnPoint = Vec3(10.0f, 2.5f, -5.0f);
    model.scene.settings.extraSettings["ambient"] = "dusk";
    model.scene.settings.extraSettings["gravity"] = "9.8";

    // Assets
    {
        SceneAssetDefinition a;
        a.id = "stone";
        a.mesh = "stone.obj";
        a.renderScale = Vec3(2.0f, 1.5f, 0.5f);
        a.albedoMap = "stone_albedo.png";
        a.normalMap = "stone_normal.png";
        a.metallicRoughnessMap = "stone_mr.png";
        a.emissiveMap = "stone_emissive.png";
        a.occlusionMap = "stone_occlusion.png";
        a.guid = "guid-stone-0001";
        a.displayName = "Stone Block";
        model.scene.assets.push_back(a);
    }
    {
        SceneAssetDefinition a;
        a.id = "tree";
        a.mesh = "tree.obj";
        a.renderScale = Vec3::One();
        a.albedoMap = "tree_albedo.png";
        a.guid = "guid-tree-0002";
        a.displayName = "Oak Tree";
        model.scene.assets.push_back(a);
    }

    // Nodes — cover all kinds and optional sub-structs
    {
        SceneNodeDefinition root;
        root.id = "root_000";
        root.kind = SceneNodeKind::Panel;
        root.position = Vec3(0.0f, 0.0f, 0.0f);
        root.scale = Vec3(5.0f, 1.0f, 5.0f);
        root.extraProps["tag"] = "spawn";
        model.scene.nodes.push_back(root);
    }
    {
        SceneNodeDefinition camera;
        camera.id = "cam_main";
        camera.kind = SceneNodeKind::Camera;
        camera.position = Vec3(0.0f, 5.0f, -7.0f);
        camera.yaw = 180.0f;
        camera.parentId = "root_000";
        SceneCameraProperties cp;
        cp.fovY = 65.0f;
        cp.nearClip = 0.2f;
        cp.farClip = 400.0f;
        cp.extraProps["priority"] = "high";
        camera.camera = cp;
        model.scene.nodes.push_back(camera);
    }
    {
        SceneNodeDefinition prop;
        prop.id = "prop_crate";
        prop.kind = SceneNodeKind::Prop;
        prop.position = Vec3(3.0f, 0.5f, 2.0f);
        prop.assetId = "stone";
        prop.parentId = "root_000";
        // Rigidbody
        SceneRigidBodyProperties rb;
        rb.mass = 5.0f;
        rb.isKinematic = false;
        rb.useGravity = true;
        rb.extraProps["friction"] = "0.8";
        prop.rigidbody = rb;
        // Script
        SceneScriptProperties sp;
        sp.behaviorTag = "OpenDoor";
        sp.extraProps["phase"] = "start";
        prop.script = sp;
        // Extra component
        prop.extraComponents.push_back(
            {"custom_tracker", {{"trackId", "T-001"}}});
        model.scene.nodes.push_back(prop);
    }
    {
        SceneNodeDefinition light;
        light.id = "sun";
        light.kind = SceneNodeKind::Light;
        light.position = Vec3(100.0f, 80.0f, 50.0f);
        SceneLightProperties lp;
        lp.kind = SceneLightKind::Directional;
        lp.intensity = 4.5f;
        lp.color = Vec3(1.0f, 0.95f, 0.8f);
        lp.radius = 0.0f; // directional lights ignore radius
        lp.extraProps["castShadows"] = "true";
        light.light = lp;
        model.scene.nodes.push_back(light);
    }
    {
        SceneNodeDefinition prefabNode;
        prefabNode.id = "prefab_instance";
        prefabNode.kind = SceneNodeKind::Prop;
        prefabNode.position = Vec3(1.0f, 2.0f, 3.0f);
        prefabNode.prefabInstance =
            ScenePrefabReference{"pf_001", "assets/prefabs/chair.json"};
        model.scene.nodes.push_back(prefabNode);
    }

    // Project metadata
    model.project.schemaVersion = 3;
    model.project.projectId = "test_project";
    model.project.projectName = "Test Project";
    model.project.defaultSceneId = "test_scene";
    model.project.extraSettings["author"] = "Horo";
    model.project.scenes.emplace_back("test_scene", "assets/scenes/test.json");
    model.project.scenes.emplace_back("menu", "assets/scenes/menu.json");

    // Round-trip
    const std::vector<uint8_t> data = SerializeSceneToBinary(model);
    REQUIRE_FALSE(data.empty());

    const SceneProjectModel restored = DeserializeSceneFromBinary(data);
    RequireModelEqual(model, restored);
}

TEST_CASE("SceneBinarySerializer: empty assets and nodes round-trip",
          "[scene][binary-serializer]") {
    SceneProjectModel model;
    model.scene.metadata.sceneId = "empty";
    model.scene.metadata.sceneName = "Empty Scene";

    const std::vector<uint8_t> data = SerializeSceneToBinary(model);
    const SceneProjectModel restored = DeserializeSceneFromBinary(data);

    REQUIRE(restored.scene.assets.empty());
    REQUIRE(restored.scene.nodes.empty());
    RequireModelEqual(model, restored);
}

TEST_CASE("SceneBinarySerializer: node with all optionals absent",
          "[scene][binary-serializer]") {
    SceneProjectModel model;
    SceneNodeDefinition node;
    node.id = "bare_node";
    node.kind = SceneNodeKind::Panel;
    node.position = Vec3::Zero();
    node.scale = Vec3::One();
    // All optionals left unset
    model.scene.nodes.push_back(node);

    const std::vector<uint8_t> data = SerializeSceneToBinary(model);
    const SceneProjectModel restored = DeserializeSceneFromBinary(data);

    REQUIRE(restored.scene.nodes.size() == 1);
    const auto &rn = restored.scene.nodes[0];
    REQUIRE_FALSE(rn.prefabInstance.has_value());
    REQUIRE_FALSE(rn.parentId.has_value());
    REQUIRE_FALSE(rn.camera.has_value());
    REQUIRE_FALSE(rn.light.has_value());
    REQUIRE_FALSE(rn.rigidbody.has_value());
    REQUIRE_FALSE(rn.script.has_value());
    REQUIRE(rn.extraComponents.empty());
}

TEST_CASE("SceneBinarySerializer: node with all optionals present",
          "[scene][binary-serializer]") {
    SceneProjectModel model;
    SceneNodeDefinition node;
    node.id = "full_node";
    node.kind = SceneNodeKind::Prop;
    node.prefabInstance =
        ScenePrefabReference{"pf_full", "assets/prefabs/full.json"};
    node.parentId = "parent_node";
    node.camera = SceneCameraProperties{};
    node.light = SceneLightProperties{};
    node.light->kind = SceneLightKind::Point;
    node.rigidbody = SceneRigidBodyProperties{};
    node.script = SceneScriptProperties{};
    node.script->behaviorTag = "TestBehavior";
    node.extraComponents.push_back({"type_a", {{"k1", "v1"}}});
    model.scene.nodes.push_back(node);

    const std::vector<uint8_t> data = SerializeSceneToBinary(model);
    const SceneProjectModel restored = DeserializeSceneFromBinary(data);

    REQUIRE(restored.scene.nodes.size() == 1);
    const auto &rn = restored.scene.nodes[0];
    REQUIRE(rn.prefabInstance.has_value());
    REQUIRE(rn.parentId.has_value());
    REQUIRE(rn.camera.has_value());
    REQUIRE(rn.light.has_value());
    REQUIRE(rn.rigidbody.has_value());
    REQUIRE(rn.script.has_value());
    REQUIRE(rn.extraComponents.size() == 1);
}

TEST_CASE("SceneBinarySerializer: deserialize throws on invalid data",
          "[scene][binary-serializer]") {
    // Completely garbage bytes
    const std::vector<uint8_t> garbage = {0xFF, 0x00, 0xAB, 0xCD};
    REQUIRE_THROWS_AS(DeserializeSceneFromBinary(garbage),
                      SceneBinarySerializerException);
}

TEST_CASE("SceneBinarySerializer: deserialize handles truncated valid MessagePack",
          "[scene][binary-serializer]") {
    // Build valid data first, then truncate
    SceneProjectModel model;
    model.scene.metadata.sceneId = "test";
    const std::vector<uint8_t> full = SerializeSceneToBinary(model);

    REQUIRE(full.size() > 4);
    // Truncate to a few bytes
    const std::vector<uint8_t> truncated(full.data(), full.data() + 4);
    REQUIRE_THROWS_AS(DeserializeSceneFromBinary(truncated),
                      SceneBinarySerializerException);
}

TEST_CASE("SceneBinarySerializer: deserialize empty buffer throws",
          "[scene][binary-serializer]") {
    const std::vector<uint8_t> empty;
    REQUIRE_THROWS_AS(DeserializeSceneFromBinary(empty),
                      SceneBinarySerializerException);
}

TEST_CASE("SceneBinarySerializer: repeated round-trips are stable",
          "[scene][binary-serializer]") {
    SceneProjectModel model;
    model.scene.metadata.sceneId = "stable_test";
    SceneNodeDefinition node;
    node.id = "n1";
    node.position = Vec3(1.23f, 4.56f, 7.89f);
    model.scene.nodes.push_back(node);

    // First round-trip
    std::vector<uint8_t> data1 = SerializeSceneToBinary(model);
    const SceneProjectModel restored1 = DeserializeSceneFromBinary(data1);

    // Second round-trip from the restored model
    std::vector<uint8_t> data2 = SerializeSceneToBinary(restored1);
    const SceneProjectModel restored2 = DeserializeSceneFromBinary(data2);

    // Both restores should be identical
    RequireModelEqual(restored1, restored2);

    // Binary should be byte-identical (deterministic output)
    REQUIRE(data1 == data2);
}

TEST_CASE("SceneBinarySerializer: light kinds serialize correctly",
          "[scene][binary-serializer]") {
    SceneProjectModel model;
    {
        SceneNodeDefinition point;
        point.id = "point_light";
        point.kind = SceneNodeKind::Light;
        point.light = SceneLightProperties{};
        point.light->kind = SceneLightKind::Point;
        point.light->intensity = 2.0f;
        model.scene.nodes.push_back(point);
    }
    {
        SceneNodeDefinition dir;
        dir.id = "dir_light";
        dir.kind = SceneNodeKind::Light;
        dir.light = SceneLightProperties{};
        dir.light->kind = SceneLightKind::Directional;
        dir.light->intensity = 5.0f;
        model.scene.nodes.push_back(dir);
    }

    const std::vector<uint8_t> data = SerializeSceneToBinary(model);
    const SceneProjectModel restored = DeserializeSceneFromBinary(data);

    REQUIRE(restored.scene.nodes.size() == 2);
    REQUIRE(restored.scene.nodes[0].light->kind == SceneLightKind::Point);
    REQUIRE(restored.scene.nodes[1].light->kind == SceneLightKind::Directional);
}

TEST_CASE("SceneBinarySerializer: node kinds serialize correctly",
          "[scene][binary-serializer]") {
    SceneProjectModel model;
    {
        SceneNodeDefinition panel;
        panel.id = "p";
        panel.kind = SceneNodeKind::Panel;
        model.scene.nodes.push_back(panel);
    }
    {
        SceneNodeDefinition prop;
        prop.id = "pr";
        prop.kind = SceneNodeKind::Prop;
        model.scene.nodes.push_back(prop);
    }
    {
        SceneNodeDefinition light;
        light.id = "l";
        light.kind = SceneNodeKind::Light;
        model.scene.nodes.push_back(light);
    }
    {
        SceneNodeDefinition camera;
        camera.id = "c";
        camera.kind = SceneNodeKind::Camera;
        model.scene.nodes.push_back(camera);
    }

    const std::vector<uint8_t> data = SerializeSceneToBinary(model);
    const SceneProjectModel restored = DeserializeSceneFromBinary(data);

    REQUIRE(restored.scene.nodes.size() == 4);
    REQUIRE(restored.scene.nodes[0].kind == SceneNodeKind::Panel);
    REQUIRE(restored.scene.nodes[1].kind == SceneNodeKind::Prop);
    REQUIRE(restored.scene.nodes[2].kind == SceneNodeKind::Light);
    REQUIRE(restored.scene.nodes[3].kind == SceneNodeKind::Camera);
}

TEST_CASE("SceneBinarySerializer: project scenes array round-trips",
          "[scene][binary-serializer]") {
    SceneProjectModel model;
    model.project.scenes.emplace_back("scene_a", "path/a.json");
    model.project.scenes.emplace_back("scene_b", "path/b.json");
    model.project.scenes.emplace_back("scene_c", "path/c.json");
    model.project.defaultSceneId = "scene_b";

    const std::vector<uint8_t> data = SerializeSceneToBinary(model);
    const SceneProjectModel restored = DeserializeSceneFromBinary(data);

    REQUIRE(restored.project.scenes.size() == 3);
    REQUIRE(restored.project.scenes[0].sceneId == "scene_a");
    REQUIRE(restored.project.scenes[1].sceneId == "scene_b");
    REQUIRE(restored.project.scenes[2].sceneId == "scene_c");
    REQUIRE(restored.project.defaultSceneId == "scene_b");
}

TEST_CASE("SceneBinarySerializer: binary size is compact",
          "[scene][binary-serializer]") {
    // A default model should produce a reasonably small binary
    SceneProjectModel model;
    const std::vector<uint8_t> data = SerializeSceneToBinary(model);

    // MessagePack should be well under 1KB for a default model
    REQUIRE(data.size() < 1024);
}

TEST_CASE("SceneBinarySerializer: extra settings with special characters survive",
          "[scene][binary-serializer]") {
    SceneProjectModel model;
    model.scene.settings.extraSettings["key with spaces"] = "value with = signs";
    model.scene.settings.extraSettings["unicode"] = "café";

    const std::vector<uint8_t> data = SerializeSceneToBinary(model);
    const SceneProjectModel restored = DeserializeSceneFromBinary(data);

    REQUIRE(restored.scene.settings.extraSettings.at("key with spaces") ==
            "value with = signs");
    REQUIRE(restored.scene.settings.extraSettings.at("unicode") == "café");
}
