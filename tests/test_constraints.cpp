#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "math/Vec3.h"
#include "physics/RigidBody.h"
#include "physics/constraints/ConstraintSolver.h"
#include "physics/constraints/ContactConstraint.h"
#include "physics/narrowphase/ContactManifold.h"

using namespace Horo;
using Catch::Approx;

// ============================================================
// ContactConstraint — edge cases
// ============================================================

TEST_CASE("ContactConstraint: null bodyA is a no-op", "[constraint]") {
  ContactConstraint c;
  c.bodyA = nullptr;
  RigidBody b = RigidBody::MakeSphere(1.0f, 1.0f);
  c.bodyB = &b;
  c.manifold.AddContact({0, 0, 0}, Vec3::Up(), 0.1f);
  REQUIRE_NOTHROW(c.Solve());
}

TEST_CASE("ContactConstraint: null bodyB is a no-op", "[constraint]") {
  ContactConstraint c;
  RigidBody a = RigidBody::MakeSphere(1.0f, 1.0f);
  c.bodyA = &a;
  c.bodyB = nullptr;
  c.manifold.AddContact({0, 0, 0}, Vec3::Up(), 0.1f);
  REQUIRE_NOTHROW(c.Solve());
}

TEST_CASE("ContactConstraint: empty manifold is a no-op", "[constraint]") {
  RigidBody a = RigidBody::MakeSphere(1.0f, 1.0f);
  RigidBody b = RigidBody::MakeSphere(1.0f, 1.0f);
  a.velocity = {0, -5, 0};

  ContactConstraint c;
  c.bodyA = &a;
  c.bodyB = &b;
  // no contacts added
  REQUIRE_NOTHROW(c.Solve());
  REQUIRE(a.velocity.y == Approx(-5.0f)); // unchanged
}

TEST_CASE("ContactConstraint: separating bodies are not impuled", "[constraint]") {
  RigidBody a = RigidBody::MakeSphere(1.0f, 1.0f);
  RigidBody b = RigidBody::MakeSphere(1.0f, 1.0f);
  a.position = {0, 1, 0};
  b.position = {0, 0, 0};
  a.velocity = {0, 5, 0}; // moving away from B
  b.velocity = Vec3::Zero();

  ContactManifold m;
  m.AddContact({0, 0.5f, 0}, Vec3::Up(), 0.0f);

  ContactConstraint c;
  c.bodyA = &a;
  c.bodyB = &b;
  c.manifold = m;
  c.Solve();

  // vn > 0 → solver skips, velocities unchanged
  REQUIRE(a.velocity.y == Approx(5.0f));
  REQUIRE(b.velocity.y == Approx(0.0f));
}

TEST_CASE("ContactConstraint: static bodyA is not moved", "[constraint]") {
  // bodyA = static floor at y=0, bodyB = sphere moving DOWN toward A
  // relV = vA - vB = (0,0,0) - (0,-3,0) = (0,3,0)
  // vn = Dot(relV, Up) = 3 > 0  → solver skips (separating in A-relative terms)
  // Instead: bodyB moves INTO bodyA from above — we need vn < 0.
  // bodyA above bodyB: relV = vA - vB, normal points from B→A = Up,
  // so put bodyA at top (static wall), bodyB moving upward into it.
  // Actually easier: keep normal = Up, bodyA static at y=0, bodyB below moving
  // UP.
  RigidBody a = RigidBody::MakeStatic();
  RigidBody b = RigidBody::MakeSphere(1.0f, 1.0f);
  a.position = {0, 0, 0};     // static floor
  b.position = {0, -0.1f, 0}; // below floor
  b.velocity = {0, -3, 0};    // moving downward — away from floor on Up axis

  // Use DOWN as normal so bodyB approaching bodyA (vn < 0)
  ContactManifold m;
  m.AddContact({0, 0, 0}, Vec3::Down(),
               0.1f); // normal points from A toward B = Down

  Vec3 prevA = a.position;
  Vec3 prevVA = a.velocity;

  ContactConstraint c;
  c.bodyA = &a;
  c.bodyB = &b;
  c.manifold = m;
  c.Solve();

  // Static body must not move
  REQUIRE(a.velocity.y == Approx(prevVA.y));
  REQUIRE(a.position.x == Approx(prevA.x));
  // Dynamic body: impulse applied (velocity changed)
  REQUIRE(b.velocity.y > -3.0f); // velocity changed (less negative or positive)
}

