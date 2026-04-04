#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include "editor/Raycaster.h"
#include "editor/TransformGizmo.h"
#include "math/MathUtils.h"
#include "math/Quaternion.h"
#include "math/Vec3.h"
#include "renderer/Camera.h"

using namespace Monolith;
using namespace Monolith::Editor;
using Catch::Approx;

// ---- Helpers -----------------------------------------------------------------

static Camera MakeCamera(Vec3 pos, Vec3 target, float fovY = 60.0f, float aspect = 1.0f) {
  Camera cam;
  cam.position = pos;
  cam.target   = target;
  cam.up       = Vec3::Up();
  cam.fovY     = fovY;
  cam.aspect   = aspect;
  cam.zNear    = 0.1f;
  cam.zFar     = 1000.0f;
  return cam;
}

// ============================================================================
// Test 1 — Activation / deactivation
// ============================================================================

TEST_CASE("TransformGizmo: inactive by default", "[gizmo]") {
  TransformGizmo g;
  CHECK(!g.IsActive());
  CHECK(g.GetMode() == GizmoMode::None);
}

TEST_CASE("TransformGizmo: Activate sets mode and IsActive", "[gizmo]") {
  TransformGizmo g;
  g.Activate(GizmoMode::Translate, Vec3::Zero(), Quaternion::Identity(), Vec3::One());
  CHECK(g.IsActive());
  CHECK(g.GetMode() == GizmoMode::Translate);
}

TEST_CASE("TransformGizmo: Activate switches mode", "[gizmo]") {
  TransformGizmo g;
  g.Activate(GizmoMode::Rotate, Vec3::Zero(), Quaternion::Identity(), Vec3::One());
  CHECK(g.GetMode() == GizmoMode::Rotate);
  g.Activate(GizmoMode::Scale, Vec3::Zero(), Quaternion::Identity(), Vec3::One());
  CHECK(g.GetMode() == GizmoMode::Scale);
}

TEST_CASE("TransformGizmo: Deactivate clears mode", "[gizmo]") {
  TransformGizmo g;
  g.Activate(GizmoMode::Translate, Vec3::Zero(), Quaternion::Identity(), Vec3::One());
  REQUIRE(g.IsActive());
  g.Deactivate();
  CHECK(!g.IsActive());
  CHECK(g.GetMode() == GizmoMode::None);
}

// ============================================================================
// Test 2 — HandleSize (screen-constant size = distance * 0.15)
// ============================================================================

TEST_CASE("TransformGizmo: HandleSize scales with camera distance", "[gizmo]") {
  TransformGizmo g;
  g.Activate(GizmoMode::Translate, Vec3::Zero(), Quaternion::Identity(), Vec3::One());

  Camera cam10 = MakeCamera({0.0f, 0.0f, 10.0f}, Vec3::Zero());
  CHECK(g.HandleSize(cam10) == Approx(1.5f).margin(1e-4f));

  Camera cam20 = MakeCamera({0.0f, 0.0f, 20.0f}, Vec3::Zero());
  CHECK(g.HandleSize(cam20) == Approx(3.0f).margin(1e-4f));
}

// ============================================================================
// Test 3 — WorldToScreen projection
// ============================================================================

TEST_CASE("TransformGizmo: WorldToScreen projects origin to screen centre", "[gizmo]") {
  // Camera looking straight down -Z at origin; object at origin should map to
  // screen centre.
  Camera cam = MakeCamera({0.0f, 0.0f, 10.0f}, Vec3::Zero(), 60.0f, 1.0f);

  float sx, sy;
  bool visible = TransformGizmo::WorldToScreen(Vec3::Zero(), cam, 800, 800, sx, sy);
  CHECK(visible);
  CHECK(sx == Approx(400.0f).margin(1.0f));
  CHECK(sy == Approx(400.0f).margin(1.0f));
}

