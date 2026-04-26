#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "math/MathUtils.h"
#include "math/Vec3.h"
#include "physics/RigidBody.h"
#include "physics/integration/SemiImplicitEuler.h"

using namespace Horo;
using Catch::Approx;

// ============================================================
// SemiImplicitEuler — angular integration
// ============================================================

TEST_CASE("SemiImplicitEuler: static body ignores torque", "[integration][angular]") {
  RigidBody b = RigidBody::MakeStatic();
  b.AddTorque({0, 100, 0});
  SemiImplicitEuler::Integrate(b, 1.0f / 60.0f);
  REQUIRE(b.angularVelocity.x == Approx(0));
  REQUIRE(b.angularVelocity.y == Approx(0));
  REQUIRE(b.angularVelocity.z == Approx(0));
}

TEST_CASE("SemiImplicitEuler: torque changes angular velocity", "[integration][angular]") {
  RigidBody b = RigidBody::MakeSphere(1.0f, 1.0f);
  // I = (2/5)*m*r^2 = 0.4 → invI = 2.5
  b.angularVelocity = Vec3::Zero();
  b.AddTorque({0, 1, 0}); // torque along Y

  const float DT = 1.0f / 60.0f;
  SemiImplicitEuler::Integrate(b, DT);

  // angAccel = invI * torque = 2.5 * (0,1,0)
  // angVel after one step ≈ 2.5 * DT
  float expectedW = 2.5f * DT * (1.0f - b.angularDamping * DT);
  REQUIRE(b.angularVelocity.y == Approx(expectedW).epsilon(1e-4f));
}

TEST_CASE("SemiImplicitEuler: angular velocity rotates orientation", "[integration][angular]") {
  RigidBody b = RigidBody::MakeSphere(1.0f, 1.0f);
  b.angularVelocity = {0, 1, 0}; // spinning around Y
  b.orientation = Quaternion::Identity();

  const float DT = 1.0f / 60.0f;
  SemiImplicitEuler::Integrate(b, DT);

  // Orientation should no longer be identity
  Quaternion id = Quaternion::Identity();
  float dot = Quaternion::Dot(b.orientation, id);
  // dot < 1 means orientation has changed
  REQUIRE(dot < 1.0f);
  // Must remain normalized
  REQUIRE(b.orientation.Length() == Approx(1.0f).epsilon(1e-5f));
}

TEST_CASE("SemiImplicitEuler: orientation stays normalized under many steps", "[integration][angular]") {
  RigidBody b = RigidBody::MakeSphere(0.5f, 2.0f);
  b.angularVelocity = {1, 2, 3};
  b.orientation = Quaternion::Identity();

  const float DT = 1.0f / 120.0f;
  for (int i = 0; i < 600; i++)
    SemiImplicitEuler::Integrate(b, DT);

  REQUIRE(b.orientation.Length() == Approx(1.0f).epsilon(1e-4f));
}

TEST_CASE("SemiImplicitEuler: linear damping reduces speed", "[integration][linear]") {
  RigidBody b = RigidBody::MakeSphere(1.0f, 1.0f);
  b.velocity = {10, 0, 0};
  b.linearDamping = 0.1f;

  const float DT = 1.0f / 60.0f;
  SemiImplicitEuler::Integrate(b, DT);

  // Damping should reduce linear speed for this step.
  float expected = 10.0f * (1.0f - 0.1f * DT);
  REQUIRE(b.velocity.x == Approx(expected).epsilon(1e-4f));
}

TEST_CASE("SemiImplicitEuler: angular damping reduces spin", "[integration][angular]") {
  RigidBody b = RigidBody::MakeSphere(1.0f, 1.0f);
  b.angularVelocity = {0, 10, 0};
  b.angularDamping = 0.1f;

  const float DT = 1.0f / 60.0f;
  SemiImplicitEuler::Integrate(b, DT);

  float expected = 10.0f * (1.0f - 0.1f * DT);
  REQUIRE(b.angularVelocity.y == Approx(expected).epsilon(1e-3f));
}

// ============================================================
// RigidBody — additional factory / inertia edge cases
// ============================================================

TEST_CASE("RigidBody::UpdateWorldInertia is callable without crash", "[rigidbody]") {
  auto b = RigidBody::MakeSphere(1.0f, 1.0f);
  // Currently a no-op / stub — just ensure it doesn't crash
  REQUIRE_NOTHROW(b.UpdateWorldInertia());
}

TEST_CASE("RigidBody SetSphereInertia on static body gives zero tensor", "[rigidbody]") {
  RigidBody b = RigidBody::MakeStatic();
  b.SetSphereInertia(2.0f); // invMass == 0 → zero tensor
  for (int r = 0; r < 3; r++)
    for (int c = 0; c < 3; c++)
      REQUIRE(b.inertiaTensorInv.m[r][c] == Approx(0).margin(1e-6f));
}

TEST_CASE("RigidBody SetBoxInertia on static body gives zero tensor", "[rigidbody]") {
  RigidBody b = RigidBody::MakeStatic();
  b.SetBoxInertia({1, 1, 1}); // invMass == 0 → zero tensor
  for (int r = 0; r < 3; r++)
    for (int c = 0; c < 3; c++)
      REQUIRE(b.inertiaTensorInv.m[r][c] == Approx(0).margin(1e-6f));
}

TEST_CASE("RigidBody SetMass to zero makes it static", "[rigidbody]") {
  auto b = RigidBody::MakeSphere(1.0f, 2.0f);
  b.SetMass(0.0f);
  REQUIRE(b.invMass == Approx(0.0f));
  REQUIRE(b.IsStatic());
}
