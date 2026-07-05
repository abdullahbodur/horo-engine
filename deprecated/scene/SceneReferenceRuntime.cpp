#include "scene/SceneReferenceRuntime.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <stdexcept>

#include "ui/editor/EditorUiLogic.h"
#include "math/MathUtils.h"
#include "physics/BoxCollider.h"
#include "scene/components/BehaviorComponent.h"
#include "scene/components/RigidBodyComponent.h"
#include "scene/components/TransformComponent.h"
#include "ui/editor/SceneRuntimeCoordinatorBridge.h"

namespace Horo {
    namespace {
        std::vector<std::string>
        ExtractLightObjectIds(const Editor::SceneDocument &document) {
            std::vector<std::string> ids;
            ids.reserve(document.objects.size());
            for (const auto &object: document.objects) {
                if (object.type == Editor::SceneObjectType::Light) {
                    ids.push_back(object.id);
                }
            }
            return ids;
        }

        float ParseFloat(const std::string &value, float fallback) {
            if (value.empty()) {
                return fallback;
            }
            try {
                return std::stof(value);
            } catch (const std::invalid_argument &) {
                return fallback;
            } catch (const std::out_of_range &) {
                return fallback;
            }
        }

        Vec3 ParseColorCsv(const std::string &value, const Vec3 &fallback) {
            if (Vec3 parsed = fallback; Editor::TryParseVec3Csv(value, &parsed)) {
                return parsed;
            }
            return fallback;
        }

        Vec3 ForwardFromYawPitch(float yawDeg, float pitchDeg) {
            const float yawRad = ToRadians(yawDeg);
            const float pitchRad = ToRadians(std::clamp(pitchDeg, -89.0f, 89.0f));
            return {
                -std::sin(yawRad) * std::cos(pitchRad), std::sin(pitchRad),
                -std::cos(yawRad) * std::cos(pitchRad)
            };
        }

        bool IsSphereMeshTag(std::string_view tag) {
            return tag == "sphere";
        }

        Vec3 ComputeBoxHalfExtents(const RuntimeSceneProp &prop) {
            return {
                0.5f * std::abs(prop.scale.x),
                0.5f * std::abs(prop.scale.y),
                0.5f * std::abs(prop.scale.z)
            };
        }

        float ComputeSphereRadius(const RuntimeSceneProp &prop) {
            const float maxScale =
                    std::max({std::abs(prop.scale.x), std::abs(prop.scale.y),
                              std::abs(prop.scale.z)});
            return 0.5f * maxScale;
        }

        Quaternion BuildPropOrientation(const RuntimeSceneProp &prop) {
            return Quaternion::FromEuler(
                ToRadians(prop.pitch), ToRadians(prop.yaw), ToRadians(prop.roll));
        }

        void AttachBehaviorIfPresent(
            Scene *scene, const RuntimeSceneProp &prop,
            const SceneReferenceRuntime::BehaviorFactory &behaviorFactory,
            const Entity entity, SceneReferenceStats *stats) {
            if (!scene || !stats || prop.scriptTag.empty() || !behaviorFactory)
                return;

            std::unique_ptr<Behavior> behavior = behaviorFactory(prop.scriptTag);
            if (!behavior)
                return;
            scene->GetRegistry().Add<BehaviorComponent>(entity).behavior =
                    std::move(behavior);
            ++stats->behaviorCount;
        }

/** @brief Creates a RigidBody for a prop with the appropriate collider and mass. */
RigidBody CreateRigidBodyForProp(const RuntimeSceneProp &prop) {
    const float mass = (!prop.rigidbody || prop.rigidbody->isKinematic ||
                        prop.rigidbody->mass <= 0.0f)
                           ? 0.0f
                           : prop.rigidbody->mass;
    RigidBody body = IsSphereMeshTag(prop.meshTag)
                         ? RigidBody::MakeSphere(ComputeSphereRadius(prop), mass)
                         : RigidBody::MakeBox(ComputeBoxHalfExtents(prop), mass);
    body.position = prop.position;
    body.orientation = BuildPropOrientation(prop);
    if (prop.rigidbody)
        body.useGravity = prop.rigidbody->useGravity;
    return body;
}

/** @brief Installs a single room panel as a physics wall. */
void ApplyRoomPanel(Scene *scene, const RuntimeScenePanel &panel,
                    SceneReferenceStats &stats) {
    RigidBody wall = RigidBody::MakeStatic();
    wall.position = panel.center;
    wall.orientation = panel.rotation;
    wall.collider = std::make_shared<BoxCollider>(panel.half);
    scene->GetPhysics().AddBody(std::move(wall));
    ++stats.panelCount;
    ++stats.staticBodyCount;
}

/** @brief Spawns a single room prop with optional physics and behavior. */
void ApplyRoomProp(Scene *scene, const RuntimeSceneProp &prop,
                   Entity entity,
                   const SceneReferenceRuntime::BehaviorFactory &behaviorFactory,
                   SceneReferenceStats &stats,
                   const SceneReferenceRuntime::PropEntityCreatedCallback &callback) {
    auto &transform = scene->GetRegistry().Get<TransformComponent>(entity);
    transform.current.position = prop.position;
    transform.previous = transform.current;
    transform.current.scale = prop.scale;
    transform.previous.scale = prop.scale;

    if (prop.rigidbody.has_value()) {
        RigidBody body = CreateRigidBodyForProp(prop);
        RigidBody *bodyPtr = scene->GetPhysics().AddBody(std::move(body));
        RigidBodyComponent rbc;
        rbc.body = bodyPtr;
        scene->GetRegistry().Add<RigidBodyComponent>(entity, rbc);
        if (bodyPtr && bodyPtr->IsStatic())
            ++stats.staticBodyCount;
    }

    AttachBehaviorIfPresent(scene, prop, behaviorFactory, entity, &stats);

    if (callback)
        callback(prop, entity, *scene);
}

    } // namespace

