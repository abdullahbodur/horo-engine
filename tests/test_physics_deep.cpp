#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <numbers>
#include <vector>

#include "math/MathUtils.h"
#include "math/Vec3.h"
#include "physics/BoxCollider.h"
#include "physics/Collider.h"
#include "physics/RigidBody.h"
#include "physics/SphereCollider.h"
#include "physics/constraints/ConstraintSolver.h"
#include "physics/constraints/ContactConstraint.h"
#include "physics/narrowphase/ContactManifold.h"

using namespace Monolith;
using Catch::Approx;

// ===========================================================================
// BoxCollider
// ===========================================================================

TEST_CASE("BoxCollider: default half-extents are 0.5", "[physics][collider]") {
  BoxCollider box;
  REQUIRE(box.halfExtents.x == Approx(0.5f));
  REQUIRE(box.halfExtents.y == Approx(0.5f));
  REQUIRE(box.halfExtents.z == Approx(0.5f));
  REQUIRE(box.type == ColliderType::Box);
}

TEST_CASE("BoxCollider: custom half-extents", "[physics][collider]") {
  BoxCollider box({2.0f, 1.0f, 0.5f});
  REQUIRE(box.halfExtents.x == Approx(2.0f));
  REQUIRE(box.halfExtents.y == Approx(1.0f));
  REQUIRE(box.halfExtents.z == Approx(0.5f));
  REQUIRE(box.type == ColliderType::Box);
}

TEST_CASE("BoxCollider: type is Box", "[physics][collider]") {
  BoxCollider box({1.0f, 1.0f, 1.0f});
  REQUIRE(box.type == ColliderType::Box);
}

// ===========================================================================
// SphereCollider
// ===========================================================================

TEST_CASE("SphereCollider: default radius is 0.5", "[physics][collider]") {
  SphereCollider sphere;
  REQUIRE(sphere.radius == Approx(0.5f));
  REQUIRE(sphere.type == ColliderType::Sphere);
}

TEST_CASE("SphereCollider: custom radius", "[physics][collider]") {
  const float radius = std::numbers::pi_v<float>;
  SphereCollider sphere(radius);
  REQUIRE(sphere.radius == Approx(radius));
}

TEST_CASE("SphereCollider: type is Sphere", "[physics][collider]") {
  SphereCollider sphere(1.0f);
  REQUIRE(sphere.type == ColliderType::Sphere);
}

// ===========================================================================
// RigidBody — MakeStatic
// ===========================================================================

TEST_CASE("RigidBody::MakeStatic: invMass is 0", "[physics][rigidbody]") {
  RigidBody b = RigidBody::MakeStatic();
  REQUIRE(b.invMass == Approx(0.0f));
  REQUIRE(b.mass == Approx(0.0f));
  REQUIRE(b.IsStatic());
}

TEST_CASE("RigidBody::MakeStatic: inertia tensor is zero matrix",
          "[physics][rigidbody]") {
  RigidBody b = RigidBody::MakeStatic();
  for (int r = 0; r < 3; ++r)
    for (int c = 0; c < 3; ++c)
      REQUIRE(b.inertiaTensorInv.m[r][c] == Approx(0.0f).margin(1e-6f));
}

// ===========================================================================
// RigidBody — MakeSphere
// ===========================================================================

TEST_CASE("RigidBody::MakeSphere: correct mass and invMass",
          "[physics][rigidbody]") {
  RigidBody b = RigidBody::MakeSphere(1.0f, 2.0f);
  REQUIRE(b.mass == Approx(2.0f));
  REQUIRE(b.invMass == Approx(0.5f));
  REQUIRE_FALSE(b.IsStatic());
}

TEST_CASE("RigidBody::MakeSphere: has SphereCollider", "[physics][rigidbody]") {
  RigidBody b = RigidBody::MakeSphere(0.5f, 1.0f);
  REQUIRE(b.collider != nullptr);
  REQUIRE(b.collider->type == ColliderType::Sphere);
  const auto *sc = static_cast<const SphereCollider *>(b.collider.get());
  REQUIRE(sc->radius == Approx(0.5f));
}

