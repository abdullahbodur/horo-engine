#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "math/Mat4.h"
#include "math/MathUtils.h"
#include "math/Vec3.h"
#include "renderer/Camera.h"

using namespace Monolith;
using Catch::Approx;

// ===========================================================================
// Camera — aspect ratio effects
// ===========================================================================

TEST_CASE("Camera: wider aspect ratio changes projection x scale", "[camera]") {
  Camera narrow;
  narrow.fovY = 60.0f;
  narrow.aspect = 1.0f;
  narrow.zNear = 0.1f;
  narrow.zFar = 100.0f;

  Camera wide;
  wide.fovY = 60.0f;
  wide.aspect = 16.0f / 9.0f;
  wide.zNear = 0.1f;
  wide.zFar = 100.0f;

  // P[0][0] = 1 / (aspect * tan(fov/2))
  // Wider aspect → smaller x scale
  REQUIRE(narrow.GetProjection()(0, 0) > wide.GetProjection()(0, 0));
}

TEST_CASE("Camera: Y scale is independent of aspect ratio", "[camera]") {
  Camera cam1;
  cam1.fovY = 60.0f;
  cam1.aspect = 1.0f;

  Camera cam2;
  cam2.fovY = 60.0f;
  cam2.aspect = 2.0f;

  // P[1][1] depends only on fovY, not aspect
  REQUIRE(cam1.GetProjection()(1, 1) ==
          Approx(cam2.GetProjection()(1, 1)).epsilon(1e-5f));
}

// ===========================================================================
// Camera — near/far plane effects on projection
// ===========================================================================

TEST_CASE("Camera: different zNear/zFar changes depth projection", "[camera]") {
  Camera cam1;
  cam1.fovY = 60.0f;
  cam1.aspect = 1.0f;
  cam1.zNear = 0.1f;
  cam1.zFar = 10.0f;

  Camera cam2;
  cam2.fovY = 60.0f;
  cam2.aspect = 1.0f;
  cam2.zNear = 0.1f;
  cam2.zFar = 1000.0f;

  Mat4 P1 = cam1.GetProjection();
  Mat4 P2 = cam2.GetProjection();

  // m[2][2] encodes the depth range mapping — should differ
  REQUIRE(P1(2, 2) != Approx(P2(2, 2)).epsilon(0.01f));
}

// ===========================================================================
// Camera — view matrix properties
// ===========================================================================

TEST_CASE("Camera GetView: is invertible (non-zero determinant)", "[camera]") {
  Camera cam;
  cam.position = {3.0f, 4.0f, 5.0f};
  cam.target = {0.0f, 0.0f, 0.0f};
  cam.up = Vec3::Up();
  REQUIRE(cam.GetView().Determinant() != Approx(0.0f).margin(1e-6f));
}

TEST_CASE("Camera GetView: view matrix maps camera position to origin", "[camera]") {
  Camera cam;
  cam.position = {5.0f, 3.0f, 2.0f};
  cam.target = {0.0f, 0.0f, 0.0f};
  cam.up = Vec3::Up();

  Mat4 V = cam.GetView();
  Vec3 eye = V.TransformPoint(cam.position);
  REQUIRE(eye.x == Approx(0.0f).margin(1e-4f));
  REQUIRE(eye.y == Approx(0.0f).margin(1e-4f));
  REQUIRE(eye.z == Approx(0.0f).margin(1e-4f));
}

TEST_CASE("Camera GetView: target is at negative Z in view space", "[camera]") {
  Camera cam;
  cam.position = {0.0f, 0.0f, 10.0f};
  cam.target = {0.0f, 0.0f, 0.0f};
  cam.up = Vec3::Up();

  Mat4 V = cam.GetView();
  Vec3 targetInView = V.TransformPoint(cam.target);
  REQUIRE(targetInView.z < 0.0f);
}

// ===========================================================================
// Camera — GetForward/GetRight edge cases
// ===========================================================================