TEST_CASE("TransformGizmo: WorldToScreen returns false for point behind camera", "[gizmo]") {
  Camera cam = MakeCamera({0.0f, 0.0f, 10.0f}, Vec3::Zero());

  // Point behind the camera (further along +Z than the eye)
  float sx, sy;
  bool visible = TransformGizmo::WorldToScreen({0.0f, 0.0f, 20.0f}, cam, 800, 800, sx, sy);
  // w will be very small or negative → not visible
  CHECK(!visible);
}

// ============================================================================
// Test 4 — RayHitPlane
// ============================================================================

TEST_CASE("TransformGizmo: RayHitPlane hits XZ plane from above", "[gizmo]") {
  Ray ray;
  ray.origin    = {0.0f, 5.0f, 0.0f};
  ray.direction = {0.0f, -1.0f, 0.0f};  // straight down

  Vec3 hitPt;
  bool hit = TransformGizmo::RayHitPlane(ray, {0.0f, 1.0f, 0.0f}, Vec3::Zero(), hitPt);
  REQUIRE(hit);
  CHECK(hitPt.x == Approx(0.0f).margin(1e-4f));
  CHECK(hitPt.y == Approx(0.0f).margin(1e-4f));
  CHECK(hitPt.z == Approx(0.0f).margin(1e-4f));
}

TEST_CASE("TransformGizmo: RayHitPlane hits offset plane", "[gizmo]") {
  Ray ray;
  ray.origin    = {1.0f, 5.0f, 2.0f};
  ray.direction = {0.0f, -1.0f, 0.0f};

  Vec3 planePoint = {0.0f, 3.0f, 0.0f};
  Vec3 hitPt;
  bool hit = TransformGizmo::RayHitPlane(ray, {0.0f, 1.0f, 0.0f}, planePoint, hitPt);
  REQUIRE(hit);
  CHECK(hitPt.x == Approx(1.0f).margin(1e-4f));
  CHECK(hitPt.y == Approx(3.0f).margin(1e-4f));
  CHECK(hitPt.z == Approx(2.0f).margin(1e-4f));
}

TEST_CASE("TransformGizmo: RayHitPlane misses parallel ray", "[gizmo]") {
  Ray ray;
  ray.origin    = {0.0f, 5.0f, 0.0f};
  ray.direction = {1.0f, 0.0f, 0.0f};  // horizontal — parallel to XZ plane

  Vec3 hitPt;
  bool hit = TransformGizmo::RayHitPlane(ray, {0.0f, 1.0f, 0.0f}, Vec3::Zero(), hitPt);
  CHECK(!hit);
}

TEST_CASE("TransformGizmo: RayHitPlane misses ray pointing away", "[gizmo]") {
  Ray ray;
  ray.origin    = {0.0f, 5.0f, 0.0f};
  ray.direction = {0.0f, 1.0f, 0.0f};  // pointing up, plane below at y=0

  Vec3 hitPt;
  bool hit = TransformGizmo::RayHitPlane(ray, {0.0f, 1.0f, 0.0f}, Vec3::Zero(), hitPt);
  CHECK(!hit);  // t would be negative
}

// ============================================================================
// Test 5 — RayClosestOnLine
// ============================================================================

TEST_CASE("TransformGizmo: RayClosestOnLine perpendicular ray", "[gizmo]") {
  // Ray coming from above the X axis, pointing straight down.
  // Closest point on the X axis should be directly below the ray origin.
  Ray ray;
  ray.origin    = {3.0f, 5.0f, 0.0f};
  ray.direction = {0.0f, -1.0f, 0.0f};

  Vec3 closest = TransformGizmo::RayClosestOnLine(ray, Vec3::Zero(), {1.0f, 0.0f, 0.0f});
  CHECK(closest.x == Approx(3.0f).margin(1e-4f));
  CHECK(closest.y == Approx(0.0f).margin(1e-4f));
  CHECK(closest.z == Approx(0.0f).margin(1e-4f));
}

