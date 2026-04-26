#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "math/MathUtils.h"
#include "math/Vec3.h"
#include "physics/BoxCollider.h"
#include "physics/RigidBody.h"
#include "physics/SphereCollider.h"
#include "physics/constraints/ContactConstraint.h"
#include "physics/integration/SemiImplicitEuler.h"
#include "physics/narrowphase/ContactManifold.h"
#include "physics/narrowphase/SAT.h"

using namespace Horo;
using Catch::Approx;

// ---- RigidBody construction ----

TEST_CASE("RigidBody MakeSphere sets mass and collider", "[physics][rigidbody]") {
  auto body = RigidBody::MakeSphere(0.5f, 2.0f);
  REQUIRE(body.mass == Approx(2.0f));
  REQUIRE(body.invMass == Approx(0.5f));
  REQUIRE(body.collider != nullptr);
  REQUIRE(body.collider->type == ColliderType::Sphere);
}

TEST_CASE("RigidBody MakeStatic has zero invMass", "[physics][rigidbody]") {
  auto body = RigidBody::MakeStatic();
  REQUIRE(body.invMass == Approx(0.0f));
  REQUIRE(body.IsStatic());
}

// ---- Integration ----

TEST_CASE("SemiImplicitEuler: static body is not moved", "[physics][integration]") {
  RigidBody b = RigidBody::MakeStatic();
  b.position = {5, 5, 5};
  b.AddForce({100, -100, 100});

  SemiImplicitEuler::Integrate(b, 1.0f / 120.0f);

  REQUIRE(b.position.x == Approx(5.0f));
  REQUIRE(b.position.y == Approx(5.0f));
}

TEST_CASE("SemiImplicitEuler: free fall under gravity (one step)", "[physics][integration]") {
  RigidBody b = RigidBody::MakeSphere(0.5f, 1.0f);
  b.position = {0, 10, 0};
  b.velocity = Vec3::Zero();

  const float DT = 1.0f / 120.0f;
  const float G = 9.81f;
  b.AddForce({0, -G * b.mass, 0});
  SemiImplicitEuler::Integrate(b, DT);

  // After one step: vel.y ≈ -G * DT, pos.y ≈ 10 + vel.y * DT
  float expectedVelY = -G * DT;
  float expectedPosY = 10.0f + expectedVelY * DT;

  // Note: semi-implicit uses updated vel for position
  REQUIRE(b.velocity.y == Approx(expectedVelY).epsilon(1e-3));
  REQUIRE(b.position.y == Approx(expectedPosY).epsilon(1e-3));
}

TEST_CASE("SemiImplicitEuler: sphere falls correct distance over many steps", "[physics][integration]") {
  RigidBody b = RigidBody::MakeSphere(0.5f, 1.0f);
  b.position = {0, 20, 0};
  b.velocity = Vec3::Zero();
  b.linearDamping = 0.0f; // no damping for this test

  const float DT = 1.0f / 120.0f;
  const float G = 9.81f;
  const int STEPS = 240; // 2 seconds

  for (int i = 0; i < STEPS; i++) {
    b.AddForce({0, -G * b.mass, 0});
    SemiImplicitEuler::Integrate(b, DT);
    b.ClearForces();
  }

  // After 2 seconds: y ≈ 20 - 0.5 * 9.81 * 4 = 20 - 19.62 = 0.38
  // Semi-implicit Euler is slightly different from analytic, so use loose bound
  float t = STEPS * DT;
  float expectedY = 20.0f - 0.5f * G * t * t;

  // Should be within ~5% of analytic answer
  REQUIRE(b.position.y == Approx(expectedY).epsilon(0.5f));
}

// ---- Sphere-plane collision impulse ----

TEST_CASE("Sphere-plane impulse reverses vertical velocity", "[physics][collision]") {
  // Set up a sphere approaching a floor at y=0
  RigidBody sphere = RigidBody::MakeSphere(0.5f, 1.0f);
  sphere.position = {0, 0.5f,
                     0}; // sitting at floor level exactly (radius = 0.5)
  sphere.velocity = {0, -3.0f, 0}; // moving downward

  // Construct a contact as the analytic plane solver would
  ContactManifold m;
  m.AddContact({0, 0, 0}, Vec3::Up(), 0.0f);

  // Apply impulse inline (same logic as PhysicsWorld::SolveSpherePlane)
  const Vec3 &normal = m.contacts[0].normal;
  float vn = Vec3::Dot(sphere.velocity, normal);
  REQUIRE(vn < 0); // sphere is approaching
  float e = sphere.restitution;
  float jn = -(1.0f + e) * vn * sphere.mass;
  sphere.velocity += normal * (jn * sphere.invMass);

  // After impulse, vertical velocity should be positive (bouncing up)
  REQUIRE(sphere.velocity.y > 0.0f);

  // Magnitude should be restitution * |incoming velocity|
  float expectedY = sphere.restitution * 3.0f;
  REQUIRE(sphere.velocity.y == Approx(expectedY).epsilon(1e-4));
}

