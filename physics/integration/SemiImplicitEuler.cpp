#include "physics/integration/SemiImplicitEuler.h"

#include "math/MathUtils.h"
#include "physics/RigidBody.h"

namespace Horo {

namespace SemiImplicitEuler {

void Integrate(RigidBody& body, float dt) {
  if (body.IsStatic())
    return;

  // Linear integration: semi-implicit Euler
  // 1. Compute acceleration from accumulated force
  Vec3 accel = body.forceAccum * body.invMass;

  // 2. Update velocity first (semi-implicit)
  body.velocity += accel * dt;
  body.velocity *= (1.0f - body.linearDamping * dt);

  // 3. Update position using new velocity
  body.position += body.velocity * dt;

  // Angular integration
  Vec3 angAccel = body.inertiaTensorInv * body.torqueAccum;
  body.angularVelocity += angAccel * dt;
  body.angularVelocity *= (1.0f - body.angularDamping * dt);

  // Integrate orientation via quaternion derivative:
  // dq/dt = 0.5 * (0, wx, wy, wz) * q
  Vec3 w = body.angularVelocity;
  Quaternion spin(w.x, w.y, w.z, 0.0f);
  Quaternion dq = spin * body.orientation;
  body.orientation.x += 0.5f * dq.x * dt;
  body.orientation.y += 0.5f * dq.y * dt;
  body.orientation.z += 0.5f * dq.z * dt;
  body.orientation.w += 0.5f * dq.w * dt;
  body.orientation = body.orientation.Normalized();
}

}  // namespace SemiImplicitEuler
}  // namespace Horo