TEST_CASE("TransformGizmo: RayClosestOnLine parallel ray projects origin", "[gizmo]") {
  // Ray parallel to X axis at y=1: closest point on X-axis line is directly below ray origin.
  // w0 = ray.origin - lineOrigin = {3,1,0}; d = dot({1,0,0}, {3,1,0}) = 3.
  // Parallel branch uses s = d/a = 3/1 = 3, so closest = {3,0,0}.
  Ray ray;
  ray.origin    = {3.0f, 1.0f, 0.0f};
  ray.direction = {1.0f, 0.0f, 0.0f};

  Vec3 closest = TransformGizmo::RayClosestOnLine(ray, Vec3::Zero(), {1.0f, 0.0f, 0.0f});
  CHECK(closest.x == Approx(3.0f).margin(1e-4f));
  CHECK(closest.y == Approx(0.0f).margin(1e-4f));
  CHECK(closest.z == Approx(0.0f).margin(1e-4f));
}

// ============================================================================
// Test 6 — PickAxis: mouse over axis → correct axis returned
// ============================================================================

TEST_CASE("TransformGizmo: PickAxis returns X for mouse on X axis screen projection", "[gizmo]") {
  TransformGizmo g;
  // Place gizmo at origin, camera looking down -Z from distance 10.
  g.Activate(GizmoMode::Translate, Vec3::Zero(), Quaternion::Identity(), Vec3::One());

  Camera cam = MakeCamera({0.0f, 0.0f, 10.0f}, Vec3::Zero(), 60.0f, 1.0f);

  // The X axis handle tip is at (handleLen, 0, 0).
  // At distance 10, handleLen = 1.5.  Project midpoint of X axis handle to screen.
  float handleLen = g.HandleSize(cam);  // 1.5
  Vec3  midX      = Vec3::Right() * (handleLen * 0.5f);
  float sx, sy;
  bool  vis = TransformGizmo::WorldToScreen(midX, cam, 800, 800, sx, sy);
  REQUIRE(vis);

  GizmoAxis picked = g.PickAxis(sx, sy, cam, 800, 800);
  CHECK(picked == GizmoAxis::X);
}

TEST_CASE("TransformGizmo: PickAxis returns None when mouse is far from all axes", "[gizmo]") {
  TransformGizmo g;
  g.Activate(GizmoMode::Translate, Vec3::Zero(), Quaternion::Identity(), Vec3::One());

  Camera cam = MakeCamera({0.0f, 0.0f, 10.0f}, Vec3::Zero(), 60.0f, 1.0f);

  // Mouse in the corner — far from all axes.
  GizmoAxis picked = g.PickAxis(799.0f, 799.0f, cam, 800, 800);
  CHECK(picked == GizmoAxis::None);
}

// ============================================================================
// Test 7 — RayHitPlane + axis projection: translate delta math
// ============================================================================

TEST_CASE("TransformGizmo: translate delta is parallel to axis and correct magnitude", "[gizmo]") {
  // Simulate the translate drag math directly.
  // Drag from x=0 to x=2 along X axis on a camera-facing plane.
  Vec3  anchor    = {0.0f, 0.0f, 0.0f};
  Vec3  axisDir   = {1.0f, 0.0f, 0.0f};
  Vec3  camFwd    = {0.0f, 0.0f, -1.0f};  // camera looking -Z
  Ray   ray1, ray2;

  // Frame 1 hit: x=0 on plane at anchor
  ray1.origin    = {0.0f, 0.0f, 5.0f};
  ray1.direction = {0.0f, 0.0f, -1.0f};
  Vec3 hit1;
  bool ok1 = TransformGizmo::RayHitPlane(ray1, camFwd, anchor, hit1);
  REQUIRE(ok1);
  float prevOffset = Vec3::Dot(hit1 - anchor, axisDir);

  // Frame 2 hit: x=2 on same plane
  ray2.origin    = {2.0f, 0.0f, 5.0f};
  ray2.direction = {0.0f, 0.0f, -1.0f};
  Vec3 hit2;
  bool ok2 = TransformGizmo::RayHitPlane(ray2, camFwd, anchor, hit2);
  REQUIRE(ok2);
  float curOffset = Vec3::Dot(hit2 - anchor, axisDir);

  float delta = curOffset - prevOffset;
  CHECK(delta == Approx(2.0f).margin(1e-4f));
}

// ============================================================================
// Test 8 — Rotate delta math (90° arc → quarter-turn quaternion)
// ============================================================================

