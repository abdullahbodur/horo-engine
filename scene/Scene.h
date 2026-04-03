#pragma once
#include <memory>
#include <vector>

#include "physics/PhysicsWorld.h"
#include "scene/Registry.h"
#include "scene/System.h"

namespace Horo {

class Scene {
 public:
  Registry registry;
  PhysicsWorld physics;

  void AddSystem(std::unique_ptr<System> system);
  void UpdateSystems(float dt);

  // Render systems are updated separately (variable-rate, with alpha)
  void AddRenderSystem(std::unique_ptr<System> system);
  void RenderSystems(float alpha);

  // Convenience: creates entity, adds TransformComponent at position
  Entity CreateEntity(const Vec3& position = Vec3::Zero());

  // Destroy all entities, physics bodies, and component data.
  // Registered systems are preserved.
  void Clear();

 private:
  std::vector<std::unique_ptr<System>> m_systems;
  std::vector<std::unique_ptr<System>> m_renderSystems;
};

}  // namespace Horo
