#include "scene/SceneReferenceRuntime.h"

#include "physics/BoxCollider.h"
#include "scene/components/BehaviorComponent.h"
#include "scene/components/TransformComponent.h"

namespace Monolith {

SceneReferenceRuntime::SceneReferenceRuntime(Scene* scene) : m_scene(scene) {}

SceneRuntimeOperationResult SceneReferenceRuntime::LoadDocument(const Editor::SceneDocument& document) {
  return Editor::LoadSceneDocument(
      m_coordinator,
      document,
      [this](const RuntimeSceneDefinition& definition, std::string* error) {
        return ApplyRuntimeDefinition(definition, error);
      });
}

SceneRuntimeOperationResult SceneReferenceRuntime::ReloadDocument(const Editor::SceneDocument& document) {
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
  m_scene->Clear();
  return true;
}

void SceneReferenceRuntime::ResetReferenceState() {
  m_lights.clear();
  m_sceneCamera.reset();
  m_panels.clear();
  m_stats = SceneReferenceStats{};
}

}  // namespace Monolith
