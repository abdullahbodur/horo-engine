#pragma once
#include "physics/narrowphase/ContactManifold.h"

namespace Monolith {
    class RigidBody;

    struct ContactConstraint {
        RigidBody *bodyA = nullptr;
        RigidBody *bodyB = nullptr;
        ContactManifold manifold;

        // Cached impulse magnitudes (warm starting)
        float lambdaN = 0.0f; // normal
        float lambdaT1 = 0.0f; // friction tangent 1
        float lambdaT2 = 0.0f; // friction tangent 2

        // Solve the normal + friction impulse for this contact
        void Solve();
    };
} // namespace Monolith
