#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "math/Mat4.h"
#include "math/MathUtils.h"
#include "math/Vec3.h"
#include "renderer/Camera.h"

using namespace Monolith;
using Catch::Approx;

// ============================================================
// Camera — GetView / GetProjection
// ============================================================

TEST_CASE("Camera GetView: eye at origin looking down -Z gives near-identity "
          "rotation",
          "[camera]") {
    Camera cam;
    cam.position = {0, 0, 0};
    cam.target = {0, 0, -1};
    cam.up = Vec3::Up();

    Mat4 V = cam.GetView();

    // A point on the -Z axis should remain on -Z in view space
    Vec3 pt = V.TransformPoint({0, 0, -5});
    REQUIRE(pt.x == Approx(0).margin(1e-4f));
    REQUIRE(pt.y == Approx(0).margin(1e-4f));
    REQUIRE(pt.z < 0.0f); // -Z is in front of the camera
}

TEST_CASE("Camera GetView: translation moves eye to origin in view space",
          "[camera]") {
    Camera cam;
    cam.position = {10, 0, 0};
    cam.target = {0, 0, 0};
    cam.up = Vec3::Up();

    Mat4 V = cam.GetView();
    Vec3 eyeInView = V.TransformPoint(cam.position);

    REQUIRE(eyeInView.x == Approx(0).margin(1e-4f));
    REQUIRE(eyeInView.y == Approx(0).margin(1e-4f));
    REQUIRE(eyeInView.z == Approx(0).margin(1e-4f));
}

TEST_CASE("Camera GetProjection: produces non-zero determinant", "[camera]") {
    Camera cam;
    cam.fovY = 60.0f;
    cam.aspect = 16.0f / 9.0f;
    cam.zNear = 0.1f;
    cam.zFar = 1000.0f;

    Mat4 P = cam.GetProjection();
    REQUIRE(P.Determinant() != Approx(0).margin(1e-6f));
}

TEST_CASE("Camera GetProjection: wider FOV gives larger field coverage",
          "[camera]") {
    Camera narrow;
    narrow.fovY = 30.0f;
    narrow.aspect = 1.0f;
    narrow.zNear = 0.1f;
    narrow.zFar = 100.0f;

    Camera wide;
    wide.fovY = 90.0f;
    wide.aspect = 1.0f;
    wide.zNear = 0.1f;
    wide.zFar = 100.0f;

    // For perspective, P[0][0] = 1/(aspect * tan(fov/2))
    // Wider FOV → smaller P[0][0]
    Mat4 Pnarrow = narrow.GetProjection();
    Mat4 Pwide = wide.GetProjection();
    REQUIRE(Pnarrow(0, 0) > Pwide(0, 0));
}

TEST_CASE("Camera GetViewProjection: combines view and projection",
          "[camera]") {
    Camera cam;
    cam.position = {0, 0, 5};
    cam.target = {0, 0, 0};
    cam.up = Vec3::Up();
    cam.fovY = 60.0f;
    cam.aspect = 1.0f;
    cam.zNear = 0.1f;
    cam.zFar = 100.0f;

    Mat4 VP = cam.GetViewProjection();
    Mat4 P = cam.GetProjection();
    Mat4 V = cam.GetView();
    Mat4 PV = P * V;

    // Both should give the same result
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
            REQUIRE(VP(r, c) == Approx(PV(r, c)).epsilon(1e-5f));
}

// ============================================================
// Camera — GetForward / GetRight
// ============================================================

TEST_CASE("Camera GetForward: pointing along -Z when looking at origin from +Z",
          "[camera]") {
    Camera cam;
    cam.position = {0, 0, 5};
    cam.target = {0, 0, 0};

    Vec3 fwd = cam.GetForward();
    REQUIRE(fwd.x == Approx(0).margin(1e-5f));
    REQUIRE(fwd.y == Approx(0).margin(1e-5f));
    REQUIRE(fwd.z == Approx(-1).epsilon(1e-5f));
}

TEST_CASE("Camera GetForward: is normalized", "[camera]") {
    Camera cam;
    cam.position = {1, 2, 3};
    cam.target = {4, 5, 6};

    Vec3 fwd = cam.GetForward();
    REQUIRE(fwd.Length() == Approx(1.0f).epsilon(1e-5f));
}

TEST_CASE("Camera GetRight: is perpendicular to forward", "[camera]") {
    Camera cam;
    cam.position = {0, 0, 5};
    cam.target = {0, 0, 0};
    cam.up = Vec3::Up();

    Vec3 fwd = cam.GetForward();
    Vec3 right = cam.GetRight();

    float dot = Vec3::Dot(fwd, right);
    REQUIRE(dot == Approx(0).margin(1e-5f));
}

TEST_CASE("Camera GetRight: is normalized", "[camera]") {
    Camera cam;
    cam.position = {0, 0, 5};
    cam.target = {0, 0, 0};
    cam.up = Vec3::Up();

    Vec3 right = cam.GetRight();
    REQUIRE(right.Length() == Approx(1.0f).epsilon(1e-5f));
}

TEST_CASE("Camera GetRight: looking along -Z, right points in +X", "[camera]") {
    Camera cam;
    cam.position = {0, 0, 5};
    cam.target = {0, 0, 0};
    cam.up = Vec3::Up();

    Vec3 right = cam.GetRight();
    REQUIRE(right.x == Approx(1).epsilon(1e-5f));
    REQUIRE(right.y == Approx(0).margin(1e-5f));
    REQUIRE(right.z == Approx(0).margin(1e-5f));
}
