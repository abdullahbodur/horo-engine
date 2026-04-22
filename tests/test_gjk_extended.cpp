#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "math/MathUtils.h"
#include "math/Quaternion.h"
#include "math/Vec3.h"
#include "physics/BoxCollider.h"
#include "physics/RigidBody.h"
#include "physics/SphereCollider.h"
#include "physics/narrowphase/GJK.h"
#include "physics/narrowphase/SAT.h"

using namespace Monolith;
using Catch::Approx;

// ===========================================================================
// GJK — extended sphere-sphere tests to hit more code paths
// ===========================================================================

TEST_CASE("GJK: deep overlap produces large penetration", "[gjk]") {
    RigidBody a = RigidBody::MakeSphere(1.0f, 1.0f);
    RigidBody b = RigidBody::MakeSphere(1.0f, 1.0f);
    a.position = {0.0f, 0.0f, 0.0f};
    b.position = {0.1f, 0.0f, 0.0f}; // almost same center

    auto m = GJK::Test(a, b);
    REQUIRE(m.hasContact());
    REQUIRE(m.contacts[0].penetration > 1.5f);
}

TEST_CASE("GJK: overlap along Y axis", "[gjk]") {
    RigidBody a = RigidBody::MakeSphere(0.5f, 1.0f);
    RigidBody b = RigidBody::MakeSphere(0.5f, 1.0f);
    a.position = {0.0f, 0.0f, 0.0f};
    b.position = {0.0f, 0.8f, 0.0f};

    auto m = GJK::Test(a, b);
    REQUIRE(m.hasContact());
    // Normal should be along Y
    REQUIRE(std::abs(m.contacts[0].normal.y) > 0.9f);
}

TEST_CASE("GJK: overlap along Z axis", "[gjk]") {
    RigidBody a = RigidBody::MakeSphere(0.5f, 1.0f);
    RigidBody b = RigidBody::MakeSphere(0.5f, 1.0f);
    a.position = {0.0f, 0.0f, 0.0f};
    b.position = {0.0f, 0.0f, 0.8f};

    auto m = GJK::Test(a, b);
    REQUIRE(m.hasContact());
    REQUIRE(std::abs(m.contacts[0].normal.z) > 0.9f);
}

TEST_CASE("GJK: contact normal is normalized", "[gjk]") {
    RigidBody a = RigidBody::MakeSphere(1.0f, 1.0f);
    RigidBody b = RigidBody::MakeSphere(1.0f, 1.0f);
    a.position = {0.0f, 0.0f, 0.0f};
    b.position = {1.5f, 0.0f, 0.0f};

    auto m = GJK::Test(a, b);
    REQUIRE(m.hasContact());
    float len = m.contacts[0].normal.Length();
    REQUIRE(len == Approx(1.0f).epsilon(1e-5f));
}

TEST_CASE("GJK: different-radius spheres overlapping", "[gjk]") {
    RigidBody a = RigidBody::MakeSphere(2.0f, 1.0f);
    RigidBody b = RigidBody::MakeSphere(0.5f, 1.0f);
    a.position = {0.0f, 0.0f, 0.0f};
    b.position = {2.0f, 0.0f, 0.0f}; // dist=2, sum=2.5 → overlap of 0.5

    auto m = GJK::Test(a, b);
    REQUIRE(m.hasContact());
    REQUIRE(m.contacts[0].penetration == Approx(0.5f).epsilon(1e-4f));
}

TEST_CASE("GJK: sphere vs box returns empty (unimplemented path)", "[gjk]") {
    RigidBody a = RigidBody::MakeSphere(1.0f, 1.0f);
    RigidBody b = RigidBody::MakeBox({0.5f, 0.5f, 0.5f}, 1.0f);
    a.position = {0.0f, 0.0f, 0.0f};
    b.position = {0.5f, 0.0f, 0.0f};

    auto m = GJK::Test(a, b);
    // GJK only handles sphere-sphere fast path, box-sphere returns empty
    REQUIRE_FALSE(m.hasContact());
}