TEST_CASE("ContactConstraint: static bodyB is not moved", "[constraint]") {
  RigidBody a = RigidBody::MakeSphere(1.0f, 1.0f);
  RigidBody b = RigidBody::MakeStatic();
  a.position = {0, 1, 0};
  b.position = {0, 0, 0};
  a.velocity = {0, -3, 0};

  ContactManifold m;
  m.AddContact({0, 0.5f, 0}, Vec3::Up(), 0.05f);

  ContactConstraint c;
  c.bodyA = &a;
  c.bodyB = &b;
  c.manifold = m;
  c.Solve();

  REQUIRE(b.velocity.y == Approx(0.0f));
  REQUIRE(b.position.y == Approx(0.0f));
  REQUIRE(a.velocity.y > 0.0f); // bounced up
}

TEST_CASE("ContactConstraint: friction reduces lateral velocity", "[constraint]") {
  RigidBody a = RigidBody::MakeSphere(1.0f, 1.0f);
  RigidBody b = RigidBody::MakeStatic();
  a.position = {0, 0.1f, 0};
  b.position = {0, 0, 0};
  // A sliding laterally into static floor
  a.velocity = {5.0f, -1.0f, 0}; // mostly horizontal, slightly downward
  b.velocity = Vec3::Zero();

  ContactManifold m;
  m.AddContact({0, 0, 0}, Vec3::Up(), 0.05f);

  ContactConstraint c;
  c.bodyA = &a;
  c.bodyB = &b;
  c.manifold = m;
  c.Solve();

  // Horizontal velocity should be reduced by friction (not zero, but less)
  REQUIRE(std::abs(a.velocity.x) < 5.0f);
}

TEST_CASE("ContactConstraint: position correction pushes bodies apart", "[constraint]") {
  RigidBody a = RigidBody::MakeSphere(1.0f, 1.0f);
  RigidBody b = RigidBody::MakeSphere(1.0f, 1.0f);
  a.position = {0, 1, 0};
  b.position = {0, 0, 0};
  a.velocity = {0, -5, 0};
  b.velocity = Vec3::Zero();

  float initialDistY = a.position.y - b.position.y;

  ContactManifold m;
  m.AddContact({0, 0.5f, 0}, Vec3::Up(), 0.2f);

  ContactConstraint c;
  c.bodyA = &a;
  c.bodyB = &b;
  c.manifold = m;
  c.Solve();

  float finalDistY = a.position.y - b.position.y;
  // Position correction should increase the separation
  REQUIRE(finalDistY >= initialDistY);
}

TEST_CASE("ContactConstraint: four contact points all resolved", "[constraint]") {
  RigidBody a = RigidBody::MakeBox({0.5f, 0.5f, 0.5f}, 1.0f);
  RigidBody b = RigidBody::MakeStatic();
  a.position = {0, 0.4f, 0};
  b.position = {0, 0, 0};
  a.velocity = {0, -2, 0};

  ContactManifold m;
  m.AddContact({-0.5f, 0, -0.5f}, Vec3::Up(), 0.1f);
  m.AddContact({0.5f, 0, -0.5f}, Vec3::Up(), 0.1f);
  m.AddContact({-0.5f, 0, 0.5f}, Vec3::Up(), 0.1f);
  m.AddContact({0.5f, 0, 0.5f}, Vec3::Up(), 0.1f);

  ContactConstraint c;
  c.bodyA = &a;
  c.bodyB = &b;
  c.manifold = m;
  REQUIRE_NOTHROW(c.Solve());
  REQUIRE(a.velocity.y > -2.0f); // impulse applied
}

// ============================================================
// ConstraintSolver
// ============================================================