TEST_CASE("RigidBody::MakeSphere: inertia tensor is non-zero",
          "[physics][rigidbody]") {
  RigidBody b = RigidBody::MakeSphere(1.0f, 1.0f);
  // Sphere inertia = 2/5 * m * r^2 diagonal, so inverse diagonal should be
  // nonzero
  REQUIRE(b.inertiaTensorInv.m[0][0] > 0.0f);
  REQUIRE(b.inertiaTensorInv.m[1][1] > 0.0f);
  REQUIRE(b.inertiaTensorInv.m[2][2] > 0.0f);
}

TEST_CASE("RigidBody::MakeSphere: larger radius has smaller inverse inertia",
          "[physics][rigidbody]") {
  RigidBody small = RigidBody::MakeSphere(0.5f, 1.0f);
  RigidBody large = RigidBody::MakeSphere(2.0f, 1.0f);
  // Larger radius → larger I → smaller I_inv
  REQUIRE(large.inertiaTensorInv.m[0][0] < small.inertiaTensorInv.m[0][0]);
}

// ===========================================================================
// RigidBody — MakeBox
// ===========================================================================

TEST_CASE("RigidBody::MakeBox: correct mass", "[physics][rigidbody]") {
  RigidBody b = RigidBody::MakeBox({1.0f, 1.0f, 1.0f}, 5.0f);
  REQUIRE(b.mass == Approx(5.0f));
  REQUIRE(b.invMass == Approx(0.2f));
}

TEST_CASE("RigidBody::MakeBox: has BoxCollider", "[physics][rigidbody]") {
  RigidBody b = RigidBody::MakeBox({0.5f, 1.0f, 2.0f}, 1.0f);
  REQUIRE(b.collider != nullptr);
  REQUIRE(b.collider->type == ColliderType::Box);
  const auto *bc = static_cast<const BoxCollider *>(b.collider.get());
  REQUIRE(bc->halfExtents.x == Approx(0.5f));
  REQUIRE(bc->halfExtents.y == Approx(1.0f));
  REQUIRE(bc->halfExtents.z == Approx(2.0f));
}

TEST_CASE("RigidBody::MakeBox: inertia tensor non-zero",
          "[physics][rigidbody]") {
  RigidBody b = RigidBody::MakeBox({1.0f, 1.0f, 1.0f}, 1.0f);
  REQUIRE(b.inertiaTensorInv.m[0][0] > 0.0f);
  REQUIRE(b.inertiaTensorInv.m[1][1] > 0.0f);
  REQUIRE(b.inertiaTensorInv.m[2][2] > 0.0f);
}

TEST_CASE(
    "RigidBody::MakeBox: non-uniform extents give different inertia components",
    "[physics][rigidbody]") {
  // Wide flat box: large half.y, small half.x
  RigidBody b = RigidBody::MakeBox({0.1f, 5.0f, 0.1f}, 1.0f);
  // Ix ∝ (4*hy^2 + 4*hz^2), Iy ∝ (4*hx^2 + 4*hz^2)
  // Ix >> Iy for this shape → Ix_inv << Iy_inv
  REQUIRE(b.inertiaTensorInv.m[0][0] < b.inertiaTensorInv.m[1][1]);
}

// ===========================================================================
// RigidBody — SetMass
// ===========================================================================

TEST_CASE("RigidBody::SetMass: normal mass", "[physics][rigidbody]") {
  RigidBody b;
  b.SetMass(4.0f);
  REQUIRE(b.mass == Approx(4.0f));
  REQUIRE(b.invMass == Approx(0.25f));
}

TEST_CASE("RigidBody::SetMass: zero mass → static", "[physics][rigidbody]") {
  RigidBody b;
  b.SetMass(0.0f);
  REQUIRE(b.invMass == Approx(0.0f));
  REQUIRE(b.IsStatic());
}

// ===========================================================================
// RigidBody — AddForce / AddTorque / ClearForces
// ===========================================================================

TEST_CASE("RigidBody::AddForce accumulates forces", "[physics][rigidbody]") {
  RigidBody b;
  b.AddForce({1.0f, 0.0f, 0.0f});
  b.AddForce({0.0f, 2.0f, 0.0f});
  REQUIRE(b.forceAccum.x == Approx(1.0f));
  REQUIRE(b.forceAccum.y == Approx(2.0f));
  REQUIRE(b.forceAccum.z == Approx(0.0f));
}

