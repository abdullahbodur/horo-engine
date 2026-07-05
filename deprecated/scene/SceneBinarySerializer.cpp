/**
 * @file SceneBinarySerializer.cpp
 * @brief Binary serialization of SceneProjectModel via MessagePack.
 *
 * Internal pipeline:  SceneProjectModel  ←→  nlohmann::json  ←→  msgpack bytes.
 * The JSON intermediate is constructed/destructed by hand so the serializer
 * controls the exact key set and type fidelity — no generic reflection.
 */
#include "scene/SceneBinarySerializer.h"

#include <nlohmann/json.hpp>

#include "scene/SceneProjectModel.h"

namespace Horo {

using json = nlohmann::json;

// ============================================================================
//  Serialization helpers:  struct → json
// ============================================================================

namespace {

/** @brief Converts a Vec3 to a JSON array [x, y, z]. */
json ToJson(const Vec3 &v) {
    return json::array({v.x, v.y, v.z});
}

/** @brief Converts a string→string map to a JSON object with sorted keys
 *         for deterministic output. */
json ToJson(const std::unordered_map<std::string, std::string,
                                      StringHash, std::equal_to<>> &map) {
    json obj = json::object();
    // Sort keys for deterministic serialization
    std::vector<std::string> keys;
    keys.reserve(map.size());
    for (const auto &[k, v] : map)
        keys.push_back(k);
    std::ranges::sort(keys);
    for (const auto &k : keys)
        obj[k] = map.at(k);
    return obj;
}

json ToJson(const SceneMetadata &m) {
    return json::object({
        {"schemaVersion", m.schemaVersion},
        {"sceneId", m.sceneId},
        {"sceneName", m.sceneName},
        {"sourcePath", m.sourcePath},
    });
}

json ToJson(const SceneSettings &s) {
    return json::object({
        {"spawnPoint", ToJson(s.spawnPoint)},
        {"extraSettings", ToJson(s.extraSettings)},
    });
}

json ToJson(const SceneAssetDefinition &a) {
    return json::object({
        {"id", a.id},
        {"mesh", a.mesh},
        {"renderScale", ToJson(a.renderScale)},
        {"albedoMap", a.albedoMap},
        {"normalMap", a.normalMap},
        {"metallicRoughnessMap", a.metallicRoughnessMap},
        {"emissiveMap", a.emissiveMap},
        {"occlusionMap", a.occlusionMap},
        {"guid", a.guid},
        {"displayName", a.displayName},
    });
}

json ToJson(const SceneCameraProperties &c) {
    return json::object({
        {"fovY", c.fovY},
        {"nearClip", c.nearClip},
        {"farClip", c.farClip},
        {"extraProps", ToJson(c.extraProps)},
    });
}

json ToJson(const SceneLightProperties &l) {
    return json::object({
        {"kind", static_cast<int>(l.kind)},
        {"intensity", l.intensity},
        {"color", ToJson(l.color)},
        {"radius", l.radius},
        {"extraProps", ToJson(l.extraProps)},
    });
}

json ToJson(const SceneRigidBodyProperties &r) {
    return json::object({
        {"mass", r.mass},
        {"isKinematic", r.isKinematic},
        {"useGravity", r.useGravity},
        {"extraProps", ToJson(r.extraProps)},
    });
}

json ToJson(const SceneScriptProperties &s) {
    return json::object({
        {"behaviorTag", s.behaviorTag},
        {"extraProps", ToJson(s.extraProps)},
    });
}

json ToJson(const SceneLooseComponent &lc) {
    return json::object({
        {"type", lc.type},
        {"props", ToJson(lc.props)},
    });
}

json ToJson(const ScenePrefabReference &pr) {
    return json::object({
        {"prefabId", pr.prefabId},
        {"sourcePath", pr.sourcePath},
    });
}

json ToJson(const SceneNodeDefinition &n) {
    json j = json::object({
        {"id", n.id},
        {"kind", static_cast<int>(n.kind)},
        {"position", ToJson(n.position)},
        {"scale", ToJson(n.scale)},
        {"yaw", n.yaw},
        {"pitch", n.pitch},
        {"roll", n.roll},
        {"assetId", n.assetId},
        {"extraProps", ToJson(n.extraProps)},
    });

    if (n.prefabInstance.has_value())
        j["prefabInstance"] = ToJson(*n.prefabInstance);
    if (n.parentId.has_value())
        j["parentId"] = *n.parentId;
    if (n.camera.has_value())
        j["camera"] = ToJson(*n.camera);
    if (n.light.has_value())
        j["light"] = ToJson(*n.light);
    if (n.rigidbody.has_value())
        j["rigidbody"] = ToJson(*n.rigidbody);
    if (n.script.has_value())
        j["script"] = ToJson(*n.script);

    if (!n.extraComponents.empty()) {
        json ec = json::array();
        for (const auto &lc : n.extraComponents)
            ec.push_back(ToJson(lc));
        j["extraComponents"] = std::move(ec);
    }

    return j;
}

json ToJson(const ProjectSceneReference &pr) {
    return json::object({
        {"sceneId", pr.sceneId},
        {"scenePath", pr.scenePath},
    });
}

json ToJson(const ProjectMetadata &pm) {
    json scenes = json::array();
    for (const auto &s : pm.scenes)
        scenes.push_back(ToJson(s));

    return json::object({
        {"schemaVersion", pm.schemaVersion},
        {"projectId", pm.projectId},
        {"projectName", pm.projectName},
        {"defaultSceneId", pm.defaultSceneId},
        {"extraSettings", ToJson(pm.extraSettings)},
        {"scenes", std::move(scenes)},
    });
}

json ToJson(const SceneDefinition &sd) {
    json assets = json::array();
    for (const auto &a : sd.assets)
        assets.push_back(ToJson(a));

    json nodes = json::array();
    for (const auto &n : sd.nodes)
        nodes.push_back(ToJson(n));

    return json::object({
        {"metadata", ToJson(sd.metadata)},
        {"settings", ToJson(sd.settings)},
        {"assets", std::move(assets)},
        {"nodes", std::move(nodes)},
    });
}

json ToJson(const SceneProjectModel &model) {
    return json::object({
        {"scene", ToJson(model.scene)},
        {"project", ToJson(model.project)},
    });
}

// ============================================================================
//  Deserialization helpers:  json → struct
// ============================================================================

/** @brief Reads a JSON array [x, y, z] into a Vec3.  Returns fallback on error. */
Vec3 Vec3FromJson(const json &j, const Vec3 &fallback = Vec3::Zero()) {
    if (!j.is_array() || j.size() < 3)
        return fallback;
    return Vec3(j[0].get<float>(), j[1].get<float>(), j[2].get<float>());
}

/** @brief Reads a JSON object into a string→string map. */
void MapFromJson(
    const json &j,
    std::unordered_map<std::string, std::string, StringHash, std::equal_to<>> &map) {
    if (!j.is_object())
        return;
    for (const auto &[k, v] : j.items())
        map[k] = v.is_string() ? v.get<std::string>() : v.dump();
}

SceneMetadata SceneMetadataFromJson(const json &j) {
    SceneMetadata m;
    if (j.is_object()) {
        m.schemaVersion = j.value("schemaVersion", 1);
        m.sceneId = j.value("sceneId", "scene");
        m.sceneName = j.value("sceneName", "Scene");
        m.sourcePath = j.value("sourcePath", "");
    }
    return m;
}

SceneSettings SceneSettingsFromJson(const json &j) {
    SceneSettings s;
    if (j.is_object()) {
        if (j.contains("spawnPoint"))
            s.spawnPoint = Vec3FromJson(j["spawnPoint"], s.spawnPoint);
        if (j.contains("extraSettings"))
            MapFromJson(j["extraSettings"], s.extraSettings);
    }
    return s;
}

SceneAssetDefinition SceneAssetFromJson(const json &j) {
    SceneAssetDefinition a;
    if (j.is_object()) {
        a.id = j.value("id", "");
        a.mesh = j.value("mesh", "");
        a.renderScale = j.contains("renderScale")
                            ? Vec3FromJson(j["renderScale"], Vec3::One())
                            : Vec3::One();
        a.albedoMap = j.value("albedoMap", "");
        a.normalMap = j.value("normalMap", "");
        a.metallicRoughnessMap = j.value("metallicRoughnessMap", "");
        a.emissiveMap = j.value("emissiveMap", "");
        a.occlusionMap = j.value("occlusionMap", "");
        a.guid = j.value("guid", "");
        a.displayName = j.value("displayName", "");
    }
    return a;
}

SceneCameraProperties SceneCameraFromJson(const json &j) {
    SceneCameraProperties c;
    if (j.is_object()) {
        c.fovY = j.value("fovY", 55.0f);
        c.nearClip = j.value("nearClip", 0.1f);
        c.farClip = j.value("farClip", 200.0f);
        if (j.contains("extraProps"))
            MapFromJson(j["extraProps"], c.extraProps);
    }
    return c;
}

SceneLightProperties SceneLightFromJson(const json &j) {
    SceneLightProperties l;
    if (j.is_object()) {
        l.kind = static_cast<SceneLightKind>(j.value("kind", 0));
        l.intensity = j.value("intensity", 1.0f);
        l.color = j.contains("color") ? Vec3FromJson(j["color"], Vec3::One())
                                       : Vec3::One();
        l.radius = j.value("radius", 10.0f);
        if (j.contains("extraProps"))
            MapFromJson(j["extraProps"], l.extraProps);
    }
    return l;
}

SceneRigidBodyProperties SceneRigidBodyFromJson(const json &j) {
    SceneRigidBodyProperties r;
    if (j.is_object()) {
        r.mass = j.value("mass", 1.0f);
        r.isKinematic = j.value("isKinematic", false);
        r.useGravity = j.value("useGravity", true);
        if (j.contains("extraProps"))
            MapFromJson(j["extraProps"], r.extraProps);
    }
    return r;
}

SceneScriptProperties SceneScriptFromJson(const json &j) {
    SceneScriptProperties s;
    if (j.is_object()) {
        s.behaviorTag = j.value("behaviorTag", "");
        if (j.contains("extraProps"))
            MapFromJson(j["extraProps"], s.extraProps);
    }
    return s;
}

SceneLooseComponent SceneLooseComponentFromJson(const json &j) {
    SceneLooseComponent lc;
    if (j.is_object()) {
        lc.type = j.value("type", "");
        if (j.contains("props"))
            MapFromJson(j["props"], lc.props);
    }
    return lc;
}

ScenePrefabReference ScenePrefabReferenceFromJson(const json &j) {
    ScenePrefabReference pr;
    if (j.is_object()) {
        pr.prefabId = j.value("prefabId", "");
        pr.sourcePath = j.value("sourcePath", "");
    }
    return pr;
}

SceneNodeDefinition SceneNodeFromJson(const json &j) {
    SceneNodeDefinition n;
    if (!j.is_object())
        return n;

    n.id = j.value("id", "");
    n.kind = static_cast<SceneNodeKind>(j.value("kind", 0));
    n.position = j.contains("position") ? Vec3FromJson(j["position"]) : Vec3::Zero();
    n.scale = j.contains("scale") ? Vec3FromJson(j["scale"], Vec3::One()) : Vec3::One();
    n.yaw = j.value("yaw", 0.0f);
    n.pitch = j.value("pitch", 0.0f);
    n.roll = j.value("roll", 0.0f);
    n.assetId = j.value("assetId", "");
    if (j.contains("extraProps"))
        MapFromJson(j["extraProps"], n.extraProps);

    if (j.contains("prefabInstance"))
        n.prefabInstance = ScenePrefabReferenceFromJson(j["prefabInstance"]);
    if (j.contains("parentId") && j["parentId"].is_string())
        n.parentId = j["parentId"].get<std::string>();
    if (j.contains("camera"))
        n.camera = SceneCameraFromJson(j["camera"]);
    if (j.contains("light"))
        n.light = SceneLightFromJson(j["light"]);
    if (j.contains("rigidbody"))
        n.rigidbody = SceneRigidBodyFromJson(j["rigidbody"]);
    if (j.contains("script"))
        n.script = SceneScriptFromJson(j["script"]);
    if (j.contains("extraComponents") && j["extraComponents"].is_array()) {
        for (const auto &ec : j["extraComponents"])
            n.extraComponents.push_back(SceneLooseComponentFromJson(ec));
    }

    return n;
}

ProjectSceneReference ProjectSceneRefFromJson(const json &j) {
    ProjectSceneReference pr;
    if (j.is_object()) {
        pr.sceneId = j.value("sceneId", "");
        pr.scenePath = j.value("scenePath", "");
    }
    return pr;
}

ProjectMetadata ProjectMetadataFromJson(const json &j) {
    ProjectMetadata pm;
    if (!j.is_object())
        return pm;

    pm.schemaVersion = j.value("schemaVersion", 1);
    pm.projectId = j.value("projectId", "project");
    pm.projectName = j.value("projectName", "Project");
    pm.defaultSceneId = j.value("defaultSceneId", "");
    if (j.contains("extraSettings"))
        MapFromJson(j["extraSettings"], pm.extraSettings);
    if (j.contains("scenes") && j["scenes"].is_array()) {
        for (const auto &s : j["scenes"])
            pm.scenes.push_back(ProjectSceneRefFromJson(s));
    }
    return pm;
}

SceneDefinition SceneDefinitionFromJson(const json &j) {
    SceneDefinition sd;
    if (!j.is_object())
        return sd;

    if (j.contains("metadata"))
        sd.metadata = SceneMetadataFromJson(j["metadata"]);
    if (j.contains("settings"))
        sd.settings = SceneSettingsFromJson(j["settings"]);
    if (j.contains("assets") && j["assets"].is_array()) {
        for (const auto &a : j["assets"])
            sd.assets.push_back(SceneAssetFromJson(a));
    }
    if (j.contains("nodes") && j["nodes"].is_array()) {
        for (const auto &n : j["nodes"])
            sd.nodes.push_back(SceneNodeFromJson(n));
    }
    return sd;
}

SceneProjectModel SceneProjectModelFromJson(const json &j) {
    SceneProjectModel model;
    if (!j.is_object())
        return model;

    if (j.contains("scene"))
        model.scene = SceneDefinitionFromJson(j["scene"]);
    if (j.contains("project"))
        model.project = ProjectMetadataFromJson(j["project"]);
    return model;
}

} // namespace

// ============================================================================
//  Public API
// ============================================================================

std::vector<uint8_t>
SerializeSceneToBinary(const SceneProjectModel &model) {
    json j = ToJson(model);
    return json::to_msgpack(j);
}

SceneProjectModel
DeserializeSceneFromBinary(const std::vector<uint8_t> &data) {
    try {
        json j = json::from_msgpack(data);
        return SceneProjectModelFromJson(j);
    } catch (const json::exception &e) {
        throw SceneBinarySerializerException(
            std::string("SceneBinarySerializer: ") + e.what());
    }
}

} // namespace Horo
