#include "editor/SceneSerializer.h"

#include <algorithm>
#include <fstream>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <vector>

using json = nlohmann::json;

namespace Monolith {
namespace Editor {

static SceneObjectType TypeFromString(const std::string& s) {
  if (s == "Prop")
    return SceneObjectType::Prop;
  if (s == "Light")
    return SceneObjectType::Light;
  return SceneObjectType::Panel;
}

static const char* TypeToString(SceneObjectType t) {
  switch (t) {
    case SceneObjectType::Prop:
      return "Prop";
    case SceneObjectType::Light:
      return "Light";
    default:
      return "Panel";
  }
}

SceneDocument SceneSerializer::LoadFromFile(const std::string& path) {
  std::ifstream f(path);
  if (!f.is_open())
    throw std::runtime_error("SceneSerializer: cannot open '" + path + "'");

  json j;
  try {
    f >> j;
  } catch (const json::exception& e) {
    throw std::runtime_error("SceneSerializer: JSON parse error in '" + path + "': " + e.what());
  }

  SceneDocument doc;
  doc.filePath = path;
  doc.version = j.value("version", 1);
  doc.sceneId = j.value("sceneId", "dungeon");

  if (j.contains("settings") && j["settings"].is_object()) {
    for (auto& [k, v] : j["settings"].items()) {
      if (v.is_string())
        doc.settings[k] = v.get<std::string>();
      else
        doc.settings[k] = v.dump();
    }
  }

  // ---- Asset registry ----
  if (j.contains("assets") && j["assets"].is_object()) {
    for (auto& [id, def] : j["assets"].items()) {
      AssetDef ad;
      ad.mesh = def.value("mesh", "");
      ad.renderScale = def.value("renderScale", "1.0000,1.0000,1.0000");
      doc.assets[id] = std::move(ad);
    }
  }

  // ---- Objects ----
  if (!j.contains("objects") || !j["objects"].is_array())
    throw std::runtime_error("SceneSerializer: missing or invalid 'objects' array in '" + path + "'");
  for (auto& obj : j["objects"]) {
    SceneObject so;
    so.id = obj.value("id", "");
    so.type = TypeFromString(obj.value("type", "Panel"));
    so.yaw = obj.value("yaw", 0.0f);
    so.assetId = obj.value("asset", "");

    auto pos = obj.value("position", json::array({0.f, 0.f, 0.f}));
    auto scl = obj.value("scale", json::array({1.f, 1.f, 1.f}));
    so.position = {pos[0].get<float>(), pos[1].get<float>(), pos[2].get<float>()};
    so.scale = {scl[0].get<float>(), scl[1].get<float>(), scl[2].get<float>()};

    if (obj.contains("props") && obj["props"].is_object())
      for (auto& [k, v] : obj["props"].items())
        so.props[k] = v.get<std::string>();

    doc.objects.push_back(std::move(so));
  }

  return doc;
}

void SceneSerializer::SaveToFile(const SceneDocument& doc, const std::string& path) {
  json j;
  j["version"] = doc.version;
  j["sceneId"] = doc.sceneId.empty() ? "dungeon" : doc.sceneId;

  // ---- Scene settings (stable key order) ----
  json settings = json::object();
  {
    std::vector<std::string> keys;
    keys.reserve(doc.settings.size());
    for (const auto& kv : doc.settings)
      keys.push_back(kv.first);
    std::sort(keys.begin(), keys.end());
    for (const auto& k : keys)
      settings[k] = doc.settings.at(k);
  }
  j["settings"] = settings;

  // ---- Asset registry ----
  json assets = json::object();
  std::vector<std::string> assetIds;
  assetIds.reserve(doc.assets.size());
  for (const auto& kv : doc.assets)
    assetIds.push_back(kv.first);
  std::sort(assetIds.begin(), assetIds.end());

  for (const auto& id : assetIds) {
    const auto& def = doc.assets.at(id);
    json d;
    d["mesh"] = def.mesh;
    d["renderScale"] = def.renderScale;
    assets[id] = d;
  }
  j["assets"] = assets;

  // ---- Objects ----
  j["objects"] = json::array();
  for (auto& so : doc.objects) {
    json obj;
    obj["id"] = so.id;
    obj["type"] = TypeToString(so.type);
    obj["position"] = {so.position.x, so.position.y, so.position.z};
    obj["scale"] = {so.scale.x, so.scale.y, so.scale.z};
    obj["yaw"] = so.yaw;

    if (!so.assetId.empty()) {
      // Asset reference — mesh/renderScale live in the assets block
      obj["asset"] = so.assetId;
    } else {
      // Inline props — skip runtime-only keys (e.g. _eid)
      json props = json::object();
      std::vector<std::string> propKeys;
      propKeys.reserve(so.props.size());
      for (const auto& kv : so.props)
        propKeys.push_back(kv.first);
      std::sort(propKeys.begin(), propKeys.end());

      for (const auto& k : propKeys) {
        if (k == "_eid")
          continue;  // runtime handle, never persisted
        props[k] = so.props.at(k);
      }
      obj["props"] = props;
    }

    j["objects"].push_back(obj);
  }

  std::ofstream f(path);
  if (!f.is_open())
    throw std::runtime_error("SceneSerializer: cannot write '" + path + "'");
  f << j.dump(2);
}

}  // namespace Editor
}  // namespace Monolith