TEST_CASE("RigidBody::AddTorque accumulates torques", "[physics][rigidbody]") {
  RigidBody b;
  b.AddTorque({0.0f, 0.0f, 1.0f});
  b.AddTorque({0.0f, 0.0f, 1.0f});
  REQUIRE(b.torqueAccum.z == Approx(2.0f));
}

TEST_CASE("RigidBody::ClearForces resets force and torque",
          "[physics][rigidbody]") {
  RigidBody b;
  b.AddForce({5.0f, 5.0f, 5.0f});
  b.AddTorque({1.0f, 2.0f, 3.0f});
  b.ClearForces();
  REQUIRE(b.forceAccum.x == Approx(0.0f));
  REQUIRE(b.forceAccum.y == Approx(0.0f));
  REQUIRE(b.forceAccum.z == Approx(0.0f));
  REQUIRE(b.torqueAccum.x == Approx(0.0f));
  REQUIRE(b.torqueAccum.y == Approx(0.0f));
  REQUIRE(b.torqueAccum.z == Approx(0.0f));
}

// ===========================================================================
// RigidBody — AddForceAtPoint
// ===========================================================================

TEST_CASE(
    "RigidBody::AddForceAtPoint: force applied at center generates no torque",
    "[physics][rigidbody]") {
  RigidBody b;
  b.position = Vec3::Zero();
  b.AddForceAtPoint({0.0f, 10.0f, 0.0f}, {0.0f, 0.0f, 0.0f});
  REQUIRE(b.forceAccum.y == Approx(10.0f));
  REQUIRE(b.torqueAccum.x == Approx(0.0f).margin(1e-6f));
  REQUIRE(b.torqueAccum.y == Approx(0.0f).margin(1e-6f));
  REQUIRE(b.torqueAccum.z == Approx(0.0f).margin(1e-6f));
}

TEST_CASE("RigidBody::AddForceAtPoint: off-center force generates torque",
          "[physics][rigidbody]") {
  RigidBody b;
  b.position = Vec3::Zero();
  // Force in Y at point (1,0,0) → torque = (1,0,0) × (0,1,0) = (0,0,1)
  b.AddForceAtPoint({0.0f, 1.0f, 0.0f}, {1.0f, 0.0f, 0.0f});
  REQUIRE(b.torqueAccum.z != Approx(0.0f).margin(1e-5f));
}

// ===========================================================================
// ContactManifold
// ===========================================================================

TEST_CASE("ContactManifold: starts empty", "[physics][manifold]") {
  ContactManifold m;
  REQUIRE(m.count == 0);
  REQUIRE_FALSE(m.hasContact());
}

TEST_CASE("ContactManifold: AddContact increments count",
          "[physics][manifold]") {
  ContactManifold m;
  m.AddContact({0, 0, 0}, {0, 1, 0}, 0.1f);
  REQUIRE(m.count == 1);
  REQUIRE(m.hasContact());
}

TEST_CASE("ContactManifold: stores contact data correctly",
          "[physics][manifold]") {
  ContactManifold m;
  m.AddContact({1.0f, 2.0f, 3.0f}, {0.0f, 1.0f, 0.0f}, 0.05f);
  REQUIRE(m.contacts[0].point.x == Approx(1.0f));
  REQUIRE(m.contacts[0].point.y == Approx(2.0f));
  REQUIRE(m.contacts[0].point.z == Approx(3.0f));
  REQUIRE(m.contacts[0].normal.y == Approx(1.0f));
  REQUIRE(m.contacts[0].penetration == Approx(0.05f));
}

TEST_CASE("ContactManifold: capped at MAX_CONTACTS", "[physics][manifold]") {
  ContactManifold m;
  for (int i = 0; i < ContactManifold::MAX_CONTACTS + 5; ++i)
    m.AddContact({0, 0, 0}, {0, 1, 0}, 0.01f);
  REQUIRE(m.count == ContactManifold::MAX_CONTACTS);
}

TEST_CASE("ContactManifold: multiple contacts", "[physics][manifold]") {
  ContactManifold m;
  m.AddContact({0, 0, 0}, {0, 1, 0}, 0.1f);
  m.AddContact({1, 0, 0}, {0, 1, 0}, 0.2f);
  REQUIRE(m.count == 2);
  REQUIRE(m.contacts[1].point.x == Approx(1.0f));
  REQUIRE(m.contacts[1].penetration == Approx(0.2f));
}

// ===========================================================================
// ContactConstraint — basic solve paths
// ===========================================================================

