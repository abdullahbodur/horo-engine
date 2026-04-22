#pragma once
#include "physics/Collider.h"

namespace Monolith {
    struct SphereCollider : Collider {
        float radius;

        explicit SphereCollider(float r = 0.5f)
            : Collider(ColliderType::Sphere), radius(r) {
        }
    };
} // namespace Monolith
