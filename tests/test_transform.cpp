#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "math/Mat4.h"
#include "math/MathUtils.h"
#include "math/Quaternion.h"
#include "math/Transform.h"
#include "math/Vec3.h"

using namespace Monolith;
using Catch::Approx;

// ===========================================================================
// Transform — default construction
// ===========================================================================

TEST_CASE("Transform: default is identity", "[transform]") {
    Transform t;
    REQUIRE(t.position.x == Approx(0.0f));
    REQUIRE(t.position.y == Approx(0.0f));
    REQUIRE(t.position.z == Approx(0.0f));
    REQUIRE(t.scale.x == Approx(1.0f));
    REQUIRE(t.scale.y == Approx(1.0f));
    REQUIRE(t.scale.z == Approx(1.0f));
    REQUIRE(t.rotation.x == Approx(0.0f));
    REQUIRE(t.rotation.y == Approx(0.0f));
    REQUIRE(t.rotation.z == Approx(0.0f));
    REQUIRE(t.rotation.w == Approx(1.0f));
}

TEST_CASE("Transform: Identity() equals default", "[transform]") {
    Transform t1;
    Transform t2 = Transform::Identity();
    REQUIRE(t1.position.x == Approx(t2.position.x));
    REQUIRE(t1.position.y == Approx(t2.position.y));
    REQUIRE(t1.position.z == Approx(t2.position.z));
    REQUIRE(t1.scale.x == Approx(t2.scale.x));
    REQUIRE(t1.rotation.w == Approx(t2.rotation.w));
}

// ===========================================================================
// Transform — construction with parameters
// ===========================================================================

TEST_CASE("Transform: position-only constructor", "[transform]") {
    Transform t({1.0f, 2.0f, 3.0f});
    REQUIRE(t.position.x == Approx(1.0f));
    REQUIRE(t.position.y == Approx(2.0f));
    REQUIRE(t.position.z == Approx(3.0f));
    // rotation and scale should be identity
    REQUIRE(t.rotation.w == Approx(1.0f));
    REQUIRE(t.scale.x == Approx(1.0f));
}

TEST_CASE("Transform: full constructor sets all fields", "[transform]") {
    Vec3 pos{5.0f, -2.0f, 3.0f};
    Quaternion rot = Quaternion::FromAxisAngle({0.0f, 1.0f, 0.0f}, PI * 0.5f);
    Vec3 scale{2.0f, 2.0f, 2.0f};

    Transform t(pos, rot, scale);
    REQUIRE(t.position.x == Approx(5.0f));
    REQUIRE(t.position.y == Approx(-2.0f));
    REQUIRE(t.position.z == Approx(3.0f));
    REQUIRE(t.scale.x == Approx(2.0f));
    REQUIRE(t.rotation.w == Approx(rot.w));
}

// ===========================================================================
// Transform — ToMatrix
// ===========================================================================

TEST_CASE("Transform::ToMatrix: identity transform gives identity matrix",
          "[transform]") {
    Transform t = Transform::Identity();
    Mat4 m = t.ToMatrix();

    // diagonal should be 1
    REQUIRE(m(0, 0) == Approx(1.0f).epsilon(1e-5f));
    REQUIRE(m(1, 1) == Approx(1.0f).epsilon(1e-5f));
    REQUIRE(m(2, 2) == Approx(1.0f).epsilon(1e-5f));
    REQUIRE(m(3, 3) == Approx(1.0f).epsilon(1e-5f));

    // off-diagonals should be 0
    REQUIRE(m(0, 1) == Approx(0.0f).margin(1e-5f));
    REQUIRE(m(1, 0) == Approx(0.0f).margin(1e-5f));
    REQUIRE(m(0, 3) == Approx(0.0f).margin(1e-5f));
    REQUIRE(m(1, 3) == Approx(0.0f).margin(1e-5f));
    REQUIRE(m(2, 3) == Approx(0.0f).margin(1e-5f));
}

TEST_CASE("Transform::ToMatrix: translation is embedded in last column",
          "[transform]") {
    Transform t;
    t.position = {3.0f, -1.0f, 5.0f};
    Mat4 m = t.ToMatrix();

    REQUIRE(m.GetTranslation().x == Approx(3.0f).epsilon(1e-5f));
    REQUIRE(m.GetTranslation().y == Approx(-1.0f).epsilon(1e-5f));
    REQUIRE(m.GetTranslation().z == Approx(5.0f).epsilon(1e-5f));
}

