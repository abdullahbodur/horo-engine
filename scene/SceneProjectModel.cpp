#include "scene/SceneProjectModel.h"

#include <algorithm>
#include <format>
#include <unordered_set>

namespace Monolith {
namespace {
void ValidateSceneAssets(
    const SceneProjectModel &model,
    std::unordered_set<std::string, StringHash, std::equal_to<>> &assetIds,
    SceneProjectValidationResult &result) {
  auto addIssue = [&result](SceneProjectValidationIssue::Severity severity,
                            std::string path, std::string message) {
    result.issues.emplace_back(severity, std::move(path), std::move(message));
  };
  std::unordered_set<std::string, StringHash, std::equal_to<>> assetGuids;
  for (std::size_t i = 0; i < model.scene.assets.size(); ++i) {
    const auto &asset = model.scene.assets[i];
    const std::string path = std::format("scene.assets[{}]", i);
    if (asset.id.empty()) {
      addIssue(SceneProjectValidationIssue::Severity::Error, path + ".id",
               "Asset id must not be empty.");
      continue;
    }
    if (!assetIds.insert(asset.id).second) {
      addIssue(SceneProjectValidationIssue::Severity::Error, path + ".id",
               "Asset id must be unique.");
    }
    if (asset.guid.empty()) {
      addIssue(SceneProjectValidationIssue::Severity::Error, path + ".guid",
               "Asset guid must not be empty.");
    } else if (!assetGuids.insert(asset.guid).second) {
      addIssue(SceneProjectValidationIssue::Severity::Error, path + ".guid",
               "Asset guid must be unique.");
    }
  }
}

void ValidateSceneNodeIds(
    const SceneProjectModel &model,
    std::unordered_set<std::string, StringHash, std::equal_to<>> &nodeIds,
    SceneProjectValidationResult &result) {
  auto addIssue = [&result](SceneProjectValidationIssue::Severity severity,
                            std::string path, std::string message) {
    result.issues.emplace_back(severity, std::move(path), std::move(message));
  };
  for (std::size_t i = 0; i < model.scene.nodes.size(); ++i) {
    const auto &node = model.scene.nodes[i];
    const std::string path = std::format("scene.nodes[{}]", i);
    if (node.id.empty()) {
      addIssue(SceneProjectValidationIssue::Severity::Error, path + ".id",
               "Node id must not be empty.");
      continue;
    }
    if (!nodeIds.insert(node.id).second) {
      addIssue(SceneProjectValidationIssue::Severity::Error, path + ".id",
               "Node id must be unique.");
    }
  }
}

void ValidateSceneNodeReferences(
    const SceneProjectModel &model,
    const std::unordered_set<std::string, StringHash, std::equal_to<>> &nodeIds,
    const std::unordered_set<std::string, StringHash, std::equal_to<>>
        &assetIds,
    SceneProjectValidationResult &result) {
  auto addIssue = [&result](SceneProjectValidationIssue::Severity severity,
                            std::string path, std::string message) {
    result.issues.emplace_back(severity, std::move(path), std::move(message));
  };
  for (std::size_t i = 0; i < model.scene.nodes.size(); ++i) {
    const auto &node = model.scene.nodes[i];
    const std::string path = std::format("scene.nodes[{}]", i);
    if (!node.assetId.empty() && !assetIds.contains(node.assetId)) {
      addIssue(SceneProjectValidationIssue::Severity::Error, path + ".assetId",
               "assetId must resolve to a declared scene asset.");
    }
    if (node.prefabInstance.has_value()) {
      if (node.prefabInstance->prefabId.empty()) {
        addIssue(
            SceneProjectValidationIssue::Severity::Error,
            path + ".prefabInstance.prefabId",
            "prefabId must not be empty when a prefab instance is declared.");
      }
      if (node.prefabInstance->sourcePath.empty()) {
        addIssue(
            SceneProjectValidationIssue::Severity::Error,
            path + ".prefabInstance.sourcePath",
            "sourcePath must not be empty when a prefab instance is declared.");
      }
    }
    if (node.parentId.has_value() && !node.parentId->empty() &&
        !nodeIds.contains(*node.parentId)) {
      addIssue(SceneProjectValidationIssue::Severity::Warning,
               path + ".parentId",
               "parentId does not resolve to a declared scene node.");
    }
    if (node.kind == SceneNodeKind::Camera && !node.camera.has_value()) {
      addIssue(SceneProjectValidationIssue::Severity::Warning, path + ".camera",
               "Camera nodes should provide typed camera properties.");
    }
  }
}

void ValidateProjectScenes(const SceneProjectModel &model,
                           SceneProjectValidationResult &result) {
  auto addIssue = [&result](SceneProjectValidationIssue::Severity severity,
                            std::string path, std::string message) {
    result.issues.emplace_back(severity, std::move(path), std::move(message));
  };
  std::unordered_set<std::string, StringHash, std::equal_to<>> projectSceneIds;
  for (std::size_t i = 0; i < model.project.scenes.size(); ++i) {
    const auto &sceneRef = model.project.scenes[i];
    const std::string path = std::format("project.scenes[{}]", i);
    if (sceneRef.sceneId.empty()) {
      addIssue(SceneProjectValidationIssue::Severity::Error, path + ".sceneId",
               "Project sceneId must not be empty.");
      continue;
    }
    if (!projectSceneIds.insert(sceneRef.sceneId).second) {
      addIssue(SceneProjectValidationIssue::Severity::Error, path + ".sceneId",
               "Project sceneId must be unique.");
    }
  }
  if (!model.project.defaultSceneId.empty() &&
      !projectSceneIds.contains(model.project.defaultSceneId)) {
    addIssue(SceneProjectValidationIssue::Severity::Error,
             "project.defaultSceneId",
             "defaultSceneId must match a declared project scene.");
  }
}
} // namespace

bool SceneProjectValidationResult::HasErrors() const {
  return std::ranges::any_of(issues, [](const auto &issue) {
    return issue.severity == SceneProjectValidationIssue::Severity::Error;
  });
}

std::size_t SceneProjectValidationResult::ErrorCount() const {
  std::size_t count = 0;
  for (const auto &issue : issues) {
    if (issue.severity == SceneProjectValidationIssue::Severity::Error)
      ++count;
  }
  return count;
}

std::size_t SceneProjectValidationResult::WarningCount() const {
  std::size_t count = 0;
  for (const auto &issue : issues) {
    if (issue.severity == SceneProjectValidationIssue::Severity::Warning)
      ++count;
  }
  return count;
}

SceneProjectValidationResult
ValidateSceneProjectModel(const SceneProjectModel &model) {
  SceneProjectValidationResult result;
  auto addIssue = [&result](SceneProjectValidationIssue::Severity severity,
                            std::string path, std::string message) {
    result.issues.emplace_back(severity, std::move(path), std::move(message));
  };

  if (model.scene.metadata.schemaVersion < 1) {
    addIssue(SceneProjectValidationIssue::Severity::Error,
             "scene.metadata.schemaVersion",
             "Scene schemaVersion must be >= 1.");
  }
  if (model.project.schemaVersion < 1) {
    addIssue(SceneProjectValidationIssue::Severity::Error,
             "project.schemaVersion", "Project schemaVersion must be >= 1.");
  }
  if (model.scene.metadata.sceneId.empty()) {
    addIssue(SceneProjectValidationIssue::Severity::Error,
             "scene.metadata.sceneId", "sceneId must not be empty.");
  }
  if (model.scene.metadata.sceneName.empty()) {
    addIssue(SceneProjectValidationIssue::Severity::Error,
             "scene.metadata.sceneName", "sceneName must not be empty.");
  }
  if (model.project.projectId.empty()) {
    addIssue(SceneProjectValidationIssue::Severity::Error, "project.projectId",
             "projectId must not be empty.");
  }

  std::unordered_set<std::string, StringHash, std::equal_to<>> assetIds;
  ValidateSceneAssets(model, assetIds, result);

  std::unordered_set<std::string, StringHash, std::equal_to<>> nodeIds;
  ValidateSceneNodeIds(model, nodeIds, result);

  ValidateSceneNodeReferences(model, nodeIds, assetIds, result);

  ValidateProjectScenes(model, result);

  return result;
}
} // namespace Monolith
