#pragma once
#include "physics/RigidBody.h"

namespace Horo {

// Stores a pointer to the RigidBody owned by PhysicsWorld
struct RigidBodyComponent {
  RigidBody* body = nullptr;
};

}  // namespace Horo
