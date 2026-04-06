#include "editor/SceneProjectBridge.h"

#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <initializer_list>

#include "editor/AssetIdentity.h"

namespace Monolith {
namespace Editor {
namespace {

float ParseFloat(const std::string& value, float fallback) {
  if (value.empty())
    return fallback;
  try {
    return std::stof(value);
  } catch (...) {
    return fallback;
  }
}

bool ParseBool(const std::string& value, bool fallback) {
  if (value == "true" || value == "1")
    return true;
  if (value == "false" || value == "0")
    return false;
  return fallback;
}

Vec3 ParseVec3Csv(const std::string& value, Vec3 fallback) {
  if (value.empty())
    return fallback;
  Vec3 parsed = fallback;
  char buffer[96] = {};
  std::snprintf(buffer, sizeof(buffer), "%s", value.c_str());
  char* cursor = buffer;
  char* end = nullptr;
  parsed.x = std::strtof(cursor, &end);
  if (end == cursor)
    return fallback;
  cursor = (*end == ',') ? end + 1 : end;
  parsed.y = std::strtof(cursor, &end);
  if (end == cursor)
    return fallback;
  cursor = (*end == ',') ? end + 1 : end;
  parsed.z = std::strtof(cursor, &end);
  if (end == cursor)
    return fallback;
  return parsed;
}

std::string FormatFloat(float value) {
  char buffer[32] = {};
  std::snprintf(buffer, sizeof(buffer), "%.4f", value);
  return buffer;
}

std::string FormatVec3Csv(const Vec3& value) {
  return FormatFloat(value.x) + "," + FormatFloat(value.y) + "," + FormatFloat(value.z);
}

SceneNodeKind ToSceneNodeKind(SceneObjectType type) {
  switch (type) {
    case SceneObjectType::Prop:
      return SceneNodeKind::Prop;
    case SceneObjectType::Light:
      return SceneNodeKind::Light;
    case SceneObjectType::Camera:
      return SceneNodeKind::Camera;
    default:
      return SceneNodeKind::Panel;
  }
}

SceneObjectType ToSceneObjectType(SceneNodeKind kind) {
  switch (kind) {
    case SceneNodeKind::Prop:
      return SceneObjectType::Prop;
    case SceneNodeKind::Light:
      return SceneObjectType::Light;
    case SceneNodeKind::Camera:
      return SceneObjectType::Camera;
    default:
      return SceneObjectType::Panel;
  }
}

const std::string* FindProp(const std::unordered_map<std::string, std::string>& props,
                            const char* key) {
  const auto it = props.find(key);
  return (it != props.end()) ? &it->second : nullptr;
}

void EraseKeys(std::unordered_map<std::string, std::string>* props,
               std::initializer_list<const char*> keys) {
  for (const char* key : keys)
    props->erase(key);
}

}  // namespace

SceneProjectModel BuildSceneProjectModel(const SceneDocument& doc) {
  SceneProjectModel model;
  model.scene.metadata.schemaVersion = doc.version;
  model.scene.metadata.sceneId = doc.sceneId.empty() ? "scene" : doc.sceneId;
  model.scene.metadata.sceneName = doc.sceneName.empty() ? "Scene" : doc.sceneName;
  model.scene.metadata.sourcePath = doc.filePath;

  model.project.schemaVersion = doc.version;
  model.project.projectId = model.scene.metadata.sceneId.empty() ? "project"
                                                                 : model.scene.metadata.sceneId + "_project";
  model.project.defaultSceneId = model.scene.metadata.sceneId;
  model.project.projectName = model.scene.metadata.sceneName.empty() ? "Project"
                                                                    : model.scene.metadata.sceneName;
  model.project.scenes.push_back({model.scene.metadata.sceneId, doc.filePath});

  model.scene.settings.extraSettings = doc.settings;
  const auto spawnIt = model.scene.settings.extraSettings.find("spawnPoint");
  if (spawnIt != model.scene.settings.extraSettings.end()) {
    model.scene.settings.spawnPoint = ParseVec3Csv(spawnIt->second, model.scene.settings.spawnPoint);
    model.scene.settings.extraSettings.erase(spawnIt);
  }

  std::vector<std::string> assetIds;
  assetIds.reserve(doc.assets.size());
  for (const auto& entry : doc.assets)
    assetIds.push_back(entry.first);
  std::sort(assetIds.begin(), assetIds.end());

  for (const auto& assetId : assetIds) {
    AssetDef asset = doc.assets.at(assetId);
    EnsureAssetIdentity(assetId, &asset);
    SceneAssetDefinition typedAsset;
    typedAsset.id = assetId;
    typedAsset.mesh = asset.mesh;
    typedAsset.renderScale = ParseVec3Csv(asset.renderScale, Vec3::One());
    typedAsset.albedoMap = asset.albedoMap;
    typedAsset.guid = asset.guid;
    typedAsset.displayName = MakeAssetDisplayName(assetId, asset);
    model.scene.assets.push_back(std::move(typedAsset));
  }

  model.scene.nodes.reserve(doc.objects.size());
  for (const auto& object : doc.objects) {
    SceneNodeDefinition node;
    node.id = object.id;
    node.kind = ToSceneNodeKind(object.type);
    node.position = object.position;
    node.scale = object.scale;
    node.yaw = object.yaw;
    node.pitch = object.pitch;
    node.roll = object.roll;
    node.assetId = object.assetId;
    node.extraProps = object.props;

    const auto parentIt = node.extraProps.find("parentId");
    if (parentIt != node.extraProps.end() && !parentIt->second.empty()) {
      node.parentId = parentIt->second;
      node.extraProps.erase(parentIt);
    }

    if (node.kind == SceneNodeKind::Camera) {
      SceneCameraProperties camera;
      if (const std::string* fov = FindProp(node.extraProps, "fov"))
        camera.fovY = ParseFloat(*fov, camera.fovY);
      if (const std::string* nearClip = FindProp(node.extraProps, "nearClip"))
        camera.nearClip = ParseFloat(*nearClip, camera.nearClip);
      if (const std::string* farClip = FindProp(node.extraProps, "farClip"))
        camera.farClip = ParseFloat(*farClip, camera.farClip);
      camera.extraProps = node.extraProps;
      EraseKeys(&camera.extraProps, {"fov", "nearClip", "farClip"});
      EraseKeys(&node.extraProps, {"fov", "nearClip", "farClip"});
      node.camera = std::move(camera);
    }

    if (node.kind == SceneNodeKind::Light) {
      SceneLightProperties light;
      if (const std::string* lightType = FindProp(node.extraProps, "lightType")) {
        light.kind = (*lightType == "directional") ? SceneLightKind::Directional : SceneLightKind::Point;
      }
      if (const std::string* intensity = FindProp(node.extraProps, "intensity"))
        light.intensity = ParseFloat(*intensity, light.intensity);
      if (const std::string* color = FindProp(node.extraProps, "color"))
        light.color = ParseVec3Csv(*color, light.color);
      if (const std::string* radius = FindProp(node.extraProps, "radius"))
        light.radius = ParseFloat(*radius, light.radius);
      light.extraProps = node.extraProps;
      EraseKeys(&light.extraProps, {"lightType", "intensity", "color", "radius"});
      EraseKeys(&node.extraProps, {"lightType", "intensity", "color", "radius"});
      node.light = std::move(light);
    }

    for (const auto& component : object.components) {
      if (component.type == "script" && !node.script.has_value()) {
        SceneScriptProperties script;
        const auto it = component.props.find("behaviorTag");
        if (it != component.props.end())
          script.behaviorTag = it->second;
        script.extraProps = component.props;
        script.extraProps.erase("behaviorTag");
        node.script = std::move(script);
        continue;
      }
      if (component.type == "rigidbody" && !node.rigidbody.has_value()) {
        SceneRigidBodyProperties rigidbody;
        const auto massIt = component.props.find("mass");
        if (massIt != component.props.end())
          rigidbody.mass = ParseFloat(massIt->second, rigidbody.mass);
        const auto kinIt = component.props.find("isKinematic");
        if (kinIt != component.props.end())
          rigidbody.isKinematic = ParseBool(kinIt->second, rigidbody.isKinematic);
        const auto gravIt = component.props.find("useGravity");
        if (gravIt != component.props.end())
          rigidbody.useGravity = ParseBool(gravIt->second, rigidbody.useGravity);
        rigidbody.extraProps = component.props;
        EraseKeys(&rigidbody.extraProps, {"mass", "isKinematic", "useGravity"});
        node.rigidbody = std::move(rigidbody);
        continue;
      }
      if (component.type == "light" && !node.light.has_value()) {
        SceneLightProperties light;
        const auto typeIt = component.props.find("lightType");
        if (typeIt != component.props.end())
          light.kind = (typeIt->second == "directional") ? SceneLightKind::Directional
                                                         : SceneLightKind::Point;
        const auto intensityIt = component.props.find("intensity");
        if (intensityIt != component.props.end())
          light.intensity = ParseFloat(intensityIt->second, light.intensity);
        const auto colorIt = component.props.find("color");
        if (colorIt != component.props.end())
          light.color = ParseVec3Csv(colorIt->second, light.color);
        const auto radiusIt = component.props.find("radius");
        if (radiusIt != component.props.end())
          light.radius = ParseFloat(radiusIt->second, light.radius);
        light.extraProps = component.props;
        EraseKeys(&light.extraProps, {"lightType", "intensity", "color", "radius"});
        node.light = std::move(light);
        continue;
      }

      node.extraComponents.push_back({component.type, component.props});
    }

    if (!node.script.has_value()) {
      const auto behaviorIt = object.props.find("behavior");
      if (behaviorIt != object.props.end() && !behaviorIt->second.empty() && behaviorIt->second != "none") {
        SceneScriptProperties script;
        script.behaviorTag = behaviorIt->second;
        node.script = std::move(script);
        node.extraProps.erase("behavior");
      }
    }

    if (!node.light.has_value()) {
      const auto legacyLightIt = object.props.find("isLight");
      if (legacyLightIt != object.props.end() && ParseBool(legacyLightIt->second, false)) {
        node.light = SceneLightProperties{};
        node.extraProps.erase("isLight");
      }
    }

    model.scene.nodes.push_back(std::move(node));
  }

  return model;
}

SceneDocument BuildSceneDocument(const SceneProjectModel& model) {
  SceneDocument doc;
  doc.version = model.scene.metadata.schemaVersion;
  doc.sceneId = model.scene.metadata.sceneId;
  doc.sceneName = model.scene.metadata.sceneName;
  doc.filePath = model.scene.metadata.sourcePath;
  doc.settings = model.scene.settings.extraSettings;
  doc.settings["spawnPoint"] = FormatVec3Csv(model.scene.settings.spawnPoint);

  for (const auto& asset : model.scene.assets) {
    doc.assets[asset.id] =
        AssetDef{asset.mesh, FormatVec3Csv(asset.renderScale), asset.albedoMap, asset.guid, asset.displayName};
  }

  doc.objects.reserve(model.scene.nodes.size());
  for (const auto& node : model.scene.nodes) {
    SceneObject object;
    object.id = node.id;
    object.type = ToSceneObjectType(node.kind);
    object.position = node.position;
    object.scale = node.scale;
    object.yaw = node.yaw;
    object.pitch = node.pitch;
    object.roll = node.roll;
    object.assetId = node.assetId;
    object.props = node.extraProps;

    if (node.parentId.has_value() && !node.parentId->empty())
      object.props["parentId"] = *node.parentId;

    if (node.kind == SceneNodeKind::Camera) {
      const SceneCameraProperties camera = node.camera.value_or(SceneCameraProperties{});
      object.props["fov"] = FormatFloat(camera.fovY);
      object.props["nearClip"] = FormatFloat(camera.nearClip);
      object.props["farClip"] = FormatFloat(camera.farClip);
      for (const auto& [key, value] : camera.extraProps)
        object.props[key] = value;
    }

    if (node.kind == SceneNodeKind::Light && node.light.has_value()) {
      object.props["lightType"] =
          (node.light->kind == SceneLightKind::Directional) ? "directional" : "point";
      object.props["intensity"] = FormatFloat(node.light->intensity);
      object.props["color"] = FormatVec3Csv(node.light->color);
      object.props["radius"] = FormatFloat(node.light->radius);
      for (const auto& [key, value] : node.light->extraProps)
        object.props[key] = value;
    }

    if (node.rigidbody.has_value()) {
      ComponentDesc rigidbody;
      rigidbody.type = "rigidbody";
      rigidbody.props["mass"] = FormatFloat(node.rigidbody->mass);
      rigidbody.props["isKinematic"] = node.rigidbody->isKinematic ? "true" : "false";
      rigidbody.props["useGravity"] = node.rigidbody->useGravity ? "true" : "false";
      for (const auto& [key, value] : node.rigidbody->extraProps)
        rigidbody.props[key] = value;
      object.components.push_back(std::move(rigidbody));
    }

    if (node.script.has_value()) {
      ComponentDesc script;
      script.type = "script";
      script.props["behaviorTag"] = node.script->behaviorTag;
      for (const auto& [key, value] : node.script->extraProps)
        script.props[key] = value;
      object.components.push_back(std::move(script));
    }

    if (node.light.has_value() && node.kind != SceneNodeKind::Light) {
      ComponentDesc light;
      light.type = "light";
      light.props["lightType"] =
          (node.light->kind == SceneLightKind::Directional) ? "directional" : "point";
      light.props["intensity"] = FormatFloat(node.light->intensity);
      light.props["color"] = FormatVec3Csv(node.light->color);
      light.props["radius"] = FormatFloat(node.light->radius);
      for (const auto& [key, value] : node.light->extraProps)
        light.props[key] = value;
      object.components.push_back(std::move(light));
    }

    for (const auto& extra : node.extraComponents)
      object.components.push_back({extra.type, extra.props});

    doc.objects.push_back(std::move(object));
  }

  return doc;
}

}  // namespace Editor
}  // namespace Monolith
