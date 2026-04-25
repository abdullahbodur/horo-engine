#pragma once

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/StringHash.h"
#include "math/Vec3.h"

namespace Monolith::Mcp {
struct McpComponentSnapshot {
  std::string type;
  std::unordered_map<std::string, std::string, StringHash, std::equal_to<>>
      props;
};

struct McpObjectSnapshot {
  std::string id;
  std::string type;
  Vec3 position = Vec3::Zero();
  Vec3 scale = Vec3::One();
  float yaw = 0.0f;
  float pitch = 0.0f;
  float roll = 0.0f;
  std::string assetId;
  std::unordered_map<std::string, std::string, StringHash, std::equal_to<>>
      props;
  std::vector<McpComponentSnapshot> components;
};

struct McpAssetSnapshot {
  std::string id;
  std::string mesh;
  std::string renderScale;
  std::string albedoMap;
};

struct McpConsoleEntry {
  std::string timeText;
  std::string level;
  std::string message;
};

struct McpBuildIssueSnapshot {
  std::string stage;
  std::string severity;
  std::string path;
  std::string message;
};

struct McpBuildSnapshot {
  bool available = false;
  std::string source = "scene_project_runtime";
  std::string status = "unavailable";
  size_t assetCount = 0;
  size_t nodeCount = 0;
  size_t sceneValidationErrors = 0;
  size_t sceneValidationWarnings = 0;
  size_t runtimeBuildErrors = 0;
  size_t runtimeBuildWarnings = 0;
  size_t roomCount = 0;
  size_t panelCount = 0;
  size_t propCount = 0;
  size_t lightCount = 0;
  bool hasSceneCamera = false;
  std::vector<McpBuildIssueSnapshot> issues;
};

struct McpSchemaFieldSnapshot {
  std::string key;
  std::string label;
  std::string description;
  std::string widget = "string";
  bool hasDefault = false;
  bool required = false;
  bool allowEmpty = true;
  bool allowCustomValue = false;
  bool hasMin = false;
  bool hasMax = false;
  float minVal = 0.0f;
  float maxVal = 1.0f;
  std::vector<std::string> options;
  std::string defaultValue;
};

struct McpSchemaEntrySnapshot {
  std::string kind;
  std::string name;
  std::string label;
  std::vector<std::string> appliesTo;
  std::vector<McpSchemaFieldSnapshot> fields;
};

struct McpSchemaCatalogSnapshot {
  std::vector<McpSchemaEntrySnapshot> objectTypes;
  std::vector<McpSchemaEntrySnapshot> components;
};

struct McpEditorSnapshot {
  bool editorActive = false;
  bool playMode = false;
  bool dirty = false;
  bool reloadPending = false;
  std::string sceneId;
  std::string sceneName;
  std::string sceneFilePath;
  std::string selectedAssetId;
  std::vector<std::string> selectedObjectIds;
  std::vector<McpObjectSnapshot> objects;
  std::vector<McpAssetSnapshot> assets;
  std::vector<McpConsoleEntry> consoleEntries;
  McpBuildSnapshot build;
  McpSchemaCatalogSnapshot schema;
};

std::shared_ptr<const McpEditorSnapshot>
CloneSnapshot(const McpEditorSnapshot &snapshot);

const McpObjectSnapshot *FindObjectById(const McpEditorSnapshot &snapshot,
                                        std::string_view id);

const McpAssetSnapshot *FindAssetById(const McpEditorSnapshot &snapshot,
                                      std::string_view id);

nlohmann::json BuildSceneSummaryJson(const McpEditorSnapshot &snapshot,
                                     size_t objectLimit = 12);

nlohmann::json BuildSceneStatusJson(const McpEditorSnapshot &snapshot);

nlohmann::json BuildSelectionJson(const McpEditorSnapshot &snapshot);

nlohmann::json BuildAssetsJson(const McpEditorSnapshot &snapshot,
                               size_t assetLimit = 12);

nlohmann::json BuildAssetsSelectionJson(const McpEditorSnapshot &snapshot);

nlohmann::json BuildAssetsCatalogJson(const McpEditorSnapshot &snapshot,
                                      size_t assetLimit = 12,
                                      const std::string &query = {},
                                      size_t offset = 0);

nlohmann::json BuildConsoleJson(const McpEditorSnapshot &snapshot,
                                size_t lineLimit = 20, size_t offset = 0);

nlohmann::json BuildConsoleSummaryJson(const McpEditorSnapshot &snapshot,
                                       size_t lineLimit = 5);

nlohmann::json BuildBuildStatusJson(const McpEditorSnapshot &snapshot,
                                    size_t issueLimit = 5);

nlohmann::json BuildSchemaCatalogJson(const McpEditorSnapshot &snapshot,
                                      const std::string &kindFilter = {});

nlohmann::json BuildSchemaJson(const McpEditorSnapshot &snapshot,
                               const std::string &name,
                               const std::string &kindFilter = {});

nlohmann::json BuildObjectListJson(const McpEditorSnapshot &snapshot,
                                   size_t objectLimit = 12,
                                   const std::string &typeFilter = {},
                                   const std::string &query = {},
                                   bool selectedOnly = false,
                                   size_t offset = 0);

nlohmann::json BuildHierarchyJson(const McpEditorSnapshot &snapshot,
                                  size_t objectLimit = 32, size_t offset = 0);

nlohmann::json BuildObjectJson(const McpObjectSnapshot &object);

nlohmann::json BuildObjectEdgesJson(const McpObjectSnapshot &object);

nlohmann::json BuildAssetJson(const McpAssetSnapshot &asset);

nlohmann::json SearchSnapshot(const McpEditorSnapshot &snapshot,
                              const std::string &query, size_t limit,
                              const std::string &scope);

nlohmann::json SearchAssetsSnapshot(const McpEditorSnapshot &snapshot,
                                    const std::string &query, size_t limit);

nlohmann::json SearchConsoleSnapshot(const McpEditorSnapshot &snapshot,
                                     const std::string &query, size_t limit);
} // namespace Monolith::Mcp