TEST_CASE("TransformGizmo: rotate delta math produces 90 degree rotation", "[gizmo]") {
  // Simulate rotating around the +Y axis by 90° (right-hand rule).
  // +X rotated 90° CCW around +Y becomes -Z.
  Vec3 axisNormal = {0.0f, 1.0f, 0.0f};
  Vec3 startDir   = {1.0f, 0.0f, 0.0f};    // initial tangent (+X)
  Vec3 curDir     = {0.0f, 0.0f, -1.0f};   // after 90° around +Y: -Z

  float cosA  = Vec3::Dot(startDir, curDir);                             // cos(90) = 0
  float sinA  = Vec3::Dot(Vec3::Cross(startDir, curDir), axisNormal);   // sin(90) = +1
  float angle = std::atan2(sinA, cosA);

  CHECK(angle == Approx(ToRadians(90.0f)).margin(1e-4f));

  Quaternion deltaRot = Quaternion::FromAxisAngle(axisNormal, angle);
  Quaternion expected = Quaternion::FromAxisAngle(axisNormal, ToRadians(90.0f));

  // Both should rotate +X to approximately -Z
  Vec3 rotated  = deltaRot * Vec3::Right();
  Vec3 expRoted = expected * Vec3::Right();
  CHECK(rotated.x == Approx(expRoted.x).margin(1e-4f));
  CHECK(rotated.y == Approx(expRoted.y).margin(1e-4f));
  CHECK(rotated.z == Approx(expRoted.z).margin(1e-4f));
  CHECK(rotated.z == Approx(-1.0f).margin(1e-4f));
}

// ============================================================================
// Test 9 — Scale delta math (drag half handle length → ~1.5× scale)
// ============================================================================

TEST_CASE("TransformGizmo: scale delta math for half-handle drag", "[gizmo]") {
  // handleLen = 1.5 (camera at distance 10).
  float handleLen    = 1.5f;
  float prevOffset   = 0.0f;
  float curOffset    = handleLen * 0.5f;  // drag half handle length along axis
  float delta        = curOffset - prevOffset;
  float factor       = 1.0f + delta / handleLen;  // should be 1.5

  CHECK(factor == Approx(1.5f).margin(1e-4f));
}

TEST_CASE("TransformGizmo: scale delta math for full-handle drag doubles scale", "[gizmo]") {
  float handleLen = 2.0f;
  float prevOffset = 0.0f;
  float curOffset  = handleLen;
  float factor     = 1.0f + (curOffset - prevOffset) / handleLen;
  CHECK(factor == Approx(2.0f).margin(1e-4f));
}

// ============================================================================
// Test 10 — SyncTarget updates internal state
// ============================================================================

TEST_CASE("TransformGizmo: SyncTarget updates gizmo position for HandleSize", "[gizmo]") {
  TransformGizmo g;
  g.Activate(GizmoMode::Translate, Vec3::Zero(), Quaternion::Identity(), Vec3::One());

  Camera cam = MakeCamera({0.0f, 0.0f, 10.0f}, Vec3::Zero());
  CHECK(g.HandleSize(cam) == Approx(1.5f).margin(1e-4f));

  // Move gizmo to (0,0,5) — now 5 units from camera.
  g.SyncTarget({0.0f, 0.0f, 5.0f}, Quaternion::Identity(), Vec3::One());
  CHECK(g.HandleSize(cam) == Approx(0.75f).margin(1e-4f));
}

// ============================================================================
// Test 11 — Deactivate simulates Escape dismissal
// ============================================================================

TEST_CASE("TransformGizmo: Deactivate dismisses active gizmo (simulates Escape)", "[gizmo]") {
  TransformGizmo g;
  g.Activate(GizmoMode::Scale, {1.0f, 2.0f, 3.0f}, Quaternion::Identity(), Vec3::One());
  REQUIRE(g.IsActive());
  REQUIRE(g.GetMode() == GizmoMode::Scale);

  g.Deactivate();
  CHECK(!g.IsActive());
  CHECK(g.GetMode() == GizmoMode::None);
}
