#pragma once
#include "physics/narrowphase/ContactManifold.h"

namespace Horo {

class RigidBody;

namespace GJK {

// GJK + EPA general convex collision detection.
// For milestone 1 the sphere-plane path in PhysicsWorld bypasses this.
ContactManifold Test(const RigidBody& a, const RigidBody& b);

}  // namespace GJK
}  // namespace Horo
