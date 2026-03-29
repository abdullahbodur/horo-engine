#include "physics/RigidBody.h"

#include "math/MathUtils.h"
#include "physics/BoxCollider.h"
#include "physics/SphereCollider.h"

namespace Monolith {

RigidBody RigidBody::MakeStatic() {
  RigidBody b;
  b.mass = 0.0f;
  b.invMass = 0.0f;
  b.inertiaTensorInv = Mat3::Zero();
  return b;
}

RigidBody RigidBody::MakeSphere(float radius, float massVal) {
  RigidBody b;
  b.SetMass(massVal);
  b.SetSphereInertia(radius);
  b.collider = std::make_shared<SphereCollider>(radius);
  return b;
}

RigidBody RigidBody::MakeBox(const Vec3& halfExtents, float massVal) {
  RigidBody b;
  b.SetMass(massVal);
  b.SetBoxInertia(halfExtents);
  b.collider = std::make_shared<BoxCollider>(halfExtents);
  return b;
}

void RigidBody::SetMass(float m) {
  mass = m;
  invMass = NearlyZero(m) ? 0.0f : 1.0f / m;
}

void RigidBody::SetSphereInertia(float radius) {
  if (NearlyZero(invMass)) {
    inertiaTensorInv = Mat3::Zero();
    return;
  }
  float I = (2.0f / 5.0f) * mass * radius * radius;
  Mat3 tensor(I);
  inertiaTensorInv = tensor.Inverse();
}

void RigidBody::SetBoxInertia(const Vec3& h) {
  if (NearlyZero(invMass)) {
    inertiaTensorInv = Mat3::Zero();
    return;
  }
  float Ix = (1.0f / 12.0f) * mass * (4 * h.y * h.y + 4 * h.z * h.z);
  float Iy = (1.0f / 12.0f) * mass * (4 * h.x * h.x + 4 * h.z * h.z);
  float Iz = (1.0f / 12.0f) * mass * (4 * h.x * h.x + 4 * h.y * h.y);
  Mat3 tensor;
  tensor.m[0][0] = Ix;
  tensor.m[1][1] = Iy;
  tensor.m[2][2] = Iz;
  inertiaTensorInv = tensor.Inverse();
}

void RigidBody::UpdateWorldInertia() {
  // Rotate inertia tensor to world space: I_world_inv = R * I_body_inv * R^T
  Mat3 R = orientation.ToMat3();
  // For now, the sphere is isotropic so this is a no-op, but left here for box support
  (void)R;
}

void RigidBody::AddForceAtPoint(const Vec3& f, const Vec3& worldPoint) {
  forceAccum += f;
  torqueAccum += Vec3::Cross(worldPoint - position, f);
}

}  // namespace Monolith
