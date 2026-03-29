#pragma once
#include "physics/narrowphase/ContactManifold.h"

namespace Monolith {

class RigidBody;

namespace SAT {

// Box-Box collision via Separating Axis Theorem.
// Returns a manifold with hasContact = false if no collision.
ContactManifold TestBoxBox(const RigidBody& a, const RigidBody& b);

}  // namespace SAT
}  // namespace Monolith
