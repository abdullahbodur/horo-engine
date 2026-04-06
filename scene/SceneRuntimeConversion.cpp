#include "scene/SceneRuntimeConversion.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <unordered_map>

namespace Monolith {
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

Vec3 ParseVec3Csv(const std::string& value, const Vec3& fallback) {
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

Vec3 MultiplyComponents(const Vec3& a, const Vec3& b) {
  return {a.x * b.x, a.y * b.y, a.z * b.z};
}

}  // namespace

RuntimeSceneBuildResult BuildRuntimeSceneDefinition(const SceneProjectModel& model) {
  RuntimeSceneBuildResult result;
  auto addIssue = [&result](RuntimeSceneBuildIssue::Severity severity,
                            std::string path,
                            std::string message) {
    result.issues.push_back({severity, std::move(path), std::move(message)});
  };

  const SceneProjectValidationResult validation = ValidateSceneProjectModel(model);
  for (const auto& issue : validation.issues) {
    result.issues.push_back(
        {issue.severity == SceneProjectValidationIssue::Severity::Error
             ? RuntimeSceneBuildIssue::Severity::Error
             : RuntimeSceneBuildIssue::Severity::Warning,
         issue.path,
         issue.message});
  }

  result.definition.spawnPoint = model.scene.settings.spawnPoint;
  const auto gravityIt = model.scene.settings.extraSettings.find("gravity");

  RuntimeSceneRoom room;
  room.id = model.scene.metadata.sceneId.empty() ? "scene" : model.scene.metadata.sceneId;
  if (gravityIt != model.scene.settings.extraSettings.end()) {
    room.gravity = ParseVec3Csv(gravityIt->second, room.gravity);
  }

  std::unordered_map<std::string, const SceneAssetDefinition*> assetsById;
  for (const auto& asset : model.scene.assets)
    assetsById[asset.id] = &asset;

  for (std::size_t i = 0; i < model.scene.nodes.size(); ++i) {
    const SceneNodeDefinition& node = model.scene.nodes[i];
    const std::string path = "scene.nodes[" + std::to_string(i) + "]";

    switch (node.kind) {
      case SceneNodeKind::Panel: {
        room.panels.push_back({node.position, node.scale});
        break;
      }
      case SceneNodeKind::Prop: {
        RuntimeSceneProp prop;
        prop.id = node.id;
        prop.position = node.position;
        prop.yaw = node.yaw;
        prop.pitch = node.pitch;
        prop.roll = node.roll;
        prop.scale = node.scale;

        if (!node.assetId.empty()) {
          const auto assetIt = assetsById.find(node.assetId);
          if (assetIt == assetsById.end()) {
            addIssue(RuntimeSceneBuildIssue::Severity::Error,
                     path + ".assetId",
                     "assetId does not resolve to a declared scene asset.");
            prop.meshTag = "box";
          } else {
            prop.meshTag = assetIt->second->mesh;
            prop.albedoMap = assetIt->second->albedoMap;
            prop.scale = MultiplyComponents(node.scale, assetIt->second->renderScale);
          }
        } else {
          const auto meshIt = node.extraProps.find("mesh");
          prop.meshTag = (meshIt != node.extraProps.end()) ? meshIt->second : std::string();
          const auto renderScaleIt = node.extraProps.find("renderScale");
          if (renderScaleIt != node.extraProps.end()) {
            prop.scale = MultiplyComponents(node.scale, ParseVec3Csv(renderScaleIt->second, Vec3::One()));
          }
          const auto albedoIt = node.extraProps.find("albedoMap");
          if (albedoIt != node.extraProps.end())
            prop.albedoMap = albedoIt->second;
        }

        prop.isLight = node.light.has_value();
        if (node.script.has_value())
          prop.scriptTag = node.script->behaviorTag;

        room.props.push_back(std::move(prop));
        break;
      }
      case SceneNodeKind::Light: {
        Light light;
        if (node.light.has_value()) {
          light.type = (node.light->kind == SceneLightKind::Directional) ? Light::Type::Directional
                                                                         : Light::Type::Point;
          light.position = node.position;
          light.color = node.light->color;
          light.intensity = node.light->intensity;
          light.radius = node.light->radius;
        } else {
          addIssue(RuntimeSceneBuildIssue::Severity::Warning,
                   path + ".light",
                   "Light node is missing typed light properties; using defaults.");
          light.position = node.position;
        }
        result.definition.lights.push_back(std::move(light));
        break;
      }
      case SceneNodeKind::Camera: {
        if (!result.definition.sceneCamera.has_value()) {
          RuntimeSceneCamera camera;
          camera.position = node.position;
          camera.yaw = node.yaw;
          camera.pitch = std::clamp(node.pitch, -89.0f, 89.0f);
          if (node.camera.has_value()) {
            camera.fovY = node.camera->fovY;
            camera.nearClip = std::max(0.01f, node.camera->nearClip);
            camera.farClip = std::max(camera.nearClip + 0.01f, node.camera->farClip);
          }
          result.definition.sceneCamera = std::move(camera);
        }
        break;
      }
    }
  }

  result.definition.rooms.push_back(std::move(room));
  return result;
}

}  // namespace Monolith
