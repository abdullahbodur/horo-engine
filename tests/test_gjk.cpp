#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "math/Vec3.h"
#include "physics/BoxCollider.h"
#include "physics/RigidBody.h"
#include "physics/SphereCollider.h"
#include "physics/narrowphase/GJK.h"

using namespace Monolith;
using Catch::Approx;

// ============================================================
// GJK — sphere-sphere fast path
// ============================================================

TEST_CASE("GJK: two overlapping spheres produce contact", "[gjk][sphere]") {
  RigidBody a = RigidBody::MakeSphere(1.0f, 1.0f);
  RigidBody b = RigidBody::MakeSphere(1.0f, 1.0f);
  a.position = {0, 0, 0};
  b.position = {1.5f, 0, 0}; // radii sum = 2, dist = 1.5 → overlap

  ContactManifold m = GJK::Test(a, b);
  REQUIRE(m.hasContact());
  REQUIRE(m.count == 1);
  REQUIRE(m.contacts[0].penetration == Approx(0.5f).epsilon(1e-4f));
}

TEST_CASE("GJK: separated spheres produce no contact", "[gjk][sphere]") {
  RigidBody a = RigidBody::MakeSphere(0.5f, 1.0f);
  RigidBody b = RigidBody::MakeSphere(0.5f, 1.0f);
  a.position = {0, 0, 0};
  b.position = {5, 0, 0}; // clearly separated

  ContactManifold m = GJK::Test(a, b);
  REQUIRE_FALSE(m.hasContact());
}

TEST_CASE("GJK: touching spheres (distance == sum of radii) produce no contact", "[gjk][sphere]") {
  RigidBody a = RigidBody::MakeSphere(1.0f, 1.0f);
  RigidBody b = RigidBody::MakeSphere(1.0f, 1.0f);
  a.position = {0, 0, 0};
  b.position = {2.0f, 0, 0}; // exactly touching

  ContactManifold m = GJK::Test(a, b);
  REQUIRE_FALSE(m.hasContact()); // dist == sumR, not strictly less
}

TEST_CASE("GJK: contact normal points from B toward A", "[gjk][sphere]") {
  RigidBody a = RigidBody::MakeSphere(1.0f, 1.0f);
  RigidBody b = RigidBody::MakeSphere(1.0f, 1.0f);
  a.position = {0, 0, 0};
  b.position = {1.0f, 0, 0}; // A is to the left of B

  ContactManifold m = GJK::Test(a, b);
  REQUIRE(m.hasContact());
  // Normal should point from B toward A: negative x direction
  REQUIRE(m.contacts[0].normal.x < 0.0f);
}

TEST_CASE("GJK: co-located spheres use Up as fallback normal", "[gjk][sphere]") {
  RigidBody a = RigidBody::MakeSphere(1.0f, 1.0f);
  RigidBody b = RigidBody::MakeSphere(1.0f, 1.0f);
  a.position = {0, 0, 0};
  b.position = {0, 0, 0}; // same position

  ContactManifold m = GJK::Test(a, b);
  REQUIRE(m.hasContact());
  REQUIRE(m.contacts[0].normal.y == Approx(1.0f)); // fallback to Up
}

TEST_CASE("GJK: missing collider on A returns empty", "[gjk]") {
  RigidBody a = RigidBody::MakeStatic(); // no collider set
  RigidBody b = RigidBody::MakeSphere(1.0f, 1.0f);
  a.position = {0, 0, 0};
  b.position = {0.5f, 0, 0};

  ContactManifold m = GJK::Test(a, b);
  REQUIRE_FALSE(m.hasContact());
}

TEST_CASE("GJK: missing collider on B returns empty", "[gjk]") {
  RigidBody a = RigidBody::MakeSphere(1.0f, 1.0f);
  RigidBody b = RigidBody::MakeStatic(); // no collider set
  a.position = {0, 0, 0};
  b.position = {0.5f, 0, 0};

  ContactManifold m = GJK::Test(a, b);
  REQUIRE_FALSE(m.hasContact());
}

TEST_CASE("GJK: sphere vs box returns empty (not handled yet)", "[gjk]") {
  RigidBody a = RigidBody::MakeSphere(1.0f, 1.0f);
  RigidBody b = RigidBody::MakeBox({0.5f, 0.5f, 0.5f}, 1.0f);
  a.position = {0, 0, 0};
  b.position = {0.5f, 0, 0};

  // GJK stub only handles sphere-sphere; box-sphere returns no contact
  ContactManifold m = GJK::Test(a, b);
  REQUIRE_FALSE(m.hasContact());
}

TEST_CASE("GJK: contact point lies on surface of sphere B", "[gjk][sphere]") {
  RigidBody a = RigidBody::MakeSphere(1.0f, 1.0f);
  RigidBody b = RigidBody::MakeSphere(1.0f, 1.0f);
  a.position = {0, 0, 0};
  b.position = {1.0f, 0, 0};

  ContactManifold m = GJK::Test(a, b);
  REQUIRE(m.hasContact());
  // Contact point should land at the origin for this setup.
  REQUIRE(m.contacts[0].point.x == Approx(0.0f).margin(1e-4f));
  REQUIRE(m.contacts[0].point.y == Approx(0.0f).margin(1e-4f));
  REQUIRE(m.contacts[0].point.z == Approx(0.0f).margin(1e-4f));
}