TEST_CASE("GJK: box vs sphere returns empty (unimplemented path)", "[gjk]") {
    RigidBody a = RigidBody::MakeBox({0.5f, 0.5f, 0.5f}, 1.0f);
    RigidBody b = RigidBody::MakeSphere(1.0f, 1.0f);
    a.position = {0.0f, 0.0f, 0.0f};
    b.position = {0.5f, 0.0f, 0.0f};

    auto m = GJK::Test(a, b);
    REQUIRE_FALSE(m.hasContact());
}

TEST_CASE("GJK: box vs box returns empty (handled by SAT)", "[gjk]") {
    RigidBody a = RigidBody::MakeBox({0.5f, 0.5f, 0.5f}, 1.0f);
    RigidBody b = RigidBody::MakeBox({0.5f, 0.5f, 0.5f}, 1.0f);
    a.position = {0.0f, 0.0f, 0.0f};
    b.position = {0.5f, 0.0f, 0.0f};

    // GJK sphere-sphere fastpath won't trigger for box-box
    auto m = GJK::Test(a, b);
    REQUIRE_FALSE(m.hasContact());
}

// ===========================================================================
// SAT — box vs box collision detection
// ===========================================================================

TEST_CASE("SAT::TestBoxBox: no collider returns empty", "[sat]") {
    RigidBody a = RigidBody::MakeStatic();
    RigidBody b = RigidBody::MakeBox({0.5f, 0.5f, 0.5f}, 1.0f);
    auto m = SAT::TestBoxBox(a, b);
    REQUIRE_FALSE(m.hasContact());
}

TEST_CASE("SAT::TestBoxBox: overlapping axis-aligned boxes produce contact",
          "[sat]") {
    RigidBody a = RigidBody::MakeBox({0.5f, 0.5f, 0.5f}, 1.0f);
    RigidBody b = RigidBody::MakeBox({0.5f, 0.5f, 0.5f}, 1.0f);
    a.position = {0.0f, 0.0f, 0.0f};
    b.position = {0.8f, 0.0f, 0.0f}; // overlap = 1.0 - 0.8 = 0.2

    auto m = SAT::TestBoxBox(a, b);
    REQUIRE(m.hasContact());
}

TEST_CASE("SAT::TestBoxBox: separated boxes no contact", "[sat]") {
    RigidBody a = RigidBody::MakeBox({0.5f, 0.5f, 0.5f}, 1.0f);
    RigidBody b = RigidBody::MakeBox({0.5f, 0.5f, 0.5f}, 1.0f);
    a.position = {0.0f, 0.0f, 0.0f};
    b.position = {5.0f, 0.0f, 0.0f}; // far apart

    auto m = SAT::TestBoxBox(a, b);
    REQUIRE_FALSE(m.hasContact());
}

TEST_CASE("SAT::TestBoxBox: overlapping along Y axis", "[sat]") {
    RigidBody a = RigidBody::MakeBox({0.5f, 0.5f, 0.5f}, 1.0f);
    RigidBody b = RigidBody::MakeBox({0.5f, 0.5f, 0.5f}, 1.0f);
    a.position = {0.0f, 0.0f, 0.0f};
    b.position = {0.0f, 0.7f, 0.0f}; // overlap in Y

    auto m = SAT::TestBoxBox(a, b);
    REQUIRE(m.hasContact());
}

TEST_CASE("SAT::TestBoxBox: overlapping along Z axis", "[sat]") {
    RigidBody a = RigidBody::MakeBox({0.5f, 0.5f, 0.5f}, 1.0f);
    RigidBody b = RigidBody::MakeBox({0.5f, 0.5f, 0.5f}, 1.0f);
    a.position = {0.0f, 0.0f, 0.0f};
    b.position = {0.0f, 0.0f, 0.7f};

    auto m = SAT::TestBoxBox(a, b);
    REQUIRE(m.hasContact());
}

