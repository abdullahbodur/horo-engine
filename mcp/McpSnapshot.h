#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

#include "math/Vec3.h"

namespace Monolith {
namespace Mcp {

struct McpComponentSnapshot {
  std::string type;
  std::unordered_map<std::string, std::string> props;
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
  std::unordered_map<std::string, std::string> props;
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
};

std::shared_ptr<const McpEditorSnapshot> CloneSnapshot(const McpEditorSnapshot& snapshot);
const McpObjectSnapshot* FindObjectById(const McpEditorSnapshot& snapshot, const std::string& id);
nlohmann::json BuildSceneSummaryJson(const McpEditorSnapshot& snapshot, size_t objectLimit = 12);
nlohmann::json BuildSelectionJson(const McpEditorSnapshot& snapshot);
nlohmann::json BuildAssetsJson(const McpEditorSnapshot& snapshot, size_t assetLimit = 12);
nlohmann::json BuildConsoleJson(const McpEditorSnapshot& snapshot, size_t lineLimit = 20);
nlohmann::json BuildObjectJson(const McpObjectSnapshot& object);
nlohmann::json SearchSnapshot(const McpEditorSnapshot& snapshot,
                              const std::string& query,
                              size_t limit,
                              const std::string& scope);

}  // namespace Mcp
}  // namespace Monolith
