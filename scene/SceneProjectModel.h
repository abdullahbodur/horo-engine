#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "math/Vec3.h"

namespace Monolith {

enum class SceneNodeKind { Panel, Prop, Light, Camera };
enum class SceneLightKind { Point, Directional };

struct SceneMetadata {
  int schemaVersion = 1;
  std::string sceneId = "scene";
  std::string sceneName = "Scene";
  std::string sourcePath;
};

struct SceneSettings {
  Vec3 spawnPoint = {2.0f, 0.9f, 3.0f};
  std::unordered_map<std::string, std::string> extraSettings;
};

struct SceneAssetDefinition {
  std::string id;
  std::string mesh;
  Vec3 renderScale = Vec3::One();
  std::string albedoMap;
};

struct SceneCameraProperties {
  float fovY = 55.0f;
  float nearClip = 0.1f;
  float farClip = 200.0f;
  std::unordered_map<std::string, std::string> extraProps;
};

struct SceneLightProperties {
  SceneLightKind kind = SceneLightKind::Point;
  float intensity = 1.0f;
  Vec3 color = Vec3::One();
  float radius = 10.0f;
  std::unordered_map<std::string, std::string> extraProps;
};

struct SceneRigidBodyProperties {
  float mass = 1.0f;
  bool isKinematic = false;
  bool useGravity = true;
  std::unordered_map<std::string, std::string> extraProps;
};

struct SceneScriptProperties {
  std::string behaviorTag;
  std::unordered_map<std::string, std::string> extraProps;
};

struct SceneLooseComponent {
  std::string type;
  std::unordered_map<std::string, std::string> props;
};

struct SceneNodeDefinition {
  std::string id;
  SceneNodeKind kind = SceneNodeKind::Panel;
  Vec3 position = Vec3::Zero();
  Vec3 scale = Vec3::One();
  float yaw = 0.0f;
  float pitch = 0.0f;
  float roll = 0.0f;
  std::string assetId;
  std::optional<std::string> parentId;
  std::unordered_map<std::string, std::string> extraProps;
  std::optional<SceneCameraProperties> camera;
  std::optional<SceneLightProperties> light;
  std::optional<SceneRigidBodyProperties> rigidbody;
  std::optional<SceneScriptProperties> script;
  std::vector<SceneLooseComponent> extraComponents;
};

struct SceneDefinition {
  SceneMetadata metadata;
  SceneSettings settings;
  std::vector<SceneAssetDefinition> assets;
  std::vector<SceneNodeDefinition> nodes;
};

struct ProjectSceneReference {
  std::string sceneId;
  std::string scenePath;
};

struct ProjectMetadata {
  int schemaVersion = 1;
  std::string projectId = "project";
  std::string projectName = "Project";
  std::string defaultSceneId;
  std::unordered_map<std::string, std::string> extraSettings;
  std::vector<ProjectSceneReference> scenes;
};

struct SceneProjectModel {
  SceneDefinition scene;
  ProjectMetadata project;
};

struct SceneProjectValidationIssue {
  enum class Severity { Error, Warning };

  Severity severity = Severity::Error;
  std::string path;
  std::string message;
};

struct SceneProjectValidationResult {
  std::vector<SceneProjectValidationIssue> issues;

  bool HasErrors() const;
  std::size_t ErrorCount() const;
  std::size_t WarningCount() const;
};

SceneProjectValidationResult ValidateSceneProjectModel(const SceneProjectModel& model);

}  // namespace Monolith
