#pragma once

namespace Horo {
    class RigidBody;

    namespace SemiImplicitEuler {
        void Integrate(RigidBody &body, float dt);
    } // namespace SemiImplicitEuler
} // namespace Horo
