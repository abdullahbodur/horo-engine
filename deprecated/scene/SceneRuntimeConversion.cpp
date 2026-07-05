#include "scene/SceneRuntimeConversion.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <string_view>
#include <unordered_map>

#include "math/MathUtils.h"

namespace Horo {
    namespace {
        /** @brief Converts scene light kind to the renderer-facing light type. */
        Light::Type ToRuntimeLightType(SceneLightKind kind) {
            switch (kind) {
                case SceneLightKind::Directional:
                    return Light::Type::Directional;
                case SceneLightKind::Spot:
                    return Light::Type::Spot;
                case SceneLightKind::Rect:
                    return Light::Type::Rect;
                case SceneLightKind::Sky:
                    return Light::Type::Sky;
                case SceneLightKind::Point:
                default:
                    return Light::Type::Point;
            }
        }

        /** @brief Builds a world-space forward vector from editor yaw/pitch degrees. */
        Vec3 ForwardFromYawPitch(float yawDeg, float pitchDeg) {
            const float yawRad = ToRadians(yawDeg);
            const float pitchRad = ToRadians(std::clamp(pitchDeg, -89.0f, 89.0f));
            return {
                -std::sin(yawRad) * std::cos(pitchRad),
                std::sin(pitchRad),
                -std::cos(yawRad) * std::cos(pitchRad),
            };
        }

        Vec3 ParseVec3Csv(std::string_view value, const Vec3 &fallback) {
            if (value.empty())
                return fallback;

            Vec3 parsed = fallback;
            std::array<char, 96> buffer{};
            std::copy_n(value.data(), std::min(value.size(), buffer.size() - 1),
                        buffer.data());
            const char *cursor = buffer.data();
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

        Vec3 MultiplyComponents(const Vec3 &a, const Vec3 &b) {
            return {a.x * b.x, a.y * b.y, a.z * b.z};
        }

        void AppendPropFromNode(
            const SceneNodeDefinition &node, const std::string &path,
            const std::unordered_map<std::string, const SceneAssetDefinition *,
                StringHash, std::equal_to<> > &assetsById,
            RuntimeSceneRoom &room, RuntimeSceneBuildResult &result) {
            auto addIssue = [&result](RuntimeSceneBuildIssue::Severity severity,
                                      std::string p, std::string message) {
                result.issues.emplace_back(severity, std::move(p), std::move(message));
            };
            RuntimeSceneProp prop;
            prop.id = node.id;
            prop.position = node.position;
            prop.yaw = node.yaw;
            prop.pitch = node.pitch;
            prop.roll = node.roll;
            prop.scale = node.scale;

            if (!node.assetId.empty()) {
                const auto assetIt = assetsById.find(node.assetId);
                if (assetIt == assetsById.end()) {
                    addIssue(RuntimeSceneBuildIssue::Severity::Error, path + ".assetId",
                             "assetId does not resolve to a declared scene asset.");
                    prop.meshTag = "box";
                } else {
                    prop.meshTag = assetIt->second->mesh;
                    prop.albedoMap = assetIt->second->albedoMap;
                    prop.normalMap = assetIt->second->normalMap;
                    prop.metallicRoughnessMap =
                            assetIt->second->metallicRoughnessMap;
                    prop.emissiveMap = assetIt->second->emissiveMap;
                    prop.occlusionMap = assetIt->second->occlusionMap;
                    prop.scale = MultiplyComponents(node.scale, assetIt->second->renderScale);
                }
            } else {
                const auto meshIt = node.extraProps.find("mesh");
                prop.meshTag =
                        (meshIt != node.extraProps.end()) ? meshIt->second : std::string();
                if (const auto renderScaleIt = node.extraProps.find("renderScale");
                    renderScaleIt != node.extraProps.end()) {
                    prop.scale = MultiplyComponents(
                        node.scale, ParseVec3Csv(renderScaleIt->second, Vec3::One()));
                }
                if (const auto albedoIt = node.extraProps.find("albedoMap"); albedoIt != node.extraProps.end())
                    prop.albedoMap = albedoIt->second;
                if (const auto normalIt = node.extraProps.find("normalMap"); normalIt != node.extraProps.end())
                    prop.normalMap = normalIt->second;
                if (const auto metallicRoughnessIt = node.extraProps.find("metallicRoughnessMap"); metallicRoughnessIt != node.extraProps.end())
                    prop.metallicRoughnessMap = metallicRoughnessIt->second;
                if (const auto emissiveIt = node.extraProps.find("emissiveMap"); emissiveIt != node.extraProps.end())
                    prop.emissiveMap = emissiveIt->second;
                if (const auto occlusionIt = node.extraProps.find("occlusionMap"); occlusionIt != node.extraProps.end())
                    prop.occlusionMap = occlusionIt->second;
            }

            prop.isLight = node.light.has_value();
            if (node.script.has_value())
                prop.scriptTag = node.script->behaviorTag;
            if (node.rigidbody.has_value()) {
                prop.rigidbody = RuntimeSceneProp::RuntimeRigidBody{
                    node.rigidbody->mass,
                    node.rigidbody->isKinematic,
                    node.rigidbody->useGravity
                };
            }

            room.props.push_back(std::move(prop));
        }

        void AppendLightFromNode(const SceneNodeDefinition &node,
                                 const std::string &path,
                                 RuntimeSceneDefinition &definition,
                                 RuntimeSceneBuildResult &result) {
            auto addIssue = [&result](RuntimeSceneBuildIssue::Severity severity,
                                      std::string p, std::string message) {
                result.issues.emplace_back(severity, std::move(p), std::move(message));
            };
            Light light;
            if (node.light.has_value()) {
                light.type = ToRuntimeLightType(node.light->kind);
                light.position = node.position;
                light.direction = ForwardFromYawPitch(node.yaw, node.pitch);
                light.color = node.light->color;
                light.intensity = node.light->intensity;
                light.radius = node.light->radius;
            } else {
                addIssue(RuntimeSceneBuildIssue::Severity::Warning, path + ".light",
                         "Light node is missing typed light properties; using defaults.");
                light.position = node.position;
            }
            definition.lights.push_back(std::move(light));
        }

        void ApplyCameraFromNode(const SceneNodeDefinition &node,
                                 RuntimeSceneDefinition &definition) {
            if (definition.sceneCamera.has_value())
                return;
            RuntimeSceneCamera camera;
            camera.position = node.position;
            camera.yaw = node.yaw;
            camera.pitch = std::clamp(node.pitch, -89.0f, 89.0f);
            if (node.camera.has_value()) {
                camera.fovY = node.camera->fovY;
                camera.nearClip = std::max(0.01f, node.camera->nearClip);
                camera.farClip = std::max(camera.nearClip + 0.01f, node.camera->farClip);
            }
            definition.sceneCamera = std::move(camera);
        }
    } // namespace

