#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "math/MathUtils.h"
#include "math/Vec3.h"
#include "physics/BoxCollider.h"
#include "physics/PhysicsWorld.h"
#include "physics/RigidBody.h"
#include "physics/SphereCollider.h"
#include "physics/broadphase/BruteForce.h"
#include "physics/narrowphase/ContactManifold.h"
#include "physics/narrowphase/SAT.h"

using namespace Horo;
using Catch::Approx;

// ============================================================
// RigidBody construction (extended)
// ============================================================

TEST_CASE("RigidBody MakeBox sets mass, invMass and box collider", "[physics][rigidbody]") {
  Vec3 half{1.0f, 0.5f, 2.0f};
  auto body = RigidBody::MakeBox(half, 4.0f);

  REQUIRE(body.mass == Approx(4.0f));
  REQUIRE(body.invMass == Approx(0.25f));
  REQUIRE(body.collider != nullptr);
  REQUIRE(body.collider->type == ColliderType::Box);

  const auto *bc = static_cast<const BoxCollider *>(body.collider.get());
  REQUIRE(bc->halfExtents.x == Approx(1.0f));
  REQUIRE(bc->halfExtents.y == Approx(0.5f));
  REQUIRE(bc->halfExtents.z == Approx(2.0f));
}

TEST_CASE("RigidBody box inertia tensor is diagonal and positive", "[physics][rigidbody]") {
  Vec3 half{1.0f, 1.0f, 1.0f};
  auto body = RigidBody::MakeBox(half, 1.0f);
  // For this symmetric box, inverse inertia should be equal on all axes.
  float expectedI = 2.0f / 3.0f;
  float expectedInvI = 1.0f / expectedI; // 1.5

  REQUIRE(body.inertiaTensorInv.m[0][0] == Approx(expectedInvI).epsilon(1e-4f));
  REQUIRE(body.inertiaTensorInv.m[1][1] == Approx(expectedInvI).epsilon(1e-4f));
  REQUIRE(body.inertiaTensorInv.m[2][2] == Approx(expectedInvI).epsilon(1e-4f));
  // Off-diagonals must be zero
  REQUIRE(body.inertiaTensorInv.m[0][1] == Approx(0).margin(1e-6f));
  REQUIRE(body.inertiaTensorInv.m[0][2] == Approx(0).margin(1e-6f));
}

TEST_CASE("RigidBody AddForce accumulates", "[physics][rigidbody]") {
  auto body = RigidBody::MakeSphere(1.0f, 1.0f);
  body.AddForce({1, 0, 0});
  body.AddForce({2, 0, 0});
  REQUIRE(body.forceAccum.x == Approx(3.0f));
  REQUIRE(body.forceAccum.y == Approx(0));
}

TEST_CASE("RigidBody AddTorque accumulates", "[physics][rigidbody]") {
  auto body = RigidBody::MakeSphere(1.0f, 1.0f);
  body.AddTorque({0, 5, 0});
  body.AddTorque({0, 3, 0});
  REQUIRE(body.torqueAccum.y == Approx(8.0f));
}

TEST_CASE("RigidBody ClearForces resets both accumulators", "[physics][rigidbody]") {
  auto body = RigidBody::MakeSphere(1.0f, 1.0f);
  body.AddForce({10, 20, 30});
  body.AddTorque({1, 2, 3});
  body.ClearForces();
  REQUIRE(body.forceAccum.x == Approx(0));
  REQUIRE(body.forceAccum.y == Approx(0));
  REQUIRE(body.forceAccum.z == Approx(0));
  REQUIRE(body.torqueAccum.x == Approx(0));
  REQUIRE(body.torqueAccum.y == Approx(0));
  REQUIRE(body.torqueAccum.z == Approx(0));
}

TEST_CASE("RigidBody AddForceAtPoint creates torque", "[physics][rigidbody]") {
  auto body = RigidBody::MakeSphere(1.0f, 1.0f);
  body.position = Vec3::Zero();
  // A +X force at +Y should generate negative Z torque.
  body.AddForceAtPoint({1, 0, 0}, {0, 1, 0});
  REQUIRE(body.forceAccum.x == Approx(1));
  REQUIRE(body.torqueAccum.z == Approx(-1).epsilon(1e-5f));
}

TEST_CASE("RigidBody SetMass updates invMass", "[physics][rigidbody]") {
  auto body = RigidBody::MakeSphere(1.0f, 1.0f);
  body.SetMass(5.0f);
  REQUIRE(body.mass == Approx(5.0f));
  REQUIRE(body.invMass == Approx(0.2f));
}

TEST_CASE("RigidBody IsStatic returns true only for zero invMass", "[physics][rigidbody]") {
  auto stat = RigidBody::MakeStatic();
  auto dyn = RigidBody::MakeSphere(1.0f, 2.0f);
  REQUIRE(stat.IsStatic());
  REQUIRE_FALSE(dyn.IsStatic());
}

