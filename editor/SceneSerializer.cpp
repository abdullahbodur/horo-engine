#include "editor/SceneSerializer.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <vector>

#include "editor/AssetIdentity.h"

using json = nlohmann::json;

namespace Monolith::Editor {
    static SceneObjectType TypeFromString(std::string_view s) {
        using enum SceneObjectType;
        if (s == "Prop")
            return Prop;
        if (s == "Light")
            return Light;
        if (s == "camera")
            return Camera;
        return Panel;
    }

    static const char *TypeToString(SceneObjectType t) {
        using enum SceneObjectType;
        switch (t) {
            case Prop:
                return "Prop";
            case Light:
                return "Light";
            case Camera:
                return "camera";
            default:
                return "Panel";
        }
    }

    static json BuildObjectPropsJson(const SceneObject &so) {
        json props = json::object();
        std::vector<std::string> propKeys;
        propKeys.reserve(so.props.size());
        for (const auto &[key, val]: so.props)
            propKeys.push_back(key);
        std::ranges::sort(propKeys);
        for (const auto &k: propKeys) {
            if (k == "_eid")
                continue; // runtime handle, never persisted
            if (!so.assetId.empty() && (k == "mesh" || k == "renderScale"))
                continue; // asset-backed objects resolve these from the asset registry
            props[k] = so.props.at(k);
        }
        return props;
    }

    static json BuildObjectComponentsJson(const SceneObject &so) {
        json comps = json::array();
        for (const auto &cd: so.components) {
            json c;
            c["type"] = cd.type;
            json cprops = json::object();
            std::vector<std::string> cpropKeys;
            cpropKeys.reserve(cd.props.size());
            for (const auto &[key, val]: cd.props)
                cpropKeys.push_back(key);
            std::ranges::sort(cpropKeys);
            for (const auto &k: cpropKeys)
                cprops[k] = cd.props.at(k);
            c["props"] = cprops;
            comps.push_back(std::move(c));
        }
        return comps;
    }

    static void ParseObjectComponents(SceneObject &so, const json &obj) {
        if (!obj.contains("components") || !obj["components"].is_array())
            return;
        for (const auto &comp: obj["components"]) {
            ComponentDesc cd;
            cd.type = comp.value("type", "");
            if (comp.contains("props") && comp["props"].is_object())
                for (auto &[k, v]: comp["props"].items())
                    cd.props[k] = v.get<std::string>();
            if (!cd.type.empty())
                so.components.push_back(std::move(cd));
        }
    }

    static void ApplyLegacyIsLight(SceneObject &so) {
        if (const auto isLightIt = so.props.find("isLight");
            isLightIt != so.props.end()) {
            const bool wantsLight =
                    (isLightIt->second == "true" || isLightIt->second == "1");
            if (const bool hasLightComponent = std::ranges::any_of(
                    so.components,
                    [](const ComponentDesc &c) { return c.type == "light"; });
                wantsLight && !hasLightComponent) {
                ComponentDesc light;
                light.type = "light";
                light.props["intensity"] = "1";
                light.props["color"] = "1,1,1";
                light.props["radius"] = "5";
                so.components.push_back(std::move(light));
            }
            so.props.erase(isLightIt);
        }
    }

    static SceneObject ParseSceneObject(const json &obj) {
        SceneObject so;
        so.id = obj.value("id", "");
        so.type = TypeFromString(obj.value("type", "Panel"));
        so.yaw = obj.value("yaw", 0.0f);
        so.pitch = obj.value("pitch", 0.0f);
        so.roll = obj.value("roll", 0.0f);
        so.assetId = obj.value("asset", "");
        if (obj.contains("prefab") && obj["prefab"].is_object()) {
            ScenePrefabInstance prefab;
            prefab.prefabId = obj["prefab"].value("id", "");
            prefab.sourcePath = obj["prefab"].value("path", "");
            if (!prefab.prefabId.empty() || !prefab.sourcePath.empty())
                so.prefabInstance = std::move(prefab);
        }
        auto pos = obj.value("position", json::array({0.f, 0.f, 0.f}));
        auto scl = obj.value("scale", json::array({1.f, 1.f, 1.f}));
        so.position = {pos[0].get<float>(), pos[1].get<float>(), pos[2].get<float>()};
        so.scale = {scl[0].get<float>(), scl[1].get<float>(), scl[2].get<float>()};
        if (obj.contains("props") && obj["props"].is_object())
            for (auto &[k, v]: obj["props"].items())
                so.props[k] = v.get<std::string>();
        ParseObjectComponents(so, obj);
        ApplyLegacyIsLight(so);
        return so;
    }