    RuntimeSceneBuildResult
    BuildRuntimeSceneDefinition(const SceneProjectModel &model) {
        RuntimeSceneBuildResult result;

        const SceneProjectValidationResult validation =
                ValidateSceneProjectModel(model);
        for (const auto &issue: validation.issues) {
            result.issues.emplace_back(
                issue.severity == SceneProjectValidationIssue::Severity::Error
                    ? RuntimeSceneBuildIssue::Severity::Error
                    : RuntimeSceneBuildIssue::Severity::Warning,
                issue.path, issue.message);
        }

        result.definition.spawnPoint = model.scene.settings.spawnPoint;
        const auto gravityIt = model.scene.settings.extraSettings.find("gravity");

        RuntimeSceneRoom room;
        room.id = model.scene.metadata.sceneId.empty()
                      ? "scene"
                      : model.scene.metadata.sceneId;
        if (gravityIt != model.scene.settings.extraSettings.end()) {
            room.gravity = ParseVec3Csv(gravityIt->second, room.gravity);
        }

        std::unordered_map<std::string, const SceneAssetDefinition *, StringHash,
                    std::equal_to<> >
                assetsById;
        for (const auto &asset: model.scene.assets)
            assetsById[asset.id] = &asset;

        for (std::size_t i = 0; i < model.scene.nodes.size(); ++i) {
            const SceneNodeDefinition &node = model.scene.nodes[i];
            const std::string path = std::format("scene.nodes[{}]", i);

            switch (node.kind) {
                    using enum SceneNodeKind;
                case Panel: {
                    RuntimeScenePanel runtimePanel;
                    runtimePanel.center = node.position;
                    runtimePanel.half = node.scale;
                    runtimePanel.rotation = Quaternion::FromEuler(
                        ToRadians(node.pitch), ToRadians(node.yaw),
                        ToRadians(node.roll));
                    room.panels.push_back(runtimePanel);
                    break;
                }
                case Prop:
                    AppendPropFromNode(node, path, assetsById, room, result);
                    break;
                case Light:
                    AppendLightFromNode(node, path, result.definition, result);
                    break;
                case Camera:
                    ApplyCameraFromNode(node, result.definition);
                    break;
            }
        }

        result.definition.rooms.push_back(std::move(room));
        return result;
    }
} // namespace Horo