// ============================================================
// ContactManifold
// ============================================================

TEST_CASE("ContactManifold starts empty", "[physics][manifold]") {
  ContactManifold m;
  REQUIRE_FALSE(m.hasContact());
  REQUIRE(m.count == 0);
}

TEST_CASE("ContactManifold AddContact increments count", "[physics][manifold]") {
  ContactManifold m;
  m.AddContact({0, 0, 0}, Vec3::Up(), 0.1f);
  REQUIRE(m.hasContact());
  REQUIRE(m.count == 1);
  REQUIRE(m.contacts[0].penetration == Approx(0.1f));
  REQUIRE(m.contacts[0].normal.y == Approx(1.0f));
}

TEST_CASE("ContactManifold does not exceed MAX_CONTACTS", "[physics][manifold]") {
  ContactManifold m;
  for (int i = 0; i < ContactManifold::MAX_CONTACTS + 5; ++i)
    m.AddContact(Vec3::Zero(), Vec3::Up(), 0.01f);

  REQUIRE(m.count == ContactManifold::MAX_CONTACTS);
}

TEST_CASE("ContactManifold stores all four contacts", "[physics][manifold]") {
  ContactManifold m;
  m.AddContact({1, 0, 0}, Vec3::Up(), 0.1f);
  m.AddContact({2, 0, 0}, Vec3::Up(), 0.2f);
  m.AddContact({3, 0, 0}, Vec3::Up(), 0.3f);
  m.AddContact({4, 0, 0}, Vec3::Up(), 0.4f);

  REQUIRE(m.count == 4);
  REQUIRE(m.contacts[0].point.x == Approx(1));
  REQUIRE(m.contacts[3].point.x == Approx(4));
  REQUIRE(m.contacts[3].penetration == Approx(0.4f));
}

// ============================================================
// SAT (extended)
// ============================================================

TEST_CASE("SAT::TestBoxBox: overlapping boxes along Z axis", "[physics][sat]") {
  RigidBody a = RigidBody::MakeBox({0.5f, 0.5f, 0.5f}, 1.0f);
  RigidBody b = RigidBody::MakeStatic();
  b.collider = std::make_shared<BoxCollider>(Vec3{0.5f, 0.5f, 0.5f});

  a.position = {0.0f, 0.0f, 0.8f};
  b.position = {0.0f, 0.0f, 1.5f};

  ContactManifold m = SAT::TestBoxBox(a, b);
  REQUIRE(m.hasContact());
  REQUIRE(m.contacts[0].penetration > 0.0f);
}

TEST_CASE("SAT::TestBoxBox: clearly separated boxes produce no contact", "[physics][sat]") {
  RigidBody a = RigidBody::MakeBox({0.5f, 0.5f, 0.5f}, 1.0f);
  RigidBody b = RigidBody::MakeStatic();
  b.collider = std::make_shared<BoxCollider>(Vec3{0.5f, 0.5f, 0.5f});

  // Gap of 1 unit between them — clearly separated
  a.position = {0.0f, 0.0f, 0.0f};
  b.position = {2.0f, 0.0f, 0.0f}; // 0.5 + gap + 0.5 < 2.0

  ContactManifold m = SAT::TestBoxBox(a, b);
  REQUIRE_FALSE(m.hasContact());
}

// ============================================================
// Broadphase: BruteForce
// ============================================================

TEST_CASE("BruteForce: empty body list returns no pairs", "[physics][broadphase]") {
  std::vector<RigidBody *> bodies;
  auto pairs = BruteForce::FindOverlappingPairs(bodies);
  REQUIRE(pairs.empty());
}

TEST_CASE("BruteForce: single body returns no pairs", "[physics][broadphase]") {
  RigidBody b = RigidBody::MakeSphere(1.0f, 1.0f);
  std::vector<RigidBody *> bodies{&b};
  auto pairs = BruteForce::FindOverlappingPairs(bodies);
  REQUIRE(pairs.empty());
}

TEST_CASE("BruteForce: two overlapping spheres produce one pair", "[physics][broadphase]") {
  RigidBody a = RigidBody::MakeSphere(1.0f, 1.0f);
  RigidBody b = RigidBody::MakeSphere(1.0f, 1.0f);
  a.position = {0, 0, 0};
  b.position = {1, 0, 0}; // centers 1 unit apart, radii = 1 each → overlap

  std::vector<RigidBody *> bodies{&a, &b};
  auto pairs = BruteForce::FindOverlappingPairs(bodies);
  REQUIRE(pairs.size() == 1);
  REQUIRE(pairs[0].first == 0);
  REQUIRE(pairs[0].second == 1);
}

TEST_CASE("BruteForce: two separated spheres produce no pairs", "[physics][broadphase]") {
  RigidBody a = RigidBody::MakeSphere(0.5f, 1.0f);
  RigidBody b = RigidBody::MakeSphere(0.5f, 1.0f);
  a.position = {0, 0, 0};
  b.position = {10, 0, 0}; // far apart

  std::vector<RigidBody *> bodies{&a, &b};
  auto pairs = BruteForce::FindOverlappingPairs(bodies);
  REQUIRE(pairs.empty());
}

