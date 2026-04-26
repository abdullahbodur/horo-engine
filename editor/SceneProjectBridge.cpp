#include "editor/SceneProjectBridge.h"

#include <algorithm>
#include <cstdlib>
#include <format>
#include <initializer_list>

#include "core/Logger.h"
#include "core/ProjectPath.h"
#include "editor/AssetIdentity.h"
#include "editor/SceneSerializer.h"

namespace Horo::Editor {
    namespace {
        float ParseFloat(std::string_view value, float fallback) {
            if (value.empty())
                return fallback;
            try {
                return std::stof(std::string{value});
            } catch (const std::invalid_argument &) {
                return fallback;
            } catch (const std::out_of_range &) {
                return fallback;
            }
        }

        bool ParseBool(std::string_view value, bool fallback) {
            if (value == "true" || value == "1")
                return true;
            if (value == "false" || value == "0")
                return false;
            return fallback;
        }

        Vec3 ParseVec3Csv(std::string_view value, Vec3 fallback) {
            if (value.empty())
                return fallback;
            std::string buf{value};
            Vec3 parsed = fallback;
            const char *cursor = buf.data();
            char *end = nullptr;
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

        std::string FormatFloat(float value) { return std::format("{:.4f}", value); }

        std::string FormatVec3Csv(const Vec3 &value) {
            return FormatFloat(value.x) + "," + FormatFloat(value.y) + "," +
                   FormatFloat(value.z);
        }

        SceneNodeKind ToSceneNodeKind(SceneObjectType type) {
            switch (type) {
                case SceneObjectType::Prop:
                    return SceneNodeKind::Prop;
                case SceneObjectType::Light:
                    return SceneNodeKind::Light;
                case SceneObjectType::Camera:
                    return SceneNodeKind::Camera;
                default:
                    return SceneNodeKind::Panel;
            }
        }

        SceneObjectType ToSceneObjectType(SceneNodeKind kind) {
            switch (kind) {
                case SceneNodeKind::Prop:
                    return SceneObjectType::Prop;
                case SceneNodeKind::Light:
                    return SceneObjectType::Light;
                case SceneNodeKind::Camera:
                    return SceneObjectType::Camera;
                default:
                    return SceneObjectType::Panel;
            }
        }

        const std::string *
        FindProp(const std::unordered_map<std::string, std::string, StringHash,
                     std::equal_to<> > &props,
                 const char *key) {
            const auto it = props.find(key);
            return (it != props.end()) ? &it->second : nullptr;
        }

        void EraseKeys(std::unordered_map<std::string, std::string, StringHash,
                           std::equal_to<> > *props,
                       std::initializer_list<const char *> keys) {
            for (const char *key: keys)
                props->erase(key);
        }

        void MergeTypedAsset(SceneProjectModel *model,
                             const SceneAssetDefinition &asset) {
            if (!model)
                return;
            for (SceneAssetDefinition &existing: model->scene.assets) {
                if (existing.id == asset.id) {
                    existing = asset;
                    return;
                }
            }
            model->scene.assets.push_back(asset);
        }

        SceneCameraProperties BuildCameraFromNode(SceneNodeDefinition &node) {
            SceneCameraProperties camera;
            if (const std::string *fov = FindProp(node.extraProps, "fov"))
                camera.fovY = ParseFloat(*fov, camera.fovY);
            if (const std::string *nearClip = FindProp(node.extraProps, "nearClip"))
                camera.nearClip = ParseFloat(*nearClip, camera.nearClip);
            if (const std::string *farClip = FindProp(node.extraProps, "farClip"))
                camera.farClip = ParseFloat(*farClip, camera.farClip);
            camera.extraProps = node.extraProps;
            EraseKeys(&camera.extraProps, {"fov", "nearClip", "farClip"});
            EraseKeys(&node.extraProps, {"fov", "nearClip", "farClip"});
            return camera;
        }

        SceneLightProperties BuildLightFromNodeProps(SceneNodeDefinition &node) {
            SceneLightProperties light;
            if (const std::string *lightType = FindProp(node.extraProps, "lightType"))
                light.kind = (*lightType == "directional")
                                 ? SceneLightKind::Directional
                                 : SceneLightKind::Point;
            if (const std::string *intensity = FindProp(node.extraProps, "intensity"))
                light.intensity = ParseFloat(*intensity, light.intensity);
            if (const std::string *color = FindProp(node.extraProps, "color"))
                light.color = ParseVec3Csv(*color, light.color);
            if (const std::string *radius = FindProp(node.extraProps, "radius"))
                light.radius = ParseFloat(*radius, light.radius);
            light.extraProps = node.extraProps;
            EraseKeys(&light.extraProps, {"lightType", "intensity", "color", "radius"});
            EraseKeys(&node.extraProps, {"lightType", "intensity", "color", "radius"});
            return light;
        }

        SceneScriptProperties BuildScriptFromComponent(const ComponentDesc &component) {
            SceneScriptProperties script;
            if (const auto it = component.props.find("behaviorTag");
                it != component.props.end())
                script.behaviorTag = it->second;
            script.extraProps = component.props;
            script.extraProps.erase("behaviorTag");
            return script;
        }

        SceneRigidBodyProperties
        BuildRigidbodyFromComponent(const ComponentDesc &component) {
            SceneRigidBodyProperties rigidbody;
            if (const auto it = component.props.find("mass"); it != component.props.end())
                rigidbody.mass = ParseFloat(it->second, rigidbody.mass);
            if (const auto it = component.props.find("isKinematic");
                it != component.props.end())
                rigidbody.isKinematic = ParseBool(it->second, rigidbody.isKinematic);
            if (const auto it = component.props.find("useGravity");
                it != component.props.end())
                rigidbody.useGravity = ParseBool(it->second, rigidbody.useGravity);
            rigidbody.extraProps = component.props;
            EraseKeys(&rigidbody.extraProps, {"mass", "isKinematic", "useGravity"});
            return rigidbody;
        }

        SceneLightProperties BuildLightFromComponent(const ComponentDesc &component) {
            SceneLightProperties light;
            if (const auto it = component.props.find("lightType");
                it != component.props.end())
                light.kind = (it->second == "directional")
                                 ? SceneLightKind::Directional
                                 : SceneLightKind::Point;
            if (const auto it = component.props.find("intensity");
                it != component.props.end())
                light.intensity = ParseFloat(it->second, light.intensity);
            if (const auto it = component.props.find("color");
                it != component.props.end())
                light.color = ParseVec3Csv(it->second, light.color);
            if (const auto it = component.props.find("radius");
                it != component.props.end())
                light.radius = ParseFloat(it->second, light.radius);
            light.extraProps = component.props;
            EraseKeys(&light.extraProps, {"lightType", "intensity", "color", "radius"});
            return light;
        }

        void ApplyComponentToNode(SceneNodeDefinition &node,
                                  const ComponentDesc &component) {
            if (component.type == "script" && !node.script.has_value()) {
                node.script = BuildScriptFromComponent(component);
                return;
            }
            if (component.type == "rigidbody" && !node.rigidbody.has_value()) {
                node.rigidbody = BuildRigidbodyFromComponent(component);
                return;
            }
            if (component.type == "light" && !node.light.has_value()) {
                node.light = BuildLightFromComponent(component);
                return;
            }
            node.extraComponents.emplace_back(component.type, component.props);
        }

        void ApplyLegacyPropsToNode(SceneNodeDefinition &node,
                                    const SceneObject &object) {
            if (!node.script.has_value()) {
                if (const auto it = object.props.find("behavior");
                    it != object.props.end() && !it->second.empty() &&
                    it->second != "none") {
                    SceneScriptProperties script;
                    script.behaviorTag = it->second;
                    node.script = std::move(script);
                    node.extraProps.erase("behavior");
                }
            }
            if (!node.light.has_value()) {
                if (const auto it = object.props.find("isLight");
                    it != object.props.end() && ParseBool(it->second, false)) {
                    node.light = SceneLightProperties{};
                    node.extraProps.erase("isLight");
                }
            }
        }

        SceneNodeDefinition BuildNodeDefinitionFromObject(const SceneObject &object) {
            SceneNodeDefinition node;
            node.id = object.id;
            node.kind = ToSceneNodeKind(object.type);
            node.position = object.position;
            node.scale = object.scale;
            node.yaw = object.yaw;
            node.pitch = object.pitch;
            node.roll = object.roll;
            node.assetId = object.assetId;
            node.extraProps = object.props;
            if (object.prefabInstance.has_value()) {
                node.prefabInstance = ScenePrefabReference{
                    object.prefabInstance->prefabId, object.prefabInstance->sourcePath
                };
            }

            if (const auto parentIt = node.extraProps.find("parentId");
                parentIt != node.extraProps.end() && !parentIt->second.empty()) {
                node.parentId = parentIt->second;
                node.extraProps.erase(parentIt);
            }

            if (node.kind == SceneNodeKind::Camera)
                node.camera = BuildCameraFromNode(node);
            if (node.kind == SceneNodeKind::Light)
                node.light = BuildLightFromNodeProps(node);

            for (const auto &component: object.components)
                ApplyComponentToNode(node, component);

            ApplyLegacyPropsToNode(node, object);
            return node;
        }

        void ApplyPrefabOverrides(const SceneObject &instanceObject,
                                  SceneNodeDefinition *node) {
            if (!node)
                return;
            node->id = instanceObject.id;
            node->position = instanceObject.position;
            node->scale = instanceObject.scale;
            node->yaw = instanceObject.yaw;
            node->pitch = instanceObject.pitch;
            node->roll = instanceObject.roll;

            const auto parentIt = instanceObject.props.find("parentId");
            if (parentIt != instanceObject.props.end() && !parentIt->second.empty())
                node->parentId = parentIt->second;
            else
                node->parentId.reset();
        }

        void ApplyPrefabToNode(SceneProjectModel &model, SceneNodeDefinition &node,
                               const SceneObject &object) {
            const std::filesystem::path prefabPath =
                    std::filesystem::path(object.prefabInstance->sourcePath).is_absolute()
                        ? std::filesystem::path(object.prefabInstance->sourcePath)
                        : ProjectPath::Resolve(object.prefabInstance->sourcePath);
            const SceneDocument prefabDoc =
                    SceneSerializer::LoadFromFile(prefabPath.generic_string());
            if (prefabDoc.objects.empty())
                return;
            for (const auto &[assetId, asset]: prefabDoc.assets) {
                AssetDef normalizedAsset = asset;
                EnsureAssetIdentity(assetId, &normalizedAsset);
                MergeTypedAsset(&model,
                                SceneAssetDefinition{
                                    assetId, normalizedAsset.mesh,
                                    ParseVec3Csv(normalizedAsset.renderScale, Vec3::One()),
                                    normalizedAsset.albedoMap, normalizedAsset.guid,
                                    MakeAssetDisplayName(assetId, normalizedAsset)
                                });
            }
            SceneNodeDefinition prefabNode =
                    BuildNodeDefinitionFromObject(prefabDoc.objects.front());
            prefabNode.prefabInstance = ScenePrefabReference{
                object.prefabInstance->prefabId, object.prefabInstance->sourcePath
            };
            ApplyPrefabOverrides(object, &prefabNode);
            node = std::move(prefabNode);
        }

        void ApplyCameraToObjectProps(SceneObject &object,
                                      const SceneNodeDefinition &node) {
            const SceneCameraProperties camera =
                    node.camera.value_or(SceneCameraProperties{});
            object.props["fov"] = FormatFloat(camera.fovY);
            object.props["nearClip"] = FormatFloat(camera.nearClip);
            object.props["farClip"] = FormatFloat(camera.farClip);
            for (const auto &[key, value]: camera.extraProps)
                object.props[key] = value;
        }

        void ApplyLightObjectToObjectProps(SceneObject &object,
                                           const SceneLightProperties &light) {
            object.props["lightType"] =
                    (light.kind == SceneLightKind::Directional) ? "directional" : "point";
            object.props["intensity"] = FormatFloat(light.intensity);
            object.props["color"] = FormatVec3Csv(light.color);
            object.props["radius"] = FormatFloat(light.radius);
            for (const auto &[key, value]: light.extraProps)
                object.props[key] = value;
        }

        ComponentDesc BuildRigidbodyComponent(const SceneRigidBodyProperties &rb) {
            ComponentDesc rigidbody;
            rigidbody.type = "rigidbody";
            rigidbody.props["mass"] = FormatFloat(rb.mass);
            rigidbody.props["isKinematic"] = rb.isKinematic ? "true" : "false";
            rigidbody.props["useGravity"] = rb.useGravity ? "true" : "false";
            for (const auto &[key, value]: rb.extraProps)
                rigidbody.props[key] = value;
            return rigidbody;
        }

        ComponentDesc BuildScriptComponent(const SceneScriptProperties &script) {
            ComponentDesc desc;
            desc.type = "script";
            desc.props["behaviorTag"] = script.behaviorTag;
            for (const auto &[key, value]: script.extraProps)
                desc.props[key] = value;
            return desc;
        }

        ComponentDesc BuildLightComponent(const SceneLightProperties &light) {
            ComponentDesc desc;
            desc.type = "light";
            desc.props["lightType"] =
                    (light.kind == SceneLightKind::Directional) ? "directional" : "point";
            desc.props["intensity"] = FormatFloat(light.intensity);
            desc.props["color"] = FormatVec3Csv(light.color);
            desc.props["radius"] = FormatFloat(light.radius);
            for (const auto &[key, value]: light.extraProps)
                desc.props[key] = value;
            return desc;
        }

        SceneObject BuildSceneObjectFromNode(const SceneNodeDefinition &node) {
            SceneObject object;
            object.id = node.id;
            object.type = ToSceneObjectType(node.kind);
            object.position = node.position;
            object.scale = node.scale;
            object.yaw = node.yaw;
            object.pitch = node.pitch;
            object.roll = node.roll;
            object.assetId = node.assetId;
            if (node.prefabInstance.has_value()) {
                object.prefabInstance = ScenePrefabInstance{
                    node.prefabInstance->prefabId, node.prefabInstance->sourcePath
                };
            }
            object.props = node.extraProps;
            if (node.parentId.has_value() && !node.parentId->empty())
                object.props["parentId"] = *node.parentId;
            if (node.kind == SceneNodeKind::Camera)
                ApplyCameraToObjectProps(object, node);
            if (node.kind == SceneNodeKind::Light && node.light.has_value())
                ApplyLightObjectToObjectProps(object, *node.light);
            if (node.rigidbody.has_value())
                object.components.push_back(BuildRigidbodyComponent(*node.rigidbody));
            if (node.script.has_value())
                object.components.push_back(BuildScriptComponent(*node.script));
            if (node.light.has_value() && node.kind != SceneNodeKind::Light)
                object.components.push_back(BuildLightComponent(*node.light));
            for (const auto &extra: node.extraComponents)
                object.components.emplace_back(extra.type, extra.props);
            return object;
        }
    } // namespace