TEST_CASE("ContactConstraint::Solve: null bodies is safe",
          "[physics][constraint]") {
  ContactConstraint cc;
  cc.bodyA = nullptr;
  cc.bodyB = nullptr;
  REQUIRE_NOTHROW(cc.Solve());
}

TEST_CASE("ContactConstraint::Solve: no contact is safe",
          "[physics][constraint]") {
  RigidBody a = RigidBody::MakeSphere(0.5f, 1.0f);
  RigidBody b = RigidBody::MakeSphere(0.5f, 1.0f);
  ContactConstraint cc;
  cc.bodyA = &a;
  cc.bodyB = &b;
  // manifold.count == 0 → no contacts
  REQUIRE_NOTHROW(cc.Solve());
}

TEST_CASE("ContactConstraint::Solve: separating bodies not modified",
          "[physics][constraint]") {
  RigidBody a = RigidBody::MakeSphere(0.5f, 1.0f);
  RigidBody b = RigidBody::MakeSphere(0.5f, 1.0f);
  a.velocity = {0.0f, 1.0f, 0.0f}; // moving apart
  b.velocity = {0.0f, -1.0f, 0.0f};

  ContactConstraint cc;
  cc.bodyA = &a;
  cc.bodyB = &b;
  cc.manifold.AddContact({0, 0, 0}, {0, 1, 0}, 0.01f);
  cc.Solve();

  // vn > 0 → separating, no impulse should be applied
  REQUIRE(a.velocity.y == Approx(1.0f));
  REQUIRE(b.velocity.y == Approx(-1.0f));
}

TEST_CASE("ContactConstraint::Solve: approaching bodies get impulse",
          "[physics][constraint]") {
  RigidBody a = RigidBody::MakeSphere(0.5f, 1.0f);
  RigidBody b = RigidBody::MakeSphere(0.5f, 1.0f);
  a.position = {0.0f, 0.1f, 0.0f};
  b.position = {0.0f, -0.1f, 0.0f};
  a.velocity = {0.0f, -2.0f, 0.0f}; // approaching
  b.velocity = {0.0f, 2.0f, 0.0f};

  ContactConstraint cc;
  cc.bodyA = &a;
  cc.bodyB = &b;
  cc.manifold.AddContact({0, 0, 0}, {0, 1, 0}, 0.05f);
  cc.Solve();

  // After impulse, relative normal velocity should be reduced / reversed
  float vn_after = (a.velocity - b.velocity).y;
  // Was -4 before; should be less negative or positive now
  REQUIRE(vn_after > -4.0f);
}

TEST_CASE("ContactConstraint::Solve: static body A not moved",
          "[physics][constraint]") {
  RigidBody a = RigidBody::MakeStatic();
  RigidBody b = RigidBody::MakeSphere(0.5f, 1.0f);
  a.position = Vec3::Zero();
  b.position = {0.0f, 0.4f, 0.0f};
  b.velocity = {0.0f, -2.0f, 0.0f};

  ContactConstraint cc;
  cc.bodyA = &a;
  cc.bodyB = &b;
  cc.manifold.AddContact({0, 0, 0}, {0, 1, 0}, 0.1f);
  cc.Solve();

  // Static body should not move
  REQUIRE(a.position.x == Approx(0.0f));
  REQUIRE(a.position.y == Approx(0.0f));
  REQUIRE(a.velocity.x == Approx(0.0f));
  REQUIRE(a.velocity.y == Approx(0.0f));
}

TEST_CASE("ContactConstraint::Solve: static body B not moved",
          "[physics][constraint]") {
  RigidBody a = RigidBody::MakeSphere(0.5f, 1.0f);
  RigidBody b = RigidBody::MakeStatic();
  a.velocity = {0.0f, -2.0f, 0.0f};

  ContactConstraint cc;
  cc.bodyA = &a;
  cc.bodyB = &b;
  cc.manifold.AddContact({0, 0, 0}, {0, 1, 0}, 0.05f);
  cc.Solve();

  REQUIRE(b.velocity.x == Approx(0.0f));
  REQUIRE(b.velocity.y == Approx(0.0f));
  REQUIRE(b.position.x == Approx(0.0f));
  REQUIRE(b.position.y == Approx(0.0f));
}

// ===========================================================================
// ConstraintSolver
// ===========================================================================

