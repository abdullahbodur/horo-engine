#include "scene/Scene.h"

#include "scene/components/TransformComponent.h"

namespace Horo {
    void Scene::AddSystem(std::unique_ptr<System> system) {
        m_systems.push_back(std::move(system));
    }

    void Scene::UpdateSystems(float dt) {
        for (const auto &system: m_systems)
            system->OnUpdate(m_registry, dt);
    }

    void Scene::AddRenderSystem(std::unique_ptr<System> system) {
        m_renderSystems.push_back(std::move(system));
    }

    void Scene::RenderSystems(float alpha) {
        for (const auto &system: m_renderSystems)
            system->OnUpdate(m_registry, alpha);
    }

    Entity Scene::CreateEntity(const Vec3 &position) {
        Entity e = m_registry.Create();
        TransformComponent tc;
        tc.current.position = position;
        tc.previous.position = position;
        m_registry.Add<TransformComponent>(e, tc);
        return e;
    }

    void Scene::Clear() {
        m_physics.Clear();
        m_registry.Clear();
    }
} // namespace Horo