TEST_CASE("Camera GetForward: looking along +X", "[camera]") {
  Camera cam;
  cam.position = {0.0f, 0.0f, 0.0f};
  cam.target = {1.0f, 0.0f, 0.0f};
  cam.up = Vec3::Up();

  Vec3 fwd = cam.GetForward();
  REQUIRE(fwd.x == Approx(1.0f).epsilon(1e-5f));
  REQUIRE(fwd.y == Approx(0.0f).margin(1e-5f));
  REQUIRE(fwd.z == Approx(0.0f).margin(1e-5f));
}

TEST_CASE("Camera GetForward: looking along +Y", "[camera]") {
  Camera cam;
  cam.position = {0.0f, 0.0f, 0.0f};
  cam.target = {0.0f, 1.0f, 0.0f};

  Vec3 fwd = cam.GetForward();
  REQUIRE(fwd.y == Approx(1.0f).epsilon(1e-5f));
}

TEST_CASE("Camera GetRight: diagonal forward direction", "[camera]") {
  Camera cam;
  cam.position = {0.0f, 0.0f, 0.0f};
  cam.target = {1.0f, 0.0f, -1.0f}; // 45° in XZ plane
  cam.up = Vec3::Up();

  Vec3 right = cam.GetRight();
  REQUIRE(right.Length() == Approx(1.0f).epsilon(1e-5f));
  // Right should be perpendicular to forward
  Vec3 fwd = cam.GetForward();
  REQUIRE(Vec3::Dot(fwd, right) == Approx(0.0f).margin(1e-5f));
}

// ===========================================================================
// Camera — ViewProjection matrix decomposition
// ===========================================================================

TEST_CASE("Camera GetViewProjection: moving camera changes VP matrix", "[camera]") {
  Camera cam1;
  cam1.position = {0.0f, 0.0f, 5.0f};
  cam1.target = {0.0f, 0.0f, 0.0f};
  cam1.up = Vec3::Up();
  cam1.fovY = 60.0f;
  cam1.aspect = 1.0f;
  cam1.zNear = 0.1f;
  cam1.zFar = 100.0f;

  Camera cam2 = cam1;
  cam2.position = {10.0f, 0.0f, 5.0f}; // moved

  Mat4 VP1 = cam1.GetViewProjection();
  Mat4 VP2 = cam2.GetViewProjection();

  const auto differs = [&]() {
    for (int r = 0; r < 4; ++r) {
      for (int c = 0; c < 4; ++c) {
        if (std::abs(VP1(r, c) - VP2(r, c)) > 1e-4f) {
          return true;
        }
      }
    }
    return false;
  }();

  REQUIRE(differs);
}

TEST_CASE("Camera GetViewProjection: is non-degenerate", "[camera]") {
  Camera cam;
  cam.position = {1.0f, 2.0f, 3.0f};
  cam.target = {4.0f, 5.0f, 6.0f};
  cam.up = Vec3::Up();
  cam.fovY = 45.0f;
  cam.aspect = 4.0f / 3.0f;
  cam.zNear = 0.01f;
  cam.zFar = 500.0f;

  Mat4 VP = cam.GetViewProjection();
  REQUIRE(VP.Determinant() != Approx(0.0f).margin(1e-8f));
}

// ===========================================================================
// Camera — default values
// ===========================================================================

TEST_CASE("Camera default construction has sensible values", "[camera]") {
  Camera cam;
  REQUIRE(cam.fovY > 0.0f);
  REQUIRE(cam.zNear > 0.0f);
  REQUIRE(cam.zFar > cam.zNear);
  REQUIRE(cam.aspect > 0.0f);
}

TEST_CASE("Camera: changing fovY changes projection", "[camera]") {
  Camera cam60;
  cam60.fovY = 60.0f;
  cam60.aspect = 1.0f;
  cam60.zNear = 0.1f;
  cam60.zFar = 100.0f;

  Camera cam90;
  cam90.fovY = 90.0f;
  cam90.aspect = 1.0f;
  cam90.zNear = 0.1f;
  cam90.zFar = 100.0f;

  REQUIRE(cam60.GetProjection()(1, 1) !=
          Approx(cam90.GetProjection()(1, 1)).epsilon(0.01f));
}