    SceneProjectModel BuildSceneProjectModel(const SceneDocument &doc) {
        SceneProjectModel model;
        model.scene.metadata.schemaVersion = doc.version;
        model.scene.metadata.sceneId = doc.sceneId.empty() ? "scene" : doc.sceneId;
        model.scene.metadata.sceneName =
                doc.sceneName.empty() ? "Scene" : doc.sceneName;
        model.scene.metadata.sourcePath = doc.filePath;

        model.project.schemaVersion = doc.version;
        model.project.projectId = model.scene.metadata.sceneId.empty()
                                      ? "project"
                                      : model.scene.metadata.sceneId + "_project";
        model.project.defaultSceneId = model.scene.metadata.sceneId;
        model.project.projectName = model.scene.metadata.sceneName.empty()
                                        ? "Project"
                                        : model.scene.metadata.sceneName;
        model.project.scenes.emplace_back(model.scene.metadata.sceneId, doc.filePath);

        model.scene.settings.extraSettings = doc.settings;
        if (const auto spawnIt =
                    model.scene.settings.extraSettings.find("spawnPoint");
            spawnIt != model.scene.settings.extraSettings.end()) {
            model.scene.settings.spawnPoint =
                    ParseVec3Csv(spawnIt->second, model.scene.settings.spawnPoint);
            model.scene.settings.extraSettings.erase(spawnIt);
        }

        std::vector<std::string> assetIds;
        assetIds.reserve(doc.assets.size());
        for (const auto &[assetId, assetDef]: doc.assets)
            assetIds.push_back(assetId);
        std::ranges::sort(assetIds);

        for (const auto &assetId: assetIds) {
            AssetDef asset = doc.assets.at(assetId);
            EnsureAssetIdentity(assetId, &asset);
            SceneAssetDefinition typedAsset;
            typedAsset.id = assetId;
            typedAsset.mesh = asset.mesh;
            typedAsset.renderScale = ParseVec3Csv(asset.renderScale, Vec3::One());
            typedAsset.albedoMap = asset.albedoMap;
            typedAsset.guid = asset.guid;
            typedAsset.displayName = MakeAssetDisplayName(assetId, asset);
            model.scene.assets.push_back(std::move(typedAsset));
        }

        model.scene.nodes.reserve(doc.objects.size());
        for (const auto &object: doc.objects) {
            SceneNodeDefinition node = BuildNodeDefinitionFromObject(object);
            if (object.prefabInstance.has_value() &&
                !object.prefabInstance->sourcePath.empty()) {
                try {
                    ApplyPrefabToNode(model, node, object);
                } catch (const SceneSerializerException &e) {
                    LogWarn("SceneProjectBridge: prefab load skipped: {}", e.what());
                } catch (const std::filesystem::filesystem_error &e) {
                    LogWarn("SceneProjectBridge: prefab load skipped: {}", e.what());
                }
            }

            model.scene.nodes.push_back(std::move(node));
        }

        return model;
    }

    SceneDocument BuildSceneDocument(const SceneProjectModel &model) {
        SceneDocument doc;
        doc.version = model.scene.metadata.schemaVersion;
        doc.sceneId = model.scene.metadata.sceneId;
        doc.sceneName = model.scene.metadata.sceneName;
        doc.filePath = model.scene.metadata.sourcePath;
        doc.settings = model.scene.settings.extraSettings;
        doc.settings["spawnPoint"] = FormatVec3Csv(model.scene.settings.spawnPoint);

        for (const auto &asset: model.scene.assets) {
            doc.assets[asset.id] =
                    AssetDef{
                        asset.mesh, FormatVec3Csv(asset.renderScale), asset.albedoMap,
                        asset.guid, asset.displayName
                    };
        }

        doc.objects.reserve(model.scene.nodes.size());
        for (const auto &node: model.scene.nodes)
            doc.objects.push_back(BuildSceneObjectFromNode(node));

        return doc;
    }
} // namespace Horo::Editor