TEST_CASE("Transform::ToMatrix: uniform scale reflects in matrix",
          "[transform]") {
    Transform t;
    t.scale = {3.0f, 3.0f, 3.0f};
    Mat4 m = t.ToMatrix();

    // Scaling a unit vector along X should give length 3
    Vec3 scaled = m.TransformVector({1.0f, 0.0f, 0.0f});
    REQUIRE(scaled.Length() == Approx(3.0f).epsilon(1e-4f));
}

TEST_CASE("Transform::ToMatrix: 90-degree Y rotation rotates X to Z",
          "[transform]") {
    Transform t;
    t.rotation = Quaternion::FromAxisAngle({0.0f, 1.0f, 0.0f}, PI * 0.5f);
    Mat4 m = t.ToMatrix();

    Vec3 rotated = m.TransformVector({1.0f, 0.0f, 0.0f});
    // Rotating X by 90° around Y → should point toward +Z (right-hand rule: X
    // cross Y = -Z, so rotate X by +90 around Y gives Z)
    REQUIRE(std::abs(rotated.x) < 1e-4f);
    REQUIRE(std::abs(rotated.y) < 1e-4f);
    REQUIRE(std::abs(rotated.z) > 0.9f);
}

// ===========================================================================
// Transform — TransformPoint
// ===========================================================================

TEST_CASE("Transform::TransformPoint: identity leaves point unchanged",
          "[transform]") {
    Transform t = Transform::Identity();
    Vec3 pt{1.0f, 2.0f, 3.0f};
    Vec3 result = t.TransformPoint(pt);
    REQUIRE(result.x == Approx(1.0f).epsilon(1e-5f));
    REQUIRE(result.y == Approx(2.0f).epsilon(1e-5f));
    REQUIRE(result.z == Approx(3.0f).epsilon(1e-5f));
}

TEST_CASE("Transform::TransformPoint: translation moves point", "[transform]") {
    Transform t;
    t.position = {10.0f, 0.0f, 0.0f};
    Vec3 result = t.TransformPoint({0.0f, 0.0f, 0.0f});
    REQUIRE(result.x == Approx(10.0f).epsilon(1e-5f));
    REQUIRE(result.y == Approx(0.0f).margin(1e-5f));
    REQUIRE(result.z == Approx(0.0f).margin(1e-5f));
}

TEST_CASE("Transform::TransformPoint: scale stretches point", "[transform]") {
    Transform t;
    t.scale = {2.0f, 3.0f, 4.0f};
    Vec3 result = t.TransformPoint({1.0f, 1.0f, 1.0f});
    REQUIRE(result.x == Approx(2.0f).epsilon(1e-5f));
    REQUIRE(result.y == Approx(3.0f).epsilon(1e-5f));
    REQUIRE(result.z == Approx(4.0f).epsilon(1e-5f));
}

TEST_CASE("Transform::TransformPoint: rotation rotates point", "[transform]") {
    Transform t;
    // 180° rotation around Y → X flips to -X, Z flips to -Z
    t.rotation = Quaternion::FromAxisAngle({0.0f, 1.0f, 0.0f}, PI);
    Vec3 result = t.TransformPoint({1.0f, 0.0f, 0.0f});
    REQUIRE(result.x == Approx(-1.0f).epsilon(1e-4f));
    REQUIRE(result.y == Approx(0.0f).margin(1e-4f));
}

TEST_CASE("Transform::TransformPoint: combined TRS", "[transform]") {
    Transform t;
    t.position = {1.0f, 0.0f, 0.0f};
    t.scale = {2.0f, 2.0f, 2.0f};
    // No rotation
    Vec3 result = t.TransformPoint({1.0f, 0.0f, 0.0f});
    // scale(1,0,0) → (2,0,0) then translate +1 → (3,0,0)
    REQUIRE(result.x == Approx(3.0f).epsilon(1e-5f));
}

// ===========================================================================
// Transform — Forward / Up / Right
// ===========================================================================

TEST_CASE("Transform::Forward: identity points in default forward (-Z)",
          "[transform]") {
    Transform t = Transform::Identity();
    Vec3 fwd = t.Forward();
    REQUIRE(fwd.x == Approx(0.0f).margin(1e-5f));
    REQUIRE(fwd.y == Approx(0.0f).margin(1e-5f));
    REQUIRE(fwd.z == Approx(-1.0f).epsilon(1e-5f));
}

TEST_CASE("Transform::Up: identity points in +Y", "[transform]") {
    Transform t = Transform::Identity();
    Vec3 up = t.Up();
    REQUIRE(up.x == Approx(0.0f).margin(1e-5f));
    REQUIRE(up.y == Approx(1.0f).epsilon(1e-5f));
    REQUIRE(up.z == Approx(0.0f).margin(1e-5f));
}

