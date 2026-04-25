#include "physics/broadphase/BruteForce.h"

#include "math/Vec3.h"
#include "physics/BoxCollider.h"
#include "physics/RigidBody.h"
#include "physics/SphereCollider.h"

namespace Monolith::BruteForce {
// Returns AABB half-extents for a body
static Vec3 GetAABBHalf(const RigidBody &b) {
  if (!b.collider)
    return Vec3::Zero();

  if (b.collider->type == ColliderType::Sphere) {
    auto *s = static_cast<const SphereCollider *>(b.collider.get());
    return Vec3(s->radius);
  }
  if (b.collider->type == ColliderType::Box) {
    auto *box = static_cast<const BoxCollider *>(b.collider.get());
    return box->halfExtents;
  }
  return Vec3::Zero();
}

std::vector<std::pair<int, int>>
FindOverlappingPairs(const std::vector<RigidBody *> &bodies) {
  std::vector<std::pair<int, int>> pairs;
  auto n = static_cast<int>(bodies.size());

  for (int i = 0; i < n; i++) {
    for (int j = i + 1; j < n; j++) {
      if (!bodies[i]->collider || !bodies[j]->collider)
        continue;

      Vec3 halfA = GetAABBHalf(*bodies[i]);
      Vec3 halfB = GetAABBHalf(*bodies[j]);

      Vec3 diff = bodies[i]->position - bodies[j]->position;
      Vec3 sumH = halfA + halfB;

      // AABB overlap check
      if (std::abs(diff.x) <= sumH.x && std::abs(diff.y) <= sumH.y &&
          std::abs(diff.z) <= sumH.z) {
        pairs.emplace_back(i, j);
      }
    }
  }
  return pairs;
}
} // namespace Monolith::BruteForce