TEST_CASE("BruteForce: three bodies, two overlapping, one not", "[physics][broadphase]") {
  RigidBody a = RigidBody::MakeSphere(1.0f, 1.0f);
  RigidBody b = RigidBody::MakeSphere(1.0f, 1.0f);
  RigidBody c = RigidBody::MakeSphere(1.0f, 1.0f);
  a.position = {0, 0, 0};
  b.position = {1, 0, 0};  // overlaps a
  c.position = {50, 0, 0}; // far from both

  std::vector<RigidBody *> bodies{&a, &b, &c};
  auto pairs = BruteForce::FindOverlappingPairs(bodies);
  REQUIRE(pairs.size() == 1);
}

TEST_CASE("BruteForce: all pairs returned when all overlap", "[physics][broadphase]") {
  RigidBody a = RigidBody::MakeSphere(5.0f, 1.0f);
  RigidBody b = RigidBody::MakeSphere(5.0f, 1.0f);
  RigidBody c = RigidBody::MakeSphere(5.0f, 1.0f);
  a.position = {0, 0, 0};
  b.position = {1, 0, 0};
  c.position = {2, 0, 0};

  std::vector<RigidBody *> bodies{&a, &b, &c};
  auto pairs = BruteForce::FindOverlappingPairs(bodies);
  // 3 bodies → 3 possible pairs: (0,1), (0,2), (1,2)
  REQUIRE(pairs.size() == 3);
}

// ============================================================
// PhysicsWorld
// ============================================================

TEST_CASE("PhysicsWorld AddBody returns non-null pointer", "[physics][world]") {
  PhysicsWorld world;
  RigidBody *ptr = world.AddBody(RigidBody::MakeSphere(0.5f, 1.0f));
  REQUIRE(ptr != nullptr);
  REQUIRE(world.GetBodies().size() == 1);
}

TEST_CASE("PhysicsWorld AddBody multiple bodies increases count", "[physics][world]") {
  PhysicsWorld world;
  world.AddBody(RigidBody::MakeSphere(0.5f, 1.0f));
  world.AddBody(RigidBody::MakeSphere(0.5f, 2.0f));
  world.AddBody(RigidBody::MakeStatic());
  REQUIRE(world.GetBodies().size() == 3);
}

TEST_CASE("PhysicsWorld RemoveBody decrements count", "[physics][world]") {
  PhysicsWorld world;
  const RigidBody *a = world.AddBody(RigidBody::MakeSphere(0.5f, 1.0f));
  world.AddBody(RigidBody::MakeSphere(0.5f, 1.0f));
  REQUIRE(world.GetBodies().size() == 2);
  world.RemoveBody(a);
  REQUIRE(world.GetBodies().size() == 1);
}

TEST_CASE("PhysicsWorld Clear removes all bodies", "[physics][world]") {
  PhysicsWorld world;
  world.AddBody(RigidBody::MakeSphere(0.5f, 1.0f));
  world.AddBody(RigidBody::MakeSphere(0.5f, 2.0f));
  world.Clear();
  REQUIRE(world.GetBodies().empty());
}

TEST_CASE("PhysicsWorld Step: dynamic body accelerates under gravity", "[physics][world]") {
  PhysicsWorld world;
  RigidBody *body = world.AddBody(RigidBody::MakeSphere(0.5f, 1.0f));
  body->position = {0, 10, 0};
  body->velocity = Vec3::Zero();
  body->linearDamping = 0.0f;

  const float DT = 1.0f / 120.0f;
  world.Step(DT);

  // After one step, gravity should have pulled it down
  REQUIRE(body->velocity.y < 0.0f);
  REQUIRE(body->position.y < 10.0f);
}

TEST_CASE("PhysicsWorld Step: static body is not moved by gravity", "[physics][world]") {
  PhysicsWorld world;
  RigidBody *body = world.AddBody(RigidBody::MakeStatic());
  body->position = {0, 5, 0};

  world.Step(1.0f / 120.0f);

  REQUIRE(body->position.y == Approx(5.0f));
  REQUIRE(body->velocity.y == Approx(0.0f));
}

TEST_CASE("PhysicsWorld gravity field is configurable", "[physics][world]") {
  PhysicsWorld world;
  world.SetGravity({0, -20.0f, 0});
  RigidBody *body = world.AddBody(RigidBody::MakeSphere(0.5f, 1.0f));
  body->velocity = Vec3::Zero();
  body->linearDamping = 0.0f;

  const float DT = 1.0f / 120.0f;
  world.Step(DT);

  // With stronger gravity, velocity should be more negative than standard -9.81
  REQUIRE(body->velocity.y < -9.81f * DT);
}