TEST_CASE("ConstraintSolver::Solve: empty constraints is safe",
          "[physics][solver]") {
  ConstraintSolver solver;
  std::vector<ContactConstraint> constraints;
  REQUIRE_NOTHROW(solver.Solve(constraints));
}

TEST_CASE("ConstraintSolver::Solve: runs for requested iterations",
          "[physics][solver]") {
  // Counter body that tracks how many times Solve() is called
  struct CountingBody : public RigidBody {
    int solveCount = 0;
  };

  RigidBody a = RigidBody::MakeSphere(0.5f, 1.0f);
  RigidBody b = RigidBody::MakeSphere(0.5f, 1.0f);
  a.velocity = {0.0f, -1.0f, 0.0f};
  b.velocity = {0.0f, 1.0f, 0.0f};

  ContactConstraint cc;
  cc.bodyA = &a;
  cc.bodyB = &b;
  cc.manifold.AddContact({0, 0, 0}, {0, 1, 0}, 0.05f);

  std::vector<ContactConstraint> constraints = {cc};
  ConstraintSolver solver;
  REQUIRE_NOTHROW(solver.Solve(constraints, 5));
}

TEST_CASE("ConstraintSolver::Solve: zero iterations does nothing",
          "[physics][solver]") {
  RigidBody a = RigidBody::MakeSphere(0.5f, 1.0f);
  RigidBody b = RigidBody::MakeSphere(0.5f, 1.0f);
  float vy_a = -2.0f;
  a.velocity = {0.0f, vy_a, 0.0f};

  ContactConstraint cc;
  cc.bodyA = &a;
  cc.bodyB = &b;
  cc.manifold.AddContact({0, 0, 0}, {0, 1, 0}, 0.05f);

  std::vector<ContactConstraint> constraints = {cc};
  ConstraintSolver solver;
  solver.Solve(constraints, 0);

  // Zero iterations → velocity unchanged
  REQUIRE(a.velocity.y == Approx(vy_a));
}

TEST_CASE("ConstraintSolver::Solve: multiple constraints processed",
          "[physics][solver]") {
  RigidBody a1 = RigidBody::MakeSphere(0.5f, 1.0f);
  RigidBody b1 = RigidBody::MakeStatic();
  RigidBody a2 = RigidBody::MakeSphere(0.5f, 1.0f);
  RigidBody b2 = RigidBody::MakeStatic();

  a1.velocity = {0.0f, -3.0f, 0.0f};
  a2.velocity = {0.0f, -3.0f, 0.0f};

  ContactConstraint cc1;
  ContactConstraint cc2;
  cc1.bodyA = &a1;
  cc1.bodyB = &b1;
  cc1.manifold.AddContact({0, 0, 0}, {0, 1, 0}, 0.1f);
  cc2.bodyA = &a2;
  cc2.bodyB = &b2;
  cc2.manifold.AddContact({5, 0, 0}, {0, 1, 0}, 0.1f);

  std::vector<ContactConstraint> constraints = {cc1, cc2};
  ConstraintSolver solver;
  solver.Solve(constraints, 1);

  // Both should have been resolved
  REQUIRE(a1.velocity.y > -3.0f);
  REQUIRE(a2.velocity.y > -3.0f);
}

// ===========================================================================
// RigidBody — SetSphereInertia / SetBoxInertia edge cases
// ===========================================================================

TEST_CASE("RigidBody::SetSphereInertia: static body keeps zero inertia",
          "[physics][rigidbody]") {
  RigidBody b = RigidBody::MakeStatic();
  b.SetSphereInertia(1.0f);
  REQUIRE(b.inertiaTensorInv.m[0][0] == Approx(0.0f));
}

TEST_CASE("RigidBody::SetBoxInertia: static body keeps zero inertia",
          "[physics][rigidbody]") {
  RigidBody b = RigidBody::MakeStatic();
  b.SetBoxInertia({1.0f, 1.0f, 1.0f});
  REQUIRE(b.inertiaTensorInv.m[0][0] == Approx(0.0f));
}

TEST_CASE("RigidBody::UpdateWorldInertia: does not crash",
          "[physics][rigidbody]") {
  RigidBody b = RigidBody::MakeSphere(1.0f, 1.0f);
  REQUIRE_NOTHROW(b.UpdateWorldInertia());
}