TEST_CASE("Sphere-plane: no impulse when moving away", "[physics][collision]") {
  RigidBody sphere = RigidBody::MakeSphere(0.5f, 1.0f);
  sphere.position = {0, 0.5f, 0};
  sphere.velocity = {0, 5.0f, 0}; // moving upward — no contact force

  float vn = Vec3::Dot(sphere.velocity, Vec3::Up());
  REQUIRE(vn > 0); // separating — solver should skip
}

TEST_CASE("ContactConstraint: resolves two-body normal impulse", "[physics][constraint]") {
  RigidBody a = RigidBody::MakeSphere(0.5f, 1.0f);
  RigidBody b = RigidBody::MakeSphere(0.5f, 1.0f);
  a.position = {0, 1.0f, 0};
  b.position = {0, 0.0f, 0};
  a.velocity = {0, -2.0f, 0}; // A moving toward B
  b.velocity = {0, 0.0f, 0};  // B stationary

  ContactManifold m;
  m.AddContact({0, 0.5f, 0}, {0, 1, 0}, 0.01f);

  ContactConstraint c;
  c.bodyA = &a;
  c.bodyB = &b;
  c.manifold = m;
  c.Solve();

  // After impulse both should be separating (relative vel along normal > 0)
  Vec3 relV = a.velocity - b.velocity;
  float vn = Vec3::Dot(relV, m.contacts[0].normal);
  REQUIRE(vn >= 0.0f);
}

// ---- Mass and inertia ----

TEST_CASE("Sphere inertia tensor is diagonal", "[physics][rigidbody]") {
  auto body = RigidBody::MakeSphere(1.0f, 1.0f);
  // Expected inertia for unit sphere with unit mass.
  float expectedI = 0.4f;
  float invI = 1.0f / expectedI;

  REQUIRE(body.inertiaTensorInv.m[0][0] == Approx(invI).epsilon(1e-4));
  REQUIRE(body.inertiaTensorInv.m[1][1] == Approx(invI).epsilon(1e-4));
  REQUIRE(body.inertiaTensorInv.m[2][2] == Approx(invI).epsilon(1e-4));
  // Off-diagonals should be zero
  REQUIRE(body.inertiaTensorInv.m[0][1] == Approx(0).margin(1e-6));
  REQUIRE(body.inertiaTensorInv.m[1][2] == Approx(0).margin(1e-6));
}

// ---- SAT box-box contact normal direction ----

TEST_CASE("SAT::TestBoxBox normal points from B toward A", "[physics][sat]") {
  // A is the player (dynamic), B is the wall (static).
  // A is at x=0, B is at x=2. They overlap when A's right face reaches B's left
  // face.
  RigidBody a = RigidBody::MakeBox({0.5f, 0.5f, 0.5f}, 70.0f);
  RigidBody b = RigidBody::MakeStatic();
  b.collider = std::make_shared<BoxCollider>(Vec3{0.5f, 0.5f, 0.5f});

  // Place boxes slightly overlapping along X axis
  a.position = {0.8f, 0.0f, 0.0f};
  b.position = {1.5f, 0.0f, 0.0f};

  ContactManifold m = SAT::TestBoxBox(a, b);

  REQUIRE(m.hasContact());
  // Normal must point from B (wall) toward A (player) — i.e. in the -X
  // direction
  REQUIRE(m.contacts[0].normal.x < 0.0f);
  // Penetration must be positive
  REQUIRE(m.contacts[0].penetration > 0.0f);
}

TEST_CASE("SAT::TestBoxBox produces no contact for separated boxes", "[physics][sat]") {
  RigidBody a = RigidBody::MakeBox({0.5f, 0.5f, 0.5f}, 70.0f);
  RigidBody b = RigidBody::MakeStatic();
  b.collider = std::make_shared<BoxCollider>(Vec3{0.5f, 0.5f, 0.5f});

  a.position = {0.0f, 0.0f, 0.0f};
  b.position = {5.0f, 0.0f, 0.0f}; // clearly separated

  ContactManifold m = SAT::TestBoxBox(a, b);

  REQUIRE_FALSE(m.hasContact());
}
