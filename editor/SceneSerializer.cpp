#include "editor/SceneSerializer.h"

#include <algorithm>
#include <filesystem>
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
  if (s == "camera")
    return SceneObjectType::Camera;
  return SceneObjectType::Panel;
}

static const char* TypeToString(SceneObjectType t) {
  switch (t) {
    case SceneObjectType::Prop:
      return "Prop";
    case SceneObjectType::Light:
      return "Light";
    case SceneObjectType::Camera:
      return "camera";
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
  doc.sceneId   = j.value("sceneId",   "scene");  // "world" accepted for legacy files
  if (doc.sceneId == "world") doc.sceneId = "scene";
  doc.sceneName = j.value("sceneName", "Scene");

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
      ad.albedoMap = def.value("albedoMap", "");
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
    so.pitch = obj.value("pitch", 0.0f);
    so.roll = obj.value("roll", 0.0f);
    so.assetId = obj.value("asset", "");

    auto pos = obj.value("position", json::array({0.f, 0.f, 0.f}));
    auto scl = obj.value("scale", json::array({1.f, 1.f, 1.f}));
    so.position = {pos[0].get<float>(), pos[1].get<float>(), pos[2].get<float>()};
    so.scale = {scl[0].get<float>(), scl[1].get<float>(), scl[2].get<float>()};

    if (obj.contains("props") && obj["props"].is_object())
      for (auto& [k, v] : obj["props"].items())
        so.props[k] = v.get<std::string>();

    // ---- Components ----
    if (obj.contains("components") && obj["components"].is_array()) {
      for (auto& comp : obj["components"]) {
        ComponentDesc cd;
        cd.type = comp.value("type", "");
        if (comp.contains("props") && comp["props"].is_object())
          for (auto& [k, v] : comp["props"].items())
            cd.props[k] = v.get<std::string>();
        if (!cd.type.empty())
          so.components.push_back(std::move(cd));
      }
    }

    // ---- Legacy compatibility: isLight prop → optional light component ----
    // Accept old scene files at load time, but never persist isLight again.
    auto isLightIt = so.props.find("isLight");
    if (isLightIt != so.props.end()) {
      const bool wantsLight = (isLightIt->second == "true" || isLightIt->second == "1");
      bool hasLightComponent = false;
      for (const auto& c : so.components) {
        if (c.type == "light") {
          hasLightComponent = true;
          break;
        }
      }

      if (wantsLight && !hasLightComponent) {
        ComponentDesc light;
        light.type = "light";
        light.props["intensity"] = "1";
        light.props["color"] = "1,1,1";
        light.props["radius"] = "5";
        so.components.push_back(std::move(light));
      }
      so.props.erase(isLightIt);
    }

    doc.objects.push_back(std::move(so));
  }

  return doc;
}

void SceneSerializer::SaveToFile(const SceneDocument& doc, const std::string& path) {
  json j;
  j["version"] = doc.version;
  j["sceneId"]   = doc.sceneId.empty()   ? "scene" : doc.sceneId;
  j["sceneName"] = doc.sceneName.empty() ? "Scene" : doc.sceneName;

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
    if (!def.albedoMap.empty())
      d["albedoMap"] = def.albedoMap;
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
    if (so.pitch != 0.0f || so.type == SceneObjectType::Camera)
      obj["pitch"] = so.pitch;
    if (so.roll != 0.0f)
      obj["roll"] = so.roll;

    if (!so.assetId.empty()) {
      // Asset reference — mesh/renderScale live in the assets block.
      obj["asset"] = so.assetId;
    }

    // Persist type-specific and hierarchy props for both asset-backed and inline objects.
    // Runtime-only keys (e.g. _eid) are always skipped.
    json props = json::object();
    std::vector<std::string> propKeys;
    propKeys.reserve(so.props.size());
    for (const auto& kv : so.props)
      propKeys.push_back(kv.first);
    std::sort(propKeys.begin(), propKeys.end());

    for (const auto& k : propKeys) {
      if (k == "_eid")
        continue;  // runtime handle, never persisted
      if (!so.assetId.empty() && (k == "mesh" || k == "renderScale"))
        continue;  // asset-backed objects resolve these from the asset registry
      props[k] = so.props.at(k);
    }
    if (!props.empty())
      obj["props"] = std::move(props);

    // ---- Components ----
    if (!so.components.empty()) {
      json comps = json::array();
      for (const auto& cd : so.components) {
        json c;
        c["type"] = cd.type;
        json cprops = json::object();
        std::vector<std::string> cpropKeys;
        cpropKeys.reserve(cd.props.size());
        for (const auto& kv : cd.props)
          cpropKeys.push_back(kv.first);
        std::sort(cpropKeys.begin(), cpropKeys.end());
        for (const auto& k : cpropKeys)
          cprops[k] = cd.props.at(k);
        c["props"] = cprops;
        comps.push_back(std::move(c));
      }
      obj["components"] = std::move(comps);
    }

    j["objects"].push_back(obj);
  }

  namespace fs = std::filesystem;
  std::error_code ec;
  fs::path out(path);
  fs::create_directories(out.parent_path(), ec);

  std::ofstream f(path);
  if (!f.is_open())
    throw std::runtime_error("SceneSerializer: cannot write '" + path + "'");
  f << j.dump(2);
}

}  // namespace Editor
}  // namespace Monolith
