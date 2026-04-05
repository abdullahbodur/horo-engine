#pragma once
#include "math/Vec3.h"
#include "physics/Collider.h"

namespace Monolith {

struct BoxCollider : Collider {
  Vec3 halfExtents;  // half-widths in each axis

  explicit BoxCollider(const Vec3& half = {0.5f, 0.5f, 0.5f})
      : Collider(ColliderType::Box), halfExtents(half) {}
};

}  // namespace Monolith
