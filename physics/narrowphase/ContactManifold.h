#pragma once
#include "math/Vec3.h"

namespace Monolith {

struct ContactPoint {
  Vec3 point;         // contact position in world space
  Vec3 normal;        // collision normal, pointing from B toward A
  float penetration;  // positive penetration depth
};

// Up to 4 contact points per collision pair.
// Multiple contacts prevent torque instability when a box rests on a flat surface
// (a single contact creates a lever-arm impulse that spins the body).
struct ContactManifold {
  static constexpr int MAX_CONTACTS = 4;

  ContactPoint contacts[MAX_CONTACTS];
  int count = 0;

  bool hasContact() const { return count > 0; }

  void AddContact(const Vec3& pt, const Vec3& n, float pen) {
    if (count < MAX_CONTACTS)
      contacts[count++] = {pt, n, pen};
  }
};

}  // namespace Monolith
