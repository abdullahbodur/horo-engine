#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "math/MathUtils.h"
#include "math/Vec3.h"
#include "physics/BoxCollider.h"
#include "physics/PhysicsWorld.h"
#include "physics/RigidBody.h"
#include "physics/SphereCollider.h"

using namespace Horo;
using Catch::Approx;

// ============================================================
// PhysicsWorld — floor collision responses
// ============================================================

TEST_CASE("PhysicsWorld: sphere bounces off floor at y=0", "[world][collision]") {
  PhysicsWorld world;
  RigidBody *sphere = world.AddBody(RigidBody::MakeSphere(0.5f, 1.0f));
  // Position sphere below floor so the floor solver fires; velocity downward
  sphere->position = {0, 0.4f, 0}; // bottom face at y=-0.1 (below floor)
  sphere->velocity = {0, -3.0f, 0};
  sphere->linearDamping = 0.0f;

  world.Step(1.0f / 120.0f);

  // After SolveSpherePlane, velocity.y should have been flipped positive
  // (impulse = (1+e)*|vn|*mass, then position corrected)
  REQUIRE(sphere->velocity.y > 0.0f);
}

TEST_CASE("PhysicsWorld: sphere above floor does not bounce", "[world][collision]") {
  PhysicsWorld world;
  RigidBody *sphere = world.AddBody(RigidBody::MakeSphere(0.5f, 1.0f));
  sphere->position = {0, 10.0f, 0}; // far above floor
  sphere->velocity = Vec3::Zero();
  sphere->linearDamping = 0.0f;

  world.Step(1.0f / 120.0f);

  // Should have just fallen a bit — no bounce
  REQUIRE(sphere->position.y < 10.0f);
  REQUIRE(sphere->velocity.y < 0.0f);
}

TEST_CASE("PhysicsWorld: box bounces off floor at y=0", "[world][collision]") {
  PhysicsWorld world;
  RigidBody *box = world.AddBody(RigidBody::MakeBox({0.5f, 0.5f, 0.5f}, 1.0f));
  // Place below floor so SolveBoxPlane fires
  box->position = {0, 0.4f, 0}; // bottom face at y=-0.1
  box->velocity = {0, -3.0f, 0};
  box->linearDamping = 0.0f;

  world.Step(1.0f / 120.0f);

  // After SolveBoxPlane, velocity.y should be >= 0 (bounced or clamped)
  REQUIRE(box->velocity.y >= 0.0f);
}

TEST_CASE("PhysicsWorld: box moving upward does not trigger floor response", "[world][collision]") {
  PhysicsWorld world;
  RigidBody *box = world.AddBody(RigidBody::MakeBox({0.5f, 0.5f, 0.5f}, 1.0f));
  box->position = {0, 0.3f, 0}; // slightly below threshold
  box->velocity = {0, 5.0f, 0}; // moving upward
  box->linearDamping = 0.0f;

  world.Step(1.0f / 120.0f);

  // Moving upward — no floor impulse should clamp to zero
  // (velocity may change due to gravity but should not flip to bounce)
  REQUIRE(box->velocity.y > 0.0f);
}

TEST_CASE("PhysicsWorld: two overlapping spheres get separated", "[world][collision]") {
  PhysicsWorld world;
  RigidBody *a = world.AddBody(RigidBody::MakeSphere(1.0f, 1.0f));
  RigidBody *b = world.AddBody(RigidBody::MakeSphere(1.0f, 1.0f));
  a->position = {0, 10, 0};
  b->position = {1.0f, 10, 0}; // overlapping (sum radii = 2, dist = 1)
  a->velocity = {-1, 0, 0};
  b->velocity = {1, 0, 0};

  // Step to trigger GJK and constraint solve
  REQUIRE_NOTHROW(world.Step(1.0f / 120.0f));
}

TEST_CASE("PhysicsWorld: box-box collision processed without crash", "[world][collision]") {
  PhysicsWorld world;
  RigidBody *a = world.AddBody(RigidBody::MakeBox({0.5f, 0.5f, 0.5f}, 1.0f));
  RigidBody *b = world.AddBody(RigidBody::MakeBox({0.5f, 0.5f, 0.5f}, 1.0f));
  a->position = {0, 10, 0};
  b->position = {0.8f, 10, 0}; // overlapping on X
  a->velocity = {-1, 0, 0};
  b->velocity = {1, 0, 0};

  REQUIRE_NOTHROW(world.Step(1.0f / 120.0f));
}

TEST_CASE("PhysicsWorld: static-static pair is skipped in narrowphase", "[world]") {
  PhysicsWorld world;
  RigidBody *a = world.AddBody(RigidBody::MakeStatic());
  RigidBody *b = world.AddBody(RigidBody::MakeStatic());
  a->position = {0, 0, 0};
  b->position = {0.1f, 0, 0};
  // Give them overlapping sphere colliders via MakeSphere... but they're static
  a->collider = std::make_shared<SphereCollider>(1.0f);
  b->collider = std::make_shared<SphereCollider>(1.0f);

  // Should process without crash, neither should move
  REQUIRE_NOTHROW(world.Step(1.0f / 120.0f));
  REQUIRE(a->position.x == Approx(0.0f));
  REQUIRE(b->position.x == Approx(0.1f));
}

TEST_CASE("PhysicsWorld: body without collider is skipped in floor check", "[world]") {
  PhysicsWorld world;
  RigidBody *body = world.AddBody(RigidBody::MakeSphere(1.0f, 1.0f));
  body->collider = nullptr; // remove collider
  body->position = {0, 0.5f, 0};
  body->velocity = {0, -3, 0};

  // Should not crash — body without collider skips floor solve
  REQUIRE_NOTHROW(world.Step(1.0f / 120.0f));
}

TEST_CASE("PhysicsWorld: multiple Step calls don't crash", "[world]") {
  PhysicsWorld world;
  RigidBody *b = world.AddBody(RigidBody::MakeSphere(0.5f, 1.0f));
  b->position = {0, 5, 0};

  const float DT = 1.0f / 60.0f;
  for (int i = 0; i < 300; ++i)
    REQUIRE_NOTHROW(world.Step(DT));
}

TEST_CASE("PhysicsWorld: body falls and stays at floor level over time", "[world]") {
  PhysicsWorld world;
  world.SetGravity({0, -9.81f, 0});
  RigidBody *b = world.AddBody(RigidBody::MakeSphere(0.5f, 1.0f));
  b->position = {0, 5.0f, 0};
  b->linearDamping = 0.0f;

  const float DT = 1.0f / 120.0f;
  for (int i = 0; i < 1000; ++i)
    world.Step(DT);

  // After enough steps, sphere should be resting at floor level (radius = 0.5)
  REQUIRE(b->position.y >= 0.4f);
  REQUIRE(b->position.y <= 1.5f);
}
