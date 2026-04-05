#pragma once

namespace Monolith {

class RigidBody;

namespace SemiImplicitEuler {

// vel += (F / mass) * dt
// pos += vel * dt
// angVel += I_inv * torque * dt
// orientation += (0.5 * angVel * q) * dt  (quaternion integration)
void Integrate(RigidBody& body, float dt);

}  // namespace SemiImplicitEuler
}  // namespace Monolith
