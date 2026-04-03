#pragma once
#include "scene/System.h"

namespace Horo {

class PhysicsWorld;

class PhysicsSystem : public System {
 public:
  explicit PhysicsSystem(PhysicsWorld& world) : m_world(world) {}
  void OnUpdate(Registry& registry, float dt) override;

 private:
  PhysicsWorld& m_world;
};

}  // namespace Horo