    SceneDocument SceneSerializer::LoadFromFile(const std::string &path) {
        std::ifstream f(path);
        if (!f.is_open())
            throw SceneSerializerException("SceneSerializer: cannot open '" + path +
                                           "'");

        json j;
        try {
            f >> j;
        } catch (const json::exception &e) {
            throw SceneSerializerException("SceneSerializer: JSON parse error in '" +
                                           path + "': " + e.what());
        }

        SceneDocument doc;
        doc.filePath = path;
        doc.version = j.value("version", 1);
        doc.sceneId = j.value("sceneId", "scene");
        if (doc.sceneId == "world")
            doc.sceneId = "scene";
        doc.sceneName = j.value("sceneName", "Scene");

        if (j.contains("settings") && j["settings"].is_object())
            for (auto &[k, v]: j["settings"].items())
                doc.settings[k] = v.is_string() ? v.get<std::string>() : v.dump();

        if (j.contains("assets") && j["assets"].is_object()) {
            for (auto &[id, def]: j["assets"].items()) {
                AssetDef ad;
                ad.mesh = def.value("mesh", "");
                ad.renderScale = def.value("renderScale", "1.0000,1.0000,1.0000");
                ad.albedoMap = def.value("albedoMap", "");
                ad.guid = def.value("guid", "");
                ad.displayName = def.value("displayName", "");
                EnsureAssetIdentity(id, &ad);
                doc.assets[id] = std::move(ad);
            }
        }

        if (!j.contains("objects") || !j["objects"].is_array())
            throw SceneSerializerException(
                "SceneSerializer: missing or invalid 'objects' array in '" + path +
                "'");
        for (const auto &obj: j["objects"])
            doc.objects.push_back(ParseSceneObject(obj));

        return doc;
    }

    void SceneSerializer::SaveToFile(const SceneDocument &doc,
                                     const std::string &path) {
        SceneDocument docToSave = doc;
        EnsureAssetIdentity(&docToSave);

        json j;
        j["version"] = docToSave.version;
        j["sceneId"] = docToSave.sceneId.empty() ? "scene" : docToSave.sceneId;
        j["sceneName"] = docToSave.sceneName.empty() ? "Scene" : docToSave.sceneName;

        // ---- Scene settings (stable key order) ----
        json settings = json::object();
        {
            std::vector<std::string> keys;
            keys.reserve(docToSave.settings.size());
            for (const auto &[key, val]: docToSave.settings)
                keys.push_back(key);
            std::ranges::sort(keys);
            for (const auto &k: keys)
                settings[k] = docToSave.settings.at(k);
        }
        j["settings"] = settings;

        // ---- Asset registry ----
        json assets = json::object();
        std::vector<std::string> assetIds;
        assetIds.reserve(docToSave.assets.size());
        for (const auto &[id, def]: docToSave.assets)
            assetIds.push_back(id);
        std::ranges::sort(assetIds);

        for (const auto &id: assetIds) {
            const auto &def = docToSave.assets.at(id);
            json d;
            d["guid"] = def.guid;
            d["displayName"] = def.displayName;
            d["mesh"] = def.mesh;
            d["renderScale"] = def.renderScale;
            if (!def.albedoMap.empty())
                d["albedoMap"] = def.albedoMap;
            assets[id] = d;
        }
        j["assets"] = assets;

        // ---- Objects ----
        j["objects"] = json::array();
        for (auto &so: docToSave.objects) {
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
            if (so.prefabInstance.has_value()) {
                obj["prefab"] = json{
                    {"id", so.prefabInstance->prefabId},
                    {"path", so.prefabInstance->sourcePath}
                };
            }

            if (json props = BuildObjectPropsJson(so); !props.empty())
                obj["props"] = std::move(props);

            if (!so.components.empty())
                obj["components"] = BuildObjectComponentsJson(so);

            j["objects"].push_back(obj);
        }

        namespace fs = std::filesystem;
        std::error_code ec;
        fs::path out(path);
        fs::create_directories(out.parent_path(), ec);

        std::ofstream f(path);
        if (!f.is_open())
            throw SceneSerializerException("SceneSerializer: cannot write '" + path +
                                           "'");
        f << j.dump(2);
    }
} // namespace Monolith::Editor
