#include "mcp/McpSnapshot.h"

#include <algorithm>
#include <cctype>

namespace Monolith {
namespace Mcp {

using json = nlohmann::json;

namespace {

std::string ToLowerAscii(std::string value) {
  for (char& c : value)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return value;
}

bool ContainsCaseInsensitive(const std::string& haystack, const std::string& needle) {
  if (needle.empty())
    return true;
  return ToLowerAscii(haystack).find(ToLowerAscii(needle)) != std::string::npos;
}

json Vec3ToJson(const Vec3& value) {
  return json::array({value.x, value.y, value.z});
}

json BuildObjectSummaryJson(const McpObjectSnapshot& object) {
  json out = json::object();
  out["id"] = object.id;
  out["type"] = object.type;
  out["position"] = Vec3ToJson(object.position);
  out["scale"] = Vec3ToJson(object.scale);
  out["yaw"] = object.yaw;
  if (!object.assetId.empty())
    out["assetId"] = object.assetId;

  std::vector<std::string> componentTypes;
  for (const McpComponentSnapshot& component : object.components)
    componentTypes.push_back(component.type);
  if (!componentTypes.empty())
    out["components"] = componentTypes;

  std::vector<std::string> propKeys;
  for (const auto& entry : object.props)
    propKeys.push_back(entry.first);
  std::sort(propKeys.begin(), propKeys.end());
  if (propKeys.size() > 8)
    propKeys.resize(8);
  if (!propKeys.empty())
    out["propKeys"] = propKeys;

  return out;
}

}  // namespace

std::shared_ptr<const McpEditorSnapshot> CloneSnapshot(const McpEditorSnapshot& snapshot) {
  return std::make_shared<const McpEditorSnapshot>(snapshot);
}

const McpObjectSnapshot* FindObjectById(const McpEditorSnapshot& snapshot, const std::string& id) {
  for (const McpObjectSnapshot& object : snapshot.objects) {
    if (object.id == id)
      return &object;
  }
  return nullptr;
}

json BuildSceneSummaryJson(const McpEditorSnapshot& snapshot, size_t objectLimit) {
  json out = json::object();
  out["sceneId"] = snapshot.sceneId;
  out["sceneName"] = snapshot.sceneName;
  out["filePath"] = snapshot.sceneFilePath;
  out["editorActive"] = snapshot.editorActive;
  out["playMode"] = snapshot.playMode;
  out["dirty"] = snapshot.dirty;
  out["reloadPending"] = snapshot.reloadPending;
  out["objectCount"] = snapshot.objects.size();
  out["assetCount"] = snapshot.assets.size();
  out["selectedObjectIds"] = snapshot.selectedObjectIds;

  json objects = json::array();
  const size_t count = std::min(objectLimit, snapshot.objects.size());
  for (size_t i = 0; i < count; ++i)
    objects.push_back(BuildObjectSummaryJson(snapshot.objects[i]));
  out["objects"] = std::move(objects);
  out["moreObjects"] = snapshot.objects.size() > count ? snapshot.objects.size() - count : 0;
  return out;
}

json BuildSelectionJson(const McpEditorSnapshot& snapshot) {
  json out = json::object();
  out["selectedObjectIds"] = snapshot.selectedObjectIds;
  out["selectedAssetId"] = snapshot.selectedAssetId;
  json objects = json::array();
  for (const std::string& id : snapshot.selectedObjectIds) {
    const McpObjectSnapshot* object = FindObjectById(snapshot, id);
    if (object)
      objects.push_back(BuildObjectSummaryJson(*object));
  }
  out["objects"] = std::move(objects);
  return out;
}

json BuildAssetsJson(const McpEditorSnapshot& snapshot, size_t assetLimit) {
  json out = json::object();
  out["assetCount"] = snapshot.assets.size();

  json assets = json::array();
  const size_t count = std::min(assetLimit, snapshot.assets.size());
  for (size_t i = 0; i < count; ++i) {
    const McpAssetSnapshot& asset = snapshot.assets[i];
    assets.push_back(json{
        {"id", asset.id},
        {"mesh", asset.mesh},
        {"renderScale", asset.renderScale},
        {"albedoMap", asset.albedoMap},
    });
  }
  out["assets"] = std::move(assets);
  out["moreAssets"] = snapshot.assets.size() > count ? snapshot.assets.size() - count : 0;
  return out;
}

json BuildConsoleJson(const McpEditorSnapshot& snapshot, size_t lineLimit) {
  json out = json::object();
  out["lineCount"] = snapshot.consoleEntries.size();

  json lines = json::array();
  const size_t count = std::min(lineLimit, snapshot.consoleEntries.size());
  for (size_t i = 0; i < count; ++i) {
    const McpConsoleEntry& entry = snapshot.consoleEntries[snapshot.consoleEntries.size() - count + i];
    lines.push_back(json{
        {"time", entry.timeText},
        {"level", entry.level},
        {"message", entry.message},
    });
  }
  out["lines"] = std::move(lines);
  out["truncated"] = snapshot.consoleEntries.size() > count;
  return out;
}

json BuildObjectJson(const McpObjectSnapshot& object) {
  json props = json::object();
  for (const auto& entry : object.props)
    props[entry.first] = entry.second;

  json components = json::array();
  for (const McpComponentSnapshot& component : object.components) {
    json componentProps = json::object();
    for (const auto& entry : component.props)
      componentProps[entry.first] = entry.second;
    components.push_back(json{
        {"type", component.type},
        {"props", std::move(componentProps)},
    });
  }

  return json{
      {"id", object.id},
      {"type", object.type},
      {"position", Vec3ToJson(object.position)},
      {"scale", Vec3ToJson(object.scale)},
      {"yaw", object.yaw},
      {"pitch", object.pitch},
      {"roll", object.roll},
      {"assetId", object.assetId},
      {"props", std::move(props)},
      {"components", std::move(components)},
  };
}

json SearchSnapshot(const McpEditorSnapshot& snapshot,
                    const std::string& query,
                    size_t limit,
                    const std::string& scope) {
  const size_t safeLimit = std::max<size_t>(1, std::min<size_t>(limit, 25));
  const std::string normalizedScope = ToLowerAscii(scope.empty() ? "all" : scope);

  json objects = json::array();
  if (normalizedScope == "all" || normalizedScope == "objects") {
    for (const McpObjectSnapshot& object : snapshot.objects) {
      bool match = ContainsCaseInsensitive(object.id, query) ||
                   ContainsCaseInsensitive(object.type, query) ||
                   ContainsCaseInsensitive(object.assetId, query);
      if (!match) {
        for (const auto& entry : object.props) {
          if (ContainsCaseInsensitive(entry.first, query) ||
              ContainsCaseInsensitive(entry.second, query)) {
            match = true;
            break;
          }
        }
      }
      if (!match)
        continue;
      objects.push_back(BuildObjectSummaryJson(object));
      if (objects.size() >= safeLimit)
        break;
    }
  }

  json assets = json::array();
  if (normalizedScope == "all" || normalizedScope == "assets") {
    for (const McpAssetSnapshot& asset : snapshot.assets) {
      const bool match = ContainsCaseInsensitive(asset.id, query) ||
                         ContainsCaseInsensitive(asset.mesh, query) ||
                         ContainsCaseInsensitive(asset.albedoMap, query);
      if (!match)
        continue;
      assets.push_back(json{
          {"id", asset.id},
          {"mesh", asset.mesh},
          {"renderScale", asset.renderScale},
      });
      if (assets.size() >= safeLimit)
        break;
    }
  }

  return json{
      {"query", query},
      {"scope", normalizedScope},
      {"objects", std::move(objects)},
      {"assets", std::move(assets)},
  };
}

}  // namespace Mcp
}  // namespace Monolith
