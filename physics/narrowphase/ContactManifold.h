#pragma once
#include "math/Vec3.h"
#include <array>

namespace Monolith {
    struct ContactPoint {
        Vec3 point; // contact position in world space
        Vec3 normal; // collision normal, pointing from B toward A
        float penetration; // positive penetration depth
    };

    // Up to 4 contact points per collision pair.
    // Multiple contacts prevent torque instability when a box rests on a flat
    // surface (a single contact creates a lever-arm impulse that spins the body).
    struct ContactManifold {
        static constexpr int MAX_CONTACTS = 4;

        std::array<ContactPoint, MAX_CONTACTS> contacts{};
        int count = 0;

        bool hasContact() const { return count > 0; }

        void AddContact(const Vec3 &pt, const Vec3 &n, float pen) {
            if (count < MAX_CONTACTS) {
                contacts[static_cast<std::size_t>(count)] = {pt, n, pen};
                ++count;
            }
        }
    };
} // namespace Monolith
