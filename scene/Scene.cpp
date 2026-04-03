#include "scene/Scene.h"

#include "scene/components/TransformComponent.h"

namespace Horo {

void Scene::AddSystem(std::unique_ptr<System> system) {
  m_systems.push_back(std::move(system));
}

void Scene::UpdateSystems(float dt) {
  for (auto& system : m_systems)
    system->OnUpdate(registry, dt);
}

void Scene::AddRenderSystem(std::unique_ptr<System> system) {
  m_renderSystems.push_back(std::move(system));
}

void Scene::RenderSystems(float alpha) {
  for (auto& system : m_renderSystems)
    system->OnUpdate(registry, alpha);
}

Entity Scene::CreateEntity(const Vec3& position) {
  Entity e = registry.Create();
  TransformComponent tc;
  tc.current.position = position;
  tc.previous.position = position;
  registry.Add<TransformComponent>(e, tc);
  return e;
}

void Scene::Clear() {
  physics.Clear();
  registry.Clear();
}

}  // namespace Horo
