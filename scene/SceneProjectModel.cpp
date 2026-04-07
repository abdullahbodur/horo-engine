#include "scene/SceneProjectModel.h"

#include <unordered_set>

namespace Monolith {

bool SceneProjectValidationResult::HasErrors() const {
  for (const auto& issue : issues) {
    if (issue.severity == SceneProjectValidationIssue::Severity::Error)
      return true;
  }
  return false;
}

std::size_t SceneProjectValidationResult::ErrorCount() const {
  std::size_t count = 0;
  for (const auto& issue : issues) {
    if (issue.severity == SceneProjectValidationIssue::Severity::Error)
      ++count;
  }
  return count;
}

std::size_t SceneProjectValidationResult::WarningCount() const {
  std::size_t count = 0;
  for (const auto& issue : issues) {
    if (issue.severity == SceneProjectValidationIssue::Severity::Warning)
      ++count;
  }
  return count;
}

SceneProjectValidationResult ValidateSceneProjectModel(const SceneProjectModel& model) {
  SceneProjectValidationResult result;
  auto addIssue = [&result](SceneProjectValidationIssue::Severity severity,
                            std::string path,
                            std::string message) {
    result.issues.push_back({severity, std::move(path), std::move(message)});
  };

  if (model.scene.metadata.schemaVersion < 1) {
    addIssue(SceneProjectValidationIssue::Severity::Error,
             "scene.metadata.schemaVersion",
             "Scene schemaVersion must be >= 1.");
  }
  if (model.project.schemaVersion < 1) {
    addIssue(SceneProjectValidationIssue::Severity::Error,
             "project.schemaVersion",
             "Project schemaVersion must be >= 1.");
  }
  if (model.scene.metadata.sceneId.empty()) {
    addIssue(SceneProjectValidationIssue::Severity::Error,
             "scene.metadata.sceneId",
             "sceneId must not be empty.");
  }
  if (model.scene.metadata.sceneName.empty()) {
    addIssue(SceneProjectValidationIssue::Severity::Error,
             "scene.metadata.sceneName",
             "sceneName must not be empty.");
  }
  if (model.project.projectId.empty()) {
    addIssue(SceneProjectValidationIssue::Severity::Error,
             "project.projectId",
             "projectId must not be empty.");
  }

  std::unordered_set<std::string> assetIds;
  std::unordered_set<std::string> assetGuids;
  for (std::size_t i = 0; i < model.scene.assets.size(); ++i) {
    const auto& asset = model.scene.assets[i];
    const std::string path = "scene.assets[" + std::to_string(i) + "]";
    if (asset.id.empty()) {
      addIssue(SceneProjectValidationIssue::Severity::Error,
               path + ".id",
               "Asset id must not be empty.");
      continue;
    }
    if (!assetIds.insert(asset.id).second) {
      addIssue(SceneProjectValidationIssue::Severity::Error,
               path + ".id",
               "Asset id must be unique.");
    }
    if (asset.guid.empty()) {
      addIssue(SceneProjectValidationIssue::Severity::Error,
               path + ".guid",
               "Asset guid must not be empty.");
    } else if (!assetGuids.insert(asset.guid).second) {
      addIssue(SceneProjectValidationIssue::Severity::Error,
               path + ".guid",
               "Asset guid must be unique.");
    }
  }

  std::unordered_set<std::string> nodeIds;
  for (std::size_t i = 0; i < model.scene.nodes.size(); ++i) {
    const auto& node = model.scene.nodes[i];
    const std::string path = "scene.nodes[" + std::to_string(i) + "]";
    if (node.id.empty()) {
      addIssue(SceneProjectValidationIssue::Severity::Error,
               path + ".id",
               "Node id must not be empty.");
      continue;
    }
    if (!nodeIds.insert(node.id).second) {
      addIssue(SceneProjectValidationIssue::Severity::Error,
               path + ".id",
               "Node id must be unique.");
    }
  }

  for (std::size_t i = 0; i < model.scene.nodes.size(); ++i) {
    const auto& node = model.scene.nodes[i];
    const std::string path = "scene.nodes[" + std::to_string(i) + "]";
    if (!node.assetId.empty() && assetIds.count(node.assetId) == 0) {
      addIssue(SceneProjectValidationIssue::Severity::Error,
               path + ".assetId",
               "assetId must resolve to a declared scene asset.");
    }
    if (node.parentId.has_value() && !node.parentId->empty() && nodeIds.count(*node.parentId) == 0) {
      addIssue(SceneProjectValidationIssue::Severity::Warning,
               path + ".parentId",
               "parentId does not resolve to a declared scene node.");
    }
    if (node.kind == SceneNodeKind::Camera && !node.camera.has_value()) {
      addIssue(SceneProjectValidationIssue::Severity::Warning,
               path + ".camera",
               "Camera nodes should provide typed camera properties.");
    }
  }

  std::unordered_set<std::string> projectSceneIds;
  for (std::size_t i = 0; i < model.project.scenes.size(); ++i) {
    const auto& sceneRef = model.project.scenes[i];
    const std::string path = "project.scenes[" + std::to_string(i) + "]";
    if (sceneRef.sceneId.empty()) {
      addIssue(SceneProjectValidationIssue::Severity::Error,
               path + ".sceneId",
               "Project sceneId must not be empty.");
      continue;
    }
    if (!projectSceneIds.insert(sceneRef.sceneId).second) {
      addIssue(SceneProjectValidationIssue::Severity::Error,
               path + ".sceneId",
               "Project sceneId must be unique.");
    }
  }

  if (!model.project.defaultSceneId.empty() && projectSceneIds.count(model.project.defaultSceneId) == 0) {
    addIssue(SceneProjectValidationIssue::Severity::Error,
             "project.defaultSceneId",
             "defaultSceneId must match a declared project scene.");
  }

  return result;
}

}  // namespace Monolith