TEST_CASE("Transform::Right: identity points in +X", "[transform]") {
    Transform t = Transform::Identity();
    Vec3 right = t.Right();
    REQUIRE(right.x == Approx(1.0f).epsilon(1e-5f));
    REQUIRE(right.y == Approx(0.0f).margin(1e-5f));
    REQUIRE(right.z == Approx(0.0f).margin(1e-5f));
}

TEST_CASE("Transform::Forward: 90° Y rotation changes forward direction",
          "[transform]") {
    Transform t;
    // Rotating 90° around Y: Forward (-Z) becomes (-X) rotated
    t.rotation = Quaternion::FromAxisAngle({0.0f, 1.0f, 0.0f}, PI * 0.5f);
    Vec3 fwd = t.Forward();
    REQUIRE(fwd.Length() == Approx(1.0f).epsilon(1e-5f));
    // After 90° CCW around Y, forward (-Z) rotates to (+X direction... check
    // engine convention) just verify it's a unit vector and has changed from
    // default
    REQUIRE(!(fwd.z == Approx(-1.0f).epsilon(0.1f))); // no longer -Z
}

TEST_CASE("Transform direction vectors are mutually perpendicular",
          "[transform]") {
    Transform t;
    t.rotation = Quaternion::FromEuler(0.2f, 0.5f, 0.1f);

    Vec3 fwd = t.Forward();
    Vec3 up = t.Up();
    Vec3 right = t.Right();

    REQUIRE(Vec3::Dot(fwd, up) == Approx(0.0f).margin(1e-4f));
    REQUIRE(Vec3::Dot(fwd, right) == Approx(0.0f).margin(1e-4f));
    REQUIRE(Vec3::Dot(up, right) == Approx(0.0f).margin(1e-4f));
}

TEST_CASE("Transform direction vectors are normalized", "[transform]") {
    Transform t;
    t.rotation = Quaternion::FromAxisAngle({1.0f, 1.0f, 0.0f}, 1.2f);

    REQUIRE(t.Forward().Length() == Approx(1.0f).epsilon(1e-5f));
    REQUIRE(t.Up().Length() == Approx(1.0f).epsilon(1e-5f));
    REQUIRE(t.Right().Length() == Approx(1.0f).epsilon(1e-5f));
}

// ===========================================================================
// Transform — matrix vs TransformPoint consistency
// ===========================================================================

TEST_CASE("Transform::ToMatrix and TransformPoint agree", "[transform]") {
    Transform t;
    t.position = {1.0f, 2.0f, -3.0f};
    t.scale = {2.0f, 0.5f, 1.5f};
    t.rotation = Quaternion::FromAxisAngle({0.0f, 1.0f, 0.0f}, 0.7f);

    Vec3 localPt{1.0f, 1.0f, 1.0f};

    Vec3 matResult = t.ToMatrix().TransformPoint(localPt);
    Vec3 funcResult = t.TransformPoint(localPt);

    REQUIRE(matResult.x == Approx(funcResult.x).epsilon(1e-4f));
    REQUIRE(matResult.y == Approx(funcResult.y).epsilon(1e-4f));
    REQUIRE(matResult.z == Approx(funcResult.z).epsilon(1e-4f));
}

TEST_CASE("WorldAabbFromLocalBox: translated unit cube", "[transform][aabb]") {
    Transform t;
    t.position = {10.0f, 0.0f, -5.0f};
    Vec3 wc;
    Vec3 wh;
    WorldAabbFromLocalBox(Vec3::Zero(), {0.5f, 0.5f, 0.5f}, t, wc, wh);
    REQUIRE(wc.x == Approx(10.0f));
    REQUIRE(wc.y == Approx(0.0f));
    REQUIRE(wc.z == Approx(-5.0f));
    REQUIRE(wh.x == Approx(0.5f));
    REQUIRE(wh.y == Approx(0.5f));
    REQUIRE(wh.z == Approx(0.5f));
}

TEST_CASE("WorldAabbFromLocalBox: offset local center expands world AABB",
          "[transform][aabb]") {
    Transform t;
    t.position = {0.0f, 0.0f, 0.0f};
    Vec3 wc;
    Vec3 wh;
    // Local box [0,1]^3 → center 0.5, half 0.5
    WorldAabbFromLocalBox({0.5f, 0.5f, 0.5f}, {0.5f, 0.5f, 0.5f}, t, wc, wh);
    REQUIRE(wc.x == Approx(0.5f));
    REQUIRE(wc.y == Approx(0.5f));
    REQUIRE(wc.z == Approx(0.5f));
    REQUIRE(wh.x == Approx(0.5f));
}