    SceneReferenceRuntime::SceneReferenceRuntime(Scene *scene) : m_scene(scene) {
    }

    SceneRuntimeOperationResult
    SceneReferenceRuntime::LoadDocument(const Editor::SceneDocument &document) {
        m_pendingLightObjectIds = ExtractLightObjectIds(document);
        return Editor::LoadSceneDocument(
            m_coordinator, document,
            [this](const RuntimeSceneDefinition &definition, std::string *error) {
                return ApplyRuntimeDefinition(definition, error);
            });
    }

    SceneRuntimeOperationResult
    SceneReferenceRuntime::ReloadDocument(const Editor::SceneDocument &document) {
        m_pendingLightObjectIds = ExtractLightObjectIds(document);
        return Editor::ReloadSceneDocument(
            m_coordinator, document,
            [this](const RuntimeSceneDefinition &definition, std::string *error) {
                return ApplyRuntimeDefinition(definition, error);
            });
    }

    SceneRuntimeOperationResult SceneReferenceRuntime::Unload() {
        return m_coordinator.Unload(
            [this](std::string *error) { return ClearRuntimeState(error); });
    }

    bool SceneReferenceRuntime::UpdateLiveLight(const Editor::SceneObject &object,
                                                std::string *error) {
        if (error) {
            *error = {};
        }

        if (!m_coordinator.IsActive()) {
            if (error) {
                *error = "SceneReferenceRuntime is not active.";
            }
            return false;
        }
        if (object.type != Editor::SceneObjectType::Light) {
            if (error) {
                *error = "Scene object is not a light.";
            }
            return false;
        }

        const auto idIt = std::ranges::find(m_lightObjectIds, object.id);
        if (idIt == m_lightObjectIds.end()) {
            if (error) {
                *error = "Live light not found.";
            }
            return false;
        }

        const auto lightIndex =
                static_cast<std::size_t>(std::distance(m_lightObjectIds.begin(), idIt));
        if (lightIndex >= m_lights.size()) {
            if (error) {
                *error = "Live light index is out of range.";
            }
            return false;
        }

        Light &light = m_lights[lightIndex];
        light.position = object.position;
        if (const auto intensityIt = object.props.find("intensity");
            intensityIt != object.props.end()) {
            light.intensity = ParseFloat(intensityIt->second, light.intensity);
        }
        if (const auto radiusIt = object.props.find("radius");
            radiusIt != object.props.end()) {
            light.radius = ParseFloat(radiusIt->second, light.radius);
        }
        if (const auto colorIt = object.props.find("color");
            colorIt != object.props.end()) {
            light.color = ParseColorCsv(colorIt->second, light.color);
        }

        if (const auto typeIt = object.props.find("lightType");
            typeIt != object.props.end()) {
            const std::string &type = typeIt->second;
            if (type == "directional") {
                light.type = Light::Type::Directional;
            } else if (type == "point") {
                light.type = Light::Type::Point;
            }
        }
        if (light.type == Light::Type::Directional) {
            light.direction =
                    ForwardFromYawPitch(object.yaw, object.pitch).Normalized();
        }

        return true;
    }

    /** @copydoc SceneReferenceRuntime::ApplyRoom */
    void SceneReferenceRuntime::ApplyRoom(const RuntimeSceneRoom &room) {
        m_scene->GetPhysics().SetGravity(room.gravity);

        for (const auto &panel: room.panels) {
            ApplyRoomPanel(m_scene, panel, m_stats);
            m_panels.emplace_back(panel.center, panel.half, panel.rotation);
        }

        for (const auto &prop: room.props) {
            Entity entity = m_scene->CreateEntity(prop.position);
            ++m_stats.entityCount;
            ++m_stats.propCount;
            ApplyRoomProp(m_scene, prop, entity, m_behaviorFactory,
                          m_stats, m_propEntityCreatedCallback);
        }
    }

    bool SceneReferenceRuntime::ApplyRuntimeDefinition(
        const RuntimeSceneDefinition &definition, std::string *error) {
        if (!m_scene) {
            if (error)
                *error = "SceneReferenceRuntime requires a valid Scene.";
            return false;
        }

        ResetReferenceState();
        m_scene->Clear();

        for (const auto &room: definition.rooms) {
            ApplyRoom(room);
        }

        m_lights = definition.lights;
        m_lightObjectIds = m_pendingLightObjectIds;
        m_pendingLightObjectIds.clear();
        m_sceneCamera = definition.sceneCamera;
        return true;
    }

    bool SceneReferenceRuntime::ClearRuntimeState(std::string *error) {
        if (!m_scene) {
            if (error) {
                *error = "SceneReferenceRuntime requires a valid Scene.";
            }
            return false;
        }

        ResetReferenceState();
        m_pendingLightObjectIds.clear();
        m_scene->Clear();
        return true;
    }

    void SceneReferenceRuntime::ResetReferenceState() {
        m_lights.clear();
        m_lightObjectIds.clear();
        m_sceneCamera.reset();
        m_panels.clear();
        m_stats = SceneReferenceStats{};
    }
} // namespace Horo