TEST_CASE("ConstraintSolver: empty constraint list is safe", "[solver]") {
  ConstraintSolver solver;
  std::vector<ContactConstraint> empty;
  REQUIRE_NOTHROW(solver.Solve(empty));
}

TEST_CASE("ConstraintSolver: runs multiple iterations", "[solver]") {
  RigidBody a = RigidBody::MakeSphere(1.0f, 1.0f);
  RigidBody b = RigidBody::MakeSphere(1.0f, 1.0f);
  a.position = {0, 1, 0};
  b.position = {0, 0, 0};
  a.velocity = {0, -3, 0};
  b.velocity = Vec3::Zero();

  ContactManifold m;
  m.AddContact({0, 0.5f, 0}, Vec3::Up(), 0.1f);

  ContactConstraint c;
  c.bodyA = &a;
  c.bodyB = &b;
  c.manifold = m;

  std::vector<ContactConstraint> constraints{c};

  ConstraintSolver solver;
  // 10 iterations — should not crash and further converge the solution
  REQUIRE_NOTHROW(solver.Solve(constraints, 10));
}

TEST_CASE("ConstraintSolver: default 1 iteration resolves basic contact", "[solver]") {
  RigidBody a = RigidBody::MakeSphere(1.0f, 1.0f);
  RigidBody b = RigidBody::MakeStatic();
  a.velocity = {0, -4, 0};
  b.velocity = Vec3::Zero();

  ContactManifold m;
  m.AddContact(Vec3::Zero(), Vec3::Up(), 0.05f);

  ContactConstraint c;
  c.bodyA = &a;
  c.bodyB = &b;
  c.manifold = m;

  std::vector<ContactConstraint> constraints{c};
  ConstraintSolver solver;
  solver.Solve(constraints);

  REQUIRE(a.velocity.y > 0.0f);
}

TEST_CASE("ContactConstraint: tangential velocity triggers friction angular impulse", "[constraint]") {
  // Both bodies dynamic, sliding laterally while approaching along the contact
  // normal — ensures jn > 0 so friction clamp is non-trivial.
  RigidBody a = RigidBody::MakeSphere(1.0f, 1.0f);
  RigidBody b = RigidBody::MakeSphere(1.0f, 1.0f);

  a.position = {0.0f, 0.5f, 0.0f};
  b.position = {0.0f, -0.5f, 0.0f};

  // Lateral X velocity + approaching Y velocity (a moves down toward b)
  a.velocity = {3.0f, -1.0f, 0.0f};
  b.velocity = {0.0f, 0.0f, 0.0f};

  ContactManifold m;
  m.AddContact({0.0f, 0.0f, 0.0f}, Vec3::Up(), 0.1f);

  ContactConstraint c;
  c.bodyA = &a;
  c.bodyB = &b;
  c.manifold = m;

  REQUIRE_NOTHROW(c.Solve());

  // After friction impulse bodyA lateral velocity should be reduced
  REQUIRE(a.velocity.x < 3.0f);
}

TEST_CASE("ContactConstraint: offset contact point generates friction torque", "[constraint]") {
  RigidBody a = RigidBody::MakeSphere(1.0f, 1.0f);
  RigidBody b = RigidBody::MakeSphere(1.0f, 1.0f);

  a.position = {0.0f, 1.0f, 0.0f};
  b.position = {0.0f, 0.0f, 0.0f};
  // Lateral X velocity + approaching Y velocity so jn > 0 and friction fires
  a.velocity = {5.0f, -1.0f, 0.0f};

  // Offset contact from both body centers so rA and rB are non-zero,
  // ensuring Vec3::Cross(rA, impulseT) is non-zero.
  ContactManifold m;
  m.AddContact({0.3f, 0.5f, 0.0f}, Vec3::Up(), 0.05f);

  ContactConstraint c;
  c.bodyA = &a;
  c.bodyB = &b;
  c.manifold = m;

  REQUIRE_NOTHROW(c.Solve());
  // Angular velocity should be non-zero after offset contact friction
  const float angA = a.angularVelocity.Length();
  const float angB = b.angularVelocity.Length();
  REQUIRE((angA > 0.0f || angB > 0.0f));
}
