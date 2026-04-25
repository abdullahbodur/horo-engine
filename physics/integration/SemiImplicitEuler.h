#pragma once

namespace Monolith {
class RigidBody;

namespace SemiImplicitEuler {
void Integrate(RigidBody &body, float dt);
} // namespace SemiImplicitEuler
} // namespace Monolith