TEST_CASE("SAT::TestBoxBox: wrong collider types (sphere+sphere) returns empty",
          "[sat]") {
    RigidBody a = RigidBody::MakeSphere(0.5f, 1.0f);
    RigidBody b = RigidBody::MakeSphere(0.5f, 1.0f);
    auto m = SAT::TestBoxBox(a, b);
    REQUIRE_FALSE(m.hasContact());
}

TEST_CASE("SAT::TestBoxBox: deep overlap produces penetration", "[sat]") {
    RigidBody a = RigidBody::MakeBox({1.0f, 1.0f, 1.0f}, 1.0f);
    RigidBody b = RigidBody::MakeBox({1.0f, 1.0f, 1.0f}, 1.0f);
    a.position = {0.0f, 0.0f, 0.0f};
    b.position = {0.5f, 0.0f, 0.0f};

    auto m = SAT::TestBoxBox(a, b);
    REQUIRE(m.hasContact());
    REQUIRE(m.contacts[0].penetration > 0.0f);
}

TEST_CASE("SAT::TestBoxBox: large boxes vs small box overlapping", "[sat]") {
    RigidBody a = RigidBody::MakeBox({2.0f, 2.0f, 2.0f}, 1.0f);
    RigidBody b = RigidBody::MakeBox({0.1f, 0.1f, 0.1f}, 1.0f);
    a.position = {0.0f, 0.0f, 0.0f};
    b.position = {0.0f, 0.0f, 0.0f}; // small box fully inside large box

    auto m = SAT::TestBoxBox(a, b);
    REQUIRE(m.hasContact());
}

TEST_CASE("SAT::TestBoxBox: rotated overlapping boxes produce contact",
          "[sat]") {
    RigidBody a = RigidBody::MakeBox({0.5f, 0.5f, 0.5f}, 1.0f);
    RigidBody b = RigidBody::MakeBox({0.5f, 0.5f, 0.5f}, 1.0f);
    a.position = {0.0f, 0.0f, 0.0f};
    b.position = {0.8f, 0.0f, 0.0f};
    // Rotate b 45° around Y
    b.orientation = Quaternion::FromAxisAngle({0.0f, 1.0f, 0.0f}, PI * 0.25f);

    auto m = SAT::TestBoxBox(a, b);
    REQUIRE(m.hasContact());
}

TEST_CASE("SAT::TestBoxBox: contact normal is approximately normalized",
          "[sat]") {
    RigidBody a = RigidBody::MakeBox({0.5f, 0.5f, 0.5f}, 1.0f);
    RigidBody b = RigidBody::MakeBox({0.5f, 0.5f, 0.5f}, 1.0f);
    a.position = {0.0f, 0.0f, 0.0f};
    b.position = {0.9f, 0.0f, 0.0f};

    auto m = SAT::TestBoxBox(a, b);
    REQUIRE(m.hasContact());
    float len = m.contacts[0].normal.Length();
    REQUIRE(len == Approx(1.0f).epsilon(1e-4f));
}

// ===========================================================================
// GJK — null collider guard paths
// ===========================================================================

TEST_CASE("GJK::Test: null collider on bodyA returns empty contact",
          "[gjk]") {
    RigidBody a = RigidBody::MakeStatic(); // no collider
    RigidBody b = RigidBody::MakeSphere(1.0f, 1.0f);
    b.position = {0.5f, 0.0f, 0.0f};
    auto m = GJK::Test(a, b);
    REQUIRE_FALSE(m.hasContact());
}

TEST_CASE("GJK::Test: null collider on bodyB returns empty contact",
          "[gjk]") {
    RigidBody a = RigidBody::MakeSphere(1.0f, 1.0f);
    RigidBody b = RigidBody::MakeStatic(); // no collider
    b.position = {0.5f, 0.0f, 0.0f};
    auto m = GJK::Test(a, b);
    REQUIRE_FALSE(m.hasContact());
}

TEST_CASE("GJK::Test: both null colliders returns empty contact", "[gjk]") {
    RigidBody a = RigidBody::MakeStatic();
    RigidBody b = RigidBody::MakeStatic();
    auto m = GJK::Test(a, b);
    REQUIRE_FALSE(m.hasContact());
}
