#include "scene/SceneReferenceRuntime.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "editor/EditorUiLogic.h"
#include "math/MathUtils.h"
#include "physics/BoxCollider.h"
#include "scene/components/BehaviorComponent.h"
#include "scene/components/TransformComponent.h"

namespace Monolith {

namespace {

std::vector<std::string> ExtractLightObjectIds(const Editor::SceneDocument& document) {
  std::vector<std::string> ids;
  ids.reserve(document.objects.size());
  for (const auto& object : document.objects) {
    if (object.type == Editor::SceneObjectType::Light)
      ids.push_back(object.id);
  }
  return ids;
}

float ParseFloat(const std::string& value, float fallback) {
  if (value.empty())
    return fallback;
  try {
    return std::stof(value);
  } catch (...) {
    return fallback;
  }
}

Vec3 ParseColorCsv(const std::string& value, const Vec3& fallback) {
  Vec3 parsed = fallback;
  if (Editor::TryParseVec3Csv(value, &parsed))
    return parsed;
  return fallback;
}

Vec3 ForwardFromYawPitch(float yawDeg, float pitchDeg) {
  const float yawRad = ToRadians(yawDeg);
  const float pitchRad = ToRadians(std::clamp(pitchDeg, -89.0f, 89.0f));
  return {-std::sin(yawRad) * std::cos(pitchRad),
          std::sin(pitchRad),
          -std::cos(yawRad) * std::cos(pitchRad)};
}

}  // namespace

SceneReferenceRuntime::SceneReferenceRuntime(Scene* scene) : m_scene(scene) {}

SceneRuntimeOperationResult SceneReferenceRuntime::LoadDocument(const Editor::SceneDocument& document) {
  m_pendingLightObjectIds = ExtractLightObjectIds(document);
  return Editor::LoadSceneDocument(
      m_coordinator,
      document,
      [this](const RuntimeSceneDefinition& definition, std::string* error) {
        return ApplyRuntimeDefinition(definition, error);
      });
}

SceneRuntimeOperationResult SceneReferenceRuntime::ReloadDocument(const Editor::SceneDocument& document) {
  m_pendingLightObjectIds = ExtractLightObjectIds(document);
  return Editor::ReloadSceneDocument(
      m_coordinator,
      document,
      [this](const RuntimeSceneDefinition& definition, std::string* error) {
        return ApplyRuntimeDefinition(definition, error);
      });
}

SceneRuntimeOperationResult SceneReferenceRuntime::Unload() {
  return m_coordinator.Unload([this](std::string* error) { return ClearRuntimeState(error); });
}

bool SceneReferenceRuntime::UpdateLiveLight(const Editor::SceneObject& object, std::string* error) {
  if (error)
    *error = {};

  if (!m_coordinator.IsActive()) {
    if (error)
      *error = "SceneReferenceRuntime is not active.";
    return false;
  }
  if (object.type != Editor::SceneObjectType::Light) {
    if (error)
      *error = "Scene object is not a light.";
    return false;
  }

  const auto idIt = std::find(m_lightObjectIds.begin(), m_lightObjectIds.end(), object.id);
  if (idIt == m_lightObjectIds.end()) {
    if (error)
      *error = "Live light not found.";
    return false;
  }

  const std::size_t lightIndex = static_cast<std::size_t>(std::distance(m_lightObjectIds.begin(), idIt));
  if (lightIndex >= m_lights.size()) {
    if (error)
      *error = "Live light index is out of range.";
    return false;
  }

  Light& light = m_lights[lightIndex];
  light.position = object.position;
  const auto intensityIt = object.props.find("intensity");
  if (intensityIt != object.props.end())
    light.intensity = ParseFloat(intensityIt->second, light.intensity);
  const auto radiusIt = object.props.find("radius");
  if (radiusIt != object.props.end())
    light.radius = ParseFloat(radiusIt->second, light.radius);
  const auto colorIt = object.props.find("color");
  if (colorIt != object.props.end())
    light.color = ParseColorCsv(colorIt->second, light.color);

  const auto typeIt = object.props.find("lightType");
  if (typeIt != object.props.end()) {
    const std::string& type = typeIt->second;
    if (type == "directional")
      light.type = Light::Type::Directional;
    else if (type == "point")
      light.type = Light::Type::Point;
  }
  if (light.type == Light::Type::Directional)
    light.direction = ForwardFromYawPitch(object.yaw, object.pitch).Normalized();

  return true;
}

bool SceneReferenceRuntime::ApplyRuntimeDefinition(const RuntimeSceneDefinition& definition,
                                                   std::string* error) {
  if (!m_scene) {
    if (error)
      *error = "SceneReferenceRuntime requires a valid Scene.";
    return false;
  }

  ResetReferenceState();
  m_scene->Clear();

  for (const auto& room : definition.rooms) {
    m_scene->physics.gravity = room.gravity;

    for (const auto& panel : room.panels) {
      RigidBody wall = RigidBody::MakeStatic();
      wall.position = panel.center;
      wall.collider = std::make_shared<BoxCollider>(panel.half);
      m_scene->physics.AddBody(std::move(wall));
      m_panels.push_back({panel.center, panel.half});
      ++m_stats.panelCount;
      ++m_stats.staticBodyCount;
    }

    for (const auto& prop : room.props) {
      Entity entity = m_scene->CreateEntity(prop.position);
      ++m_stats.entityCount;
      ++m_stats.propCount;

      auto& transform = m_scene->registry.Get<TransformComponent>(entity);
      transform.current.position = prop.position;
      transform.previous = transform.current;
      transform.current.scale = prop.scale;
      transform.previous.scale = prop.scale;

      if (!prop.scriptTag.empty() && m_behaviorFactory) {
        std::unique_ptr<Behavior> behavior = m_behaviorFactory(prop.scriptTag);
        if (behavior) {
          m_scene->registry.Add<BehaviorComponent>(entity).behavior = std::move(behavior);
          ++m_stats.behaviorCount;
        }
      }

      if (m_propEntityCreatedCallback)
        m_propEntityCreatedCallback(prop, entity, *m_scene);
    }
  }

  m_lights = definition.lights;
  m_lightObjectIds = m_pendingLightObjectIds;
  m_pendingLightObjectIds.clear();
  m_sceneCamera = definition.sceneCamera;
  return true;
}

bool SceneReferenceRuntime::ClearRuntimeState(std::string* error) {
  if (!m_scene) {
    if (error)
      *error = "SceneReferenceRuntime requires a valid Scene.";
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

}  // namespace Monolith
