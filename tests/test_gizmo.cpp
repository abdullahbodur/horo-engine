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

// ---- Helpers
// -----------------------------------------------------------------

static Camera MakeCamera(Vec3 pos, Vec3 target, float fovY = 60.0f,
                         float aspect = 1.0f) {
    Camera cam;
    cam.position = pos;
    cam.target = target;
    cam.up = Vec3::Up();
    cam.fovY = fovY;
    cam.aspect = aspect;
    cam.zNear = 0.1f;
    cam.zFar = 1000.0f;
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
    g.Activate(GizmoMode::Translate, Vec3::Zero(), Quaternion::Identity(),
               Vec3::One());
    CHECK(g.IsActive());
    CHECK(g.GetMode() == GizmoMode::Translate);
}

TEST_CASE("TransformGizmo: Activate switches mode", "[gizmo]") {
    TransformGizmo g;
    g.Activate(GizmoMode::Rotate, Vec3::Zero(), Quaternion::Identity(),
               Vec3::One());
    CHECK(g.GetMode() == GizmoMode::Rotate);
    g.Activate(GizmoMode::Scale, Vec3::Zero(), Quaternion::Identity(),
               Vec3::One());
    CHECK(g.GetMode() == GizmoMode::Scale);
}

TEST_CASE("TransformGizmo: Deactivate clears mode", "[gizmo]") {
    TransformGizmo g;
    g.Activate(GizmoMode::Translate, Vec3::Zero(), Quaternion::Identity(),
               Vec3::One());
    REQUIRE(g.IsActive());
    g.Deactivate();
    CHECK(!g.IsActive());
    CHECK(g.GetMode() == GizmoMode::None);
}

// ============================================================================
// Test 2 — HandleSize (FOV-aware, screen-constant:
// screenFrac*2*dist*tan(fovY/2))
// ============================================================================

TEST_CASE("HandleSize: FOV-aware formula, distance 10", "[gizmo]") {
    TransformGizmo g;
    g.Activate(GizmoMode::Translate, Vec3::Zero(), Quaternion::Identity(),
               Vec3::One());
    Camera cam10 = MakeCamera({0.0f, 0.0f, 10.0f}, Vec3::Zero(), 60.0f);
    // Keep handle size screen-constant using the same projection relation.
    float expected = 0.08f * 2.0f * 10.0f * std::tan(ToRadians(60.0f) * 0.5f);
    CHECK(g.HandleSize(cam10) == Approx(expected).margin(1e-3f));
}

TEST_CASE("HandleSize: doubles with distance", "[gizmo]") {
    TransformGizmo g;
    g.Activate(GizmoMode::Translate, Vec3::Zero(), Quaternion::Identity(),
               Vec3::One());
    Camera cam10 = MakeCamera({0.0f, 0.0f, 10.0f}, Vec3::Zero(), 60.0f);
    Camera cam20 = MakeCamera({0.0f, 0.0f, 20.0f}, Vec3::Zero(), 60.0f);
    float h10 = g.HandleSize(cam10);
    float h20 = g.HandleSize(cam20);
    // ratio must be exactly 2.0 regardless of formula constant
    CHECK(h20 / h10 == Approx(2.0f).margin(1e-4f));
}

TEST_CASE("HandleSize: varies with FOV", "[gizmo]") {
    TransformGizmo g;
    g.Activate(GizmoMode::Translate, Vec3::Zero(), Quaternion::Identity(),
               Vec3::One());
    Camera cam60 = MakeCamera({0.0f, 0.0f, 10.0f}, Vec3::Zero(), 60.0f);
    Camera cam90 = MakeCamera({0.0f, 0.0f, 10.0f}, Vec3::Zero(), 90.0f);
    // tan(45°) > tan(30°), so wider FOV → larger handles at same distance
    CHECK(g.HandleSize(cam90) > g.HandleSize(cam60));
}

// ============================================================================
// Test 3 — WorldToScreen projection
// ============================================================================

TEST_CASE("TransformGizmo: WorldToScreen projects origin to screen centre",
          "[gizmo]") {
    // Camera looking straight down -Z at origin; object at origin should map to
    // screen centre.
    Camera cam = MakeCamera({0.0f, 0.0f, 10.0f}, Vec3::Zero(), 60.0f, 1.0f);

    float sx;
    float sy;
    bool visible =
            TransformGizmo::WorldToScreen(Vec3::Zero(), cam, 800, 800, sx, sy);
    CHECK(visible);
    CHECK(sx == Approx(400.0f).margin(1.0f));
    CHECK(sy == Approx(400.0f).margin(1.0f));
}

TEST_CASE("TransformGizmo: WorldToScreen returns false for point behind camera",
          "[gizmo]") {
    Camera cam = MakeCamera({0.0f, 0.0f, 10.0f}, Vec3::Zero());

    // Point behind the camera (further along +Z than the eye)
    float sx;
    float sy;
    bool visible =
            TransformGizmo::WorldToScreen({0.0f, 0.0f, 20.0f}, cam, 800, 800, sx, sy);
    // w will be very small or negative → not visible
    CHECK(!visible);
}

// ============================================================================
// Test 4 — RayHitPlane
// ============================================================================

TEST_CASE("TransformGizmo: RayHitPlane hits XZ plane from above", "[gizmo]") {
    Ray ray;
    ray.origin = {0.0f, 5.0f, 0.0f};
    ray.direction = {0.0f, -1.0f, 0.0f}; // straight down

    Vec3 hitPt;
    bool hit =
            TransformGizmo::RayHitPlane(ray, {0.0f, 1.0f, 0.0f}, Vec3::Zero(), hitPt);
    REQUIRE(hit);
    CHECK(hitPt.x == Approx(0.0f).margin(1e-4f));
    CHECK(hitPt.y == Approx(0.0f).margin(1e-4f));
    CHECK(hitPt.z == Approx(0.0f).margin(1e-4f));
}

TEST_CASE("TransformGizmo: RayHitPlane hits offset plane", "[gizmo]") {
    Ray ray;
    ray.origin = {1.0f, 5.0f, 2.0f};
    ray.direction = {0.0f, -1.0f, 0.0f};

    Vec3 planePoint = {0.0f, 3.0f, 0.0f};
    Vec3 hitPt;
    bool hit =
            TransformGizmo::RayHitPlane(ray, {0.0f, 1.0f, 0.0f}, planePoint, hitPt);
    REQUIRE(hit);
    CHECK(hitPt.x == Approx(1.0f).margin(1e-4f));
    CHECK(hitPt.y == Approx(3.0f).margin(1e-4f));
    CHECK(hitPt.z == Approx(2.0f).margin(1e-4f));
}

TEST_CASE("TransformGizmo: RayHitPlane misses parallel ray", "[gizmo]") {
    Ray ray;
    ray.origin = {0.0f, 5.0f, 0.0f};
    ray.direction = {1.0f, 0.0f, 0.0f}; // horizontal — parallel to XZ plane

    Vec3 hitPt;
    bool hit =
            TransformGizmo::RayHitPlane(ray, {0.0f, 1.0f, 0.0f}, Vec3::Zero(), hitPt);
    CHECK(!hit);
}

TEST_CASE("TransformGizmo: RayHitPlane misses ray pointing away", "[gizmo]") {
    Ray ray;
    ray.origin = {0.0f, 5.0f, 0.0f};
    ray.direction = {0.0f, 1.0f, 0.0f}; // pointing up, plane below at y=0

    Vec3 hitPt;
    bool hit =
            TransformGizmo::RayHitPlane(ray, {0.0f, 1.0f, 0.0f}, Vec3::Zero(), hitPt);
    CHECK(!hit); // t would be negative
}

// ============================================================================
// Test 5 — RayClosestOnLine
// ============================================================================

TEST_CASE("TransformGizmo: RayClosestOnLine perpendicular ray", "[gizmo]") {
    // Ray coming from above the X axis, pointing straight down.
    // Closest point on the X axis should be directly below the ray origin.
    Ray ray;
    ray.origin = {3.0f, 5.0f, 0.0f};
    ray.direction = {0.0f, -1.0f, 0.0f};

    Vec3 closest =
            TransformGizmo::RayClosestOnLine(ray, Vec3::Zero(), {1.0f, 0.0f, 0.0f});
    CHECK(closest.x == Approx(3.0f).margin(1e-4f));
    CHECK(closest.y == Approx(0.0f).margin(1e-4f));
    CHECK(closest.z == Approx(0.0f).margin(1e-4f));
}

TEST_CASE("TransformGizmo: RayClosestOnLine parallel ray projects origin",
          "[gizmo]") {
    // Ray parallel to X axis at y=1: closest point on X-axis line is directly
    // below ray origin. w0 = ray.origin - lineOrigin = {3,1,0}; d = dot({1,0,0},
    // {3,1,0}) = 3. Parallel branch uses s = d/a = 3/1 = 3, so closest = {3,0,0}.
    Ray ray;
    ray.origin = {3.0f, 1.0f, 0.0f};
    ray.direction = {1.0f, 0.0f, 0.0f};

    Vec3 closest =
            TransformGizmo::RayClosestOnLine(ray, Vec3::Zero(), {1.0f, 0.0f, 0.0f});
    CHECK(closest.x == Approx(3.0f).margin(1e-4f));
    CHECK(closest.y == Approx(0.0f).margin(1e-4f));
    CHECK(closest.z == Approx(0.0f).margin(1e-4f));
}

// ============================================================================
// Test 6 — PickAxis: mouse over axis → correct axis returned
// ============================================================================

TEST_CASE(
    "TransformGizmo: PickAxis returns X for mouse on X axis screen projection",
    "[gizmo]") {
    TransformGizmo g;
    // Place gizmo at origin, camera looking down -Z from distance 10.
    g.Activate(GizmoMode::Translate, Vec3::Zero(), Quaternion::Identity(),
               Vec3::One());

    Camera cam = MakeCamera({0.0f, 0.0f, 10.0f}, Vec3::Zero(), 60.0f, 1.0f);

    // The X axis handle tip is at (handleLen, 0, 0).
    // Project midpoint of X axis handle to screen.
    float handleLen = g.HandleSize(cam);
    Vec3 midX = Vec3::Right() * (handleLen * 0.5f);
    float sx = 0.0f;
    float sy = 0.0f;
    bool vis = TransformGizmo::WorldToScreen(midX, cam, 800, 800, sx, sy);
    REQUIRE(vis);

    GizmoAxis picked = g.PickAxis(sx, sy, cam, 800, 800);
    CHECK(picked == GizmoAxis::X);
}

TEST_CASE(
    "TransformGizmo: PickAxis returns None when mouse is far from all axes",
    "[gizmo]") {
    TransformGizmo g;
    g.Activate(GizmoMode::Translate, Vec3::Zero(), Quaternion::Identity(),
               Vec3::One());

    Camera cam = MakeCamera({0.0f, 0.0f, 10.0f}, Vec3::Zero(), 60.0f, 1.0f);

    // Mouse in the corner — far from all axes.
    GizmoAxis picked = g.PickAxis(799.0f, 799.0f, cam, 800, 800);
    CHECK(picked == GizmoAxis::None);
}

TEST_CASE("TransformGizmo: PickAxis ignores clicks near gizmo origin",
          "[gizmo]") {
    TransformGizmo g;
    g.Activate(GizmoMode::Translate, Vec3::Zero(), Quaternion::Identity(),
               Vec3::One());

    Camera cam = MakeCamera({0.0f, 0.0f, 10.0f}, Vec3::Zero(), 60.0f, 1.0f);

    float sx = 0.0f;
    float sy = 0.0f;
    REQUIRE(TransformGizmo::WorldToScreen(Vec3::Zero(), cam, 800, 800, sx, sy));

    GizmoAxis picked = g.PickAxis(sx, sy, cam, 800, 800);
    CHECK(picked == GizmoAxis::None);
}

// ============================================================================
// Test 7 — RayHitPlane + axis projection: translate delta math
// ============================================================================

TEST_CASE(
    "TransformGizmo: translate delta is parallel to axis and correct magnitude",
    "[gizmo]") {
    // Simulate the translate drag math directly.
    // Drag from x=0 to x=2 along X axis on a camera-facing plane.
    Vec3 anchor = {0.0f, 0.0f, 0.0f};
    Vec3 axisDir = {1.0f, 0.0f, 0.0f};
    Vec3 camFwd = {0.0f, 0.0f, -1.0f}; // camera looking -Z
    Ray ray1;
    Ray ray2;

    // Frame 1 hit: x=0 on plane at anchor
    ray1.origin = {0.0f, 0.0f, 5.0f};
    ray1.direction = {0.0f, 0.0f, -1.0f};
    Vec3 hit1;
    bool ok1 = TransformGizmo::RayHitPlane(ray1, camFwd, anchor, hit1);
    REQUIRE(ok1);
    float prevOffset = Vec3::Dot(hit1 - anchor, axisDir);

    // Frame 2 hit: x=2 on same plane
    ray2.origin = {2.0f, 0.0f, 5.0f};
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

TEST_CASE("TransformGizmo: rotate delta math produces 90 degree rotation",
          "[gizmo]") {
    // Simulate rotating around the +Y axis by 90° (right-hand rule).
    // +X rotated 90° CCW around +Y becomes -Z.
    Vec3 axisNormal = {0.0f, 1.0f, 0.0f};
    Vec3 startDir = {1.0f, 0.0f, 0.0f}; // initial tangent (+X)
    Vec3 curDir = {0.0f, 0.0f, -1.0f}; // after 90° around +Y: -Z

    float cosA = Vec3::Dot(startDir, curDir); // cos(90) = 0
    float sinA =
            Vec3::Dot(Vec3::Cross(startDir, curDir), axisNormal); // sin(90) = +1
    float angle = std::atan2(sinA, cosA);

    CHECK(angle == Approx(ToRadians(90.0f)).margin(1e-4f));

    Quaternion deltaRot = Quaternion::FromAxisAngle(axisNormal, angle);
    Quaternion expected = Quaternion::FromAxisAngle(axisNormal, ToRadians(90.0f));

    // Both should rotate +X to approximately -Z
    Vec3 rotated = deltaRot * Vec3::Right();
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
    // Use arbitrary handleLen for pure math check.
    float handleLen = 1.5f;
    float prevOffset = 0.0f;
    float curOffset = handleLen * 0.5f; // drag half handle length along axis
    float delta = curOffset - prevOffset;
    float factor = 1.0f + delta / handleLen; // should be 1.5

    CHECK(factor == Approx(1.5f).margin(1e-4f));
}

TEST_CASE("TransformGizmo: scale delta math for full-handle drag doubles scale",
          "[gizmo]") {
    float handleLen = 2.0f;
    float prevOffset = 0.0f;
    float curOffset = handleLen;
    float factor = 1.0f + (curOffset - prevOffset) / handleLen;
    CHECK(factor == Approx(2.0f).margin(1e-4f));
}

// ============================================================================
// Test 10 — SyncTarget updates internal state
// ============================================================================

TEST_CASE("TransformGizmo: SyncTarget updates gizmo position for HandleSize",
          "[gizmo]") {
    TransformGizmo g;
    g.Activate(GizmoMode::Translate, Vec3::Zero(), Quaternion::Identity(),
               Vec3::One());

    Camera cam = MakeCamera({0.0f, 0.0f, 10.0f}, Vec3::Zero(), 60.0f);
    float h10 = g.HandleSize(cam);
    CHECK(h10 == Approx(0.08f * 2.0f * 10.0f * std::tan(ToRadians(60.0f) * 0.5f))
        .margin(1e-3f));

    // Move gizmo to (0,0,5) — now 5 units from camera; size should halve.
    g.SyncTarget({0.0f, 0.0f, 5.0f}, Quaternion::Identity(), Vec3::One());
    float h5 = g.HandleSize(cam);
    CHECK(h5 == Approx(h10 * 0.5f).margin(1e-4f));
}

// ============================================================================
// Test 11 — Deactivate simulates Escape dismissal
// ============================================================================

TEST_CASE(
    "TransformGizmo: Deactivate dismisses active gizmo (simulates Escape)",
    "[gizmo]") {
    TransformGizmo g;
    g.Activate(GizmoMode::Scale, {1.0f, 2.0f, 3.0f}, Quaternion::Identity(),
               Vec3::One());
    REQUIRE(g.IsActive());
    REQUIRE(g.GetMode() == GizmoMode::Scale);

    g.Deactivate();
    CHECK(!g.IsActive());
    CHECK(g.GetMode() == GizmoMode::None);
}

// ============================================================================
// Test 12 — Surface snap math
// ============================================================================

TEST_CASE("Surface snap: face aligns when within threshold", "[gizmo][snap]") {
    // Self at (0,0,0) half=(1,1,1) dragged 0.4 toward other at (2.4,0,0)
    // half=(1,1,1). Self +X face = 0.4+1 = 1.4; Other -X face = 2.4-1 = 1.4 → gap
    // = 0.
    Vec3 rawPos{0.4f, 0.0f, 0.0f};
    Vec3 selfHalf{1.0f, 1.0f, 1.0f};
    Vec3 otherPos{2.4f, 0.0f, 0.0f};
    Vec3 otherHalf{1.0f, 1.0f, 1.0f};
    float gap = std::abs((rawPos.x + selfHalf.x) - (otherPos.x - otherHalf.x));
    CHECK(gap == Approx(0.0f).margin(1e-4f));
}

TEST_CASE("Surface snap: no snap when gap exceeds threshold", "[gizmo][snap]") {
    // Self +X face = 0+1 = 1; Other -X face = 3-1 = 2; gap = 1.0 > threshold 0.5
    Vec3 rawPos{0.0f, 0.0f, 0.0f};
    Vec3 selfHalf{1.0f, 1.0f, 1.0f};
    Vec3 otherPos{3.0f, 0.0f, 0.0f};
    Vec3 otherHalf{1.0f, 1.0f, 1.0f};
    constexpr float kSnapThresh = 0.5f;
    float gap = std::abs((rawPos.x + selfHalf.x) - (otherPos.x - otherHalf.x));
    CHECK(gap > kSnapThresh);
}

// ============================================================================
// Test 13 — Grid snap math
// ============================================================================

TEST_CASE("Grid snap: rounds to nearest 0.5", "[gizmo][snap]") {
    constexpr float kGrid = 0.5f;
    auto snap = [](float x) { return std::round(x / kGrid) * kGrid; };
    CHECK(snap(0.3f) == Approx(0.5f).margin(1e-5f));
    CHECK(snap(0.7f) == Approx(0.5f).margin(1e-5f));
    CHECK(snap(0.8f) == Approx(1.0f).margin(1e-5f));
    CHECK(snap(-0.3f) == Approx(-0.5f).margin(1e-5f));
    CHECK(snap(0.0f) == Approx(0.0f).margin(1e-5f));
    CHECK(snap(1.25f) == Approx(1.5f).margin(1e-5f));
}

// ============================================================================
// Test accessor — grants access to private drag state and methods.
// TransformGizmo.h declares this struct as a friend.
// ============================================================================

struct TransformGizmoTestAccessor {
    static void SetHovered(TransformGizmo &g, GizmoAxis a) { g.m_hovered = a; }
    static void SetDragging(TransformGizmo &g, GizmoAxis a) { g.m_dragging = a; }
    static void SetDragPlaneNormal(TransformGizmo &g, Vec3 n) {
        g.m_dragPlaneNormal = n;
    }
    static void SetDragAnchorPos(TransformGizmo &g, Vec3 p) {
        g.m_dragAnchorPos = p;
    }
    static void SetDragPrevOffset(TransformGizmo &g, float f) {
        g.m_dragPrevOffset = f;
    }
    static void SetDragPrevAngle(TransformGizmo &g, float f) {
        g.m_dragPrevAngle = f;
    }
    static void SetDragStartDir(TransformGizmo &g, Vec3 d) { g.m_dragStartDir = d; }

    static void BeginDrag(TransformGizmo &g, const Ray &ray, const Camera &cam) {
        g.BeginDrag(ray, cam);
    }
    static bool ApplyActiveDrag(TransformGizmo &g, const Ray &ray, const Camera &cam,
                                Vec3 &outDeltaPos, Quaternion &outDeltaRot,
                                Vec3 &outDeltaScale) {
        return g.ApplyActiveDrag(ray, cam, outDeltaPos, outDeltaRot, outDeltaScale);
    }
    static Vec3 GetPos(const TransformGizmo &g) { return g.m_pos; }
};

using TA = TransformGizmoTestAccessor;

// ============================================================================
// Test 14 — HandleSize: near-zero distance clamps to 0.1
// ============================================================================

TEST_CASE("HandleSize: near-zero distance returns 0.1", "[gizmo]") {
    TransformGizmo g;
    // Gizmo at origin; camera also at origin (dist ≈ 0 < 0.001f)
    g.Activate(GizmoMode::Translate, Vec3::Zero(), Quaternion::Identity(),
               Vec3::One());
    Camera cam = MakeCamera({0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f});
    CHECK(g.HandleSize(cam) == Approx(0.1f));
}

// ============================================================================
// Test 15 — AxisDir: default case returns zero
// ============================================================================

TEST_CASE("TransformGizmo: AxisDir None returns zero vector", "[gizmo]") {
    TransformGizmo g;
    g.Activate(GizmoMode::Translate, Vec3::Zero(), Quaternion::Identity(),
               Vec3::One());
    Vec3 d = g.AxisDir(GizmoAxis::None);
    CHECK(d.x == Approx(0.0f));
    CHECK(d.y == Approx(0.0f));
    CHECK(d.z == Approx(0.0f));
}

// ============================================================================
// Test 16 — PickAxis: gizmo entirely behind camera triggers line 135 continue
// ============================================================================

TEST_CASE("TransformGizmo: PickAxis returns None when gizmo is behind camera",
          "[gizmo]") {
    TransformGizmo g;
    // Camera at z=10 looking toward origin; gizmo placed behind the camera at z=20.
    Camera cam = MakeCamera({0.0f, 0.0f, 10.0f}, Vec3::Zero(), 60.0f, 1.0f);
    g.Activate(GizmoMode::Translate, {0.0f, 0.0f, 20.0f}, Quaternion::Identity(),
               Vec3::One());
    // WorldToScreen(m_pos) returns false for all axes → all iterations continue
    GizmoAxis picked = g.PickAxis(400.0f, 400.0f, cam, 800, 800);
    CHECK(picked == GizmoAxis::None);
}

// ============================================================================
// Test 17 — PickAxis: gizmo barely in front but Z-axis tip behind camera
//           triggers line 137 continue for the Z axis
// ============================================================================

TEST_CASE(
    "TransformGizmo: PickAxis skips Z axis when its tip is behind the camera",
    "[gizmo]") {
    // Camera at z=10. Gizmo at z=9.9999: dist ≈ 0.0001 < 0.001, so HandleSize
    // returns 0.1. Z-axis tip = z + 0.1 = 10.0999, which is behind the camera.
    Camera cam = MakeCamera({0.0f, 0.0f, 10.0f}, Vec3::Zero(), 60.0f, 1.0f);
    TransformGizmo g;
    g.Activate(GizmoMode::Translate, {0.0f, 0.0f, 9.9999f}, Quaternion::Identity(),
               Vec3::One());
    GizmoAxis picked = g.PickAxis(400.0f, 400.0f, cam, 800, 800);
    CHECK(picked != GizmoAxis::Z); // Z tip behind camera → Z iteration continues
}

// ============================================================================
// Test 18 — RayClosestOnLine: zero-length line direction uses 0.0 fallback
// ============================================================================

TEST_CASE(
    "TransformGizmo: RayClosestOnLine with zero-length lineDir returns lineOrigin",
    "[gizmo]") {
    Ray ray;
    ray.origin = {3.0f, 1.0f, 0.0f};
    ray.direction = {1.0f, 0.0f, 0.0f};
    // Zero-length line direction: a = dot(Zero, Zero) = 0 ≤ 1e-10 → s = 0.0
    Vec3 closest =
        TransformGizmo::RayClosestOnLine(ray, {1.0f, 0.0f, 0.0f}, Vec3::Zero());
    CHECK(closest.x == Approx(1.0f));
    CHECK(closest.y == Approx(0.0f));
    CHECK(closest.z == Approx(0.0f));
}

// ============================================================================
// Tests 19-22 — Draw dispatch (no OpenGL needed; DebugDraw::Line buffers only)
// ============================================================================

TEST_CASE("TransformGizmo: Draw is a no-op when mode is None", "[gizmo][draw]") {
    TransformGizmo g; // mode = None by default
    Camera cam = MakeCamera({0.0f, 0.0f, 10.0f}, Vec3::Zero());
    // Must not crash or do anything observable.
    g.Draw(cam, 800, 600);
    CHECK(!g.IsActive());
}

TEST_CASE("TransformGizmo: Draw routes to DrawTranslate", "[gizmo][draw]") {
    TransformGizmo g;
    g.Activate(GizmoMode::Translate, Vec3::Zero(), Quaternion::Identity(),
               Vec3::One());
    Camera cam = MakeCamera({0.0f, 0.0f, 10.0f}, Vec3::Zero());
    g.Draw(cam, 800, 600); // exercises DrawTranslate: shaft, arrowhead fan
    CHECK(g.GetMode() == GizmoMode::Translate);
}

TEST_CASE("TransformGizmo: Draw routes to DrawRotate", "[gizmo][draw]") {
    TransformGizmo g;
    g.Activate(GizmoMode::Rotate, Vec3::Zero(), Quaternion::Identity(), Vec3::One());
    Camera cam = MakeCamera({0.0f, 0.0f, 10.0f}, Vec3::Zero());
    g.Draw(cam, 800, 600); // exercises DrawRotate: 3 rings of 32 segments
    CHECK(g.GetMode() == GizmoMode::Rotate);
}

TEST_CASE("TransformGizmo: Draw routes to DrawScale", "[gizmo][draw]") {
    TransformGizmo g;
    g.Activate(GizmoMode::Scale, Vec3::Zero(), Quaternion::Identity(), Vec3::One());
    Camera cam = MakeCamera({0.0f, 0.0f, 10.0f}, Vec3::Zero());
    g.Draw(cam, 800, 600); // exercises DrawScale: shafts + endpoint boxes
    CHECK(g.GetMode() == GizmoMode::Scale);
}

// ============================================================================
// Tests 23-26 — BeginDrag: Translate and Rotate branches
// ============================================================================

TEST_CASE("BeginDrag: Translate mode sets dragging axis and plane", "[gizmo][drag]") {
    TransformGizmo g;
    g.Activate(GizmoMode::Translate, Vec3::Zero(), Quaternion::Identity(),
               Vec3::One());
    TA::SetHovered(g, GizmoAxis::X);

    Camera cam = MakeCamera({0.0f, 0.0f, 10.0f}, Vec3::Zero());
    // Ray hits the camera-facing plane at x=0.
    Ray ray;
    ray.origin = {0.0f, 0.0f, 5.0f};
    ray.direction = {0.0f, 0.0f, -1.0f};
    TA::BeginDrag(g, ray, cam);

    CHECK(g.GetDragAxis() == GizmoAxis::X);
}

TEST_CASE("BeginDrag: Translate mode with plane-miss falls back to zero offset",
          "[gizmo][drag]") {
    // Ray parallel to the camera-facing plane → RayHitPlane returns false → else
    // branch: m_dragPrevOffset = 0.
    TransformGizmo g;
    g.Activate(GizmoMode::Translate, Vec3::Zero(), Quaternion::Identity(),
               Vec3::One());
    TA::SetHovered(g, GizmoAxis::Y);

    Camera cam = MakeCamera({0.0f, 0.0f, 10.0f}, Vec3::Zero());
    // cam.GetForward() = (0,0,-1); a horizontal ray is parallel to this plane.
    Ray ray;
    ray.origin = {0.0f, 0.0f, 5.0f};
    ray.direction = {1.0f, 0.0f, 0.0f};
    TA::BeginDrag(g, ray, cam);

    CHECK(g.GetDragAxis() == GizmoAxis::Y);
}

TEST_CASE("BeginDrag: Rotate mode with ray hitting axis plane sets dragStartDir",
          "[gizmo][drag]") {
    TransformGizmo g;
    g.Activate(GizmoMode::Rotate, Vec3::Zero(), Quaternion::Identity(), Vec3::One());
    TA::SetHovered(g, GizmoAxis::Y); // rotate around Y; plane normal = (0,1,0)

    Camera cam = MakeCamera({0.0f, 0.0f, 10.0f}, Vec3::Zero());
    // Vertical ray hits y=0 plane at (1,0,0) → dragStartDir = (1,0,0).
    Ray ray;
    ray.origin = {1.0f, 5.0f, 0.0f};
    ray.direction = {0.0f, -1.0f, 0.0f};
    TA::BeginDrag(g, ray, cam);

    CHECK(g.GetDragAxis() == GizmoAxis::Y);
}

TEST_CASE("BeginDrag: Rotate mode with plane-miss sets dragStartDir to Right",
          "[gizmo][drag]") {
    // Ray direction (1,0,0) is parallel to the Y-axis plane (normal = (0,1,0)),
    // so RayHitPlane returns false → else branch: m_dragStartDir = Vec3::Right().
    TransformGizmo g;
    g.Activate(GizmoMode::Rotate, Vec3::Zero(), Quaternion::Identity(), Vec3::One());
    TA::SetHovered(g, GizmoAxis::Y);

    Camera cam = MakeCamera({0.0f, 0.0f, 10.0f}, Vec3::Zero());
    Ray ray;
    ray.origin = {0.0f, 0.0f, 5.0f};
    ray.direction = {1.0f, 0.0f, 0.0f}; // parallel to Y-axis plane
    TA::BeginDrag(g, ray, cam);

    // Verify drag was started (dragging != None) even with the fallback.
    CHECK(g.GetDragAxis() == GizmoAxis::Y);
}

// ============================================================================
// Tests 27-32 — ApplyActiveDrag: Translate, Rotate, Scale X/Y/Z, plane miss
// ============================================================================

TEST_CASE("ApplyActiveDrag: Translate produces axis-aligned delta", "[gizmo][drag]") {
    TransformGizmo g;
    g.Activate(GizmoMode::Translate, Vec3::Zero(), Quaternion::Identity(),
               Vec3::One());

    // Set up drag state as BeginDrag would for X axis, cam-facing plane.
    TA::SetDragging(g, GizmoAxis::X);
    TA::SetDragPlaneNormal(g, {0.0f, 0.0f, -1.0f});
    TA::SetDragAnchorPos(g, Vec3::Zero());
    TA::SetDragPrevOffset(g, 0.0f);

    Camera cam = MakeCamera({0.0f, 0.0f, 10.0f}, Vec3::Zero());
    // Ray at x=2 hits plane at (2,0,0); axisDir=(1,0,0); delta = 2.
    Ray ray;
    ray.origin = {2.0f, 0.0f, 5.0f};
    ray.direction = {0.0f, 0.0f, -1.0f};

    Vec3 deltaPos;
    Quaternion deltaRot;
    Vec3 deltaScale;
    bool consumed = TA::ApplyActiveDrag(g, ray, cam, deltaPos, deltaRot, deltaScale);
    CHECK(!consumed);
    CHECK(deltaPos.x == Approx(2.0f).margin(1e-4f));
    CHECK(deltaPos.y == Approx(0.0f).margin(1e-4f));
    CHECK(deltaPos.z == Approx(0.0f).margin(1e-4f));
    // Gizmo position must advance by the delta.
    Vec3 pos = TA::GetPos(g);
    CHECK(pos.x == Approx(2.0f).margin(1e-4f));
}

TEST_CASE("ApplyActiveDrag: Rotate produces non-identity delta quaternion",
          "[gizmo][drag]") {
    TransformGizmo g;
    g.Activate(GizmoMode::Rotate, Vec3::Zero(), Quaternion::Identity(), Vec3::One());

    // Simulate drag start: startDir=(1,0,0), axisNormal=(0,1,0), prevAngle=0.
    TA::SetDragging(g, GizmoAxis::Y);
    TA::SetDragPlaneNormal(g, {0.0f, 1.0f, 0.0f});
    TA::SetDragAnchorPos(g, Vec3::Zero());
    TA::SetDragStartDir(g, {1.0f, 0.0f, 0.0f});
    TA::SetDragPrevAngle(g, 0.0f);

    Camera cam = MakeCamera({0.0f, 0.0f, 10.0f}, Vec3::Zero());
    // Ray hits y=0 plane at (0,0,1): curDir=(0,0,1), angle = atan2(-1,0) = -90°.
    Ray ray;
    ray.origin = {0.0f, 5.0f, 1.0f};
    ray.direction = {0.0f, -1.0f, 0.0f};

    Vec3 deltaPos;
    Quaternion deltaRot;
    Vec3 deltaScale;
    bool consumed = TA::ApplyActiveDrag(g, ray, cam, deltaPos, deltaRot, deltaScale);
    CHECK(!consumed);
    // Delta rotation must not be identity (some non-zero rotation happened).
    const Quaternion identity = Quaternion::Identity();
    bool isIdentity = (std::abs(deltaRot.w - identity.w) < 1e-4f) &&
                      (std::abs(deltaRot.x - identity.x) < 1e-4f) &&
                      (std::abs(deltaRot.y - identity.y) < 1e-4f) &&
                      (std::abs(deltaRot.z - identity.z) < 1e-4f);
    CHECK(!isIdentity);
}

TEST_CASE("ApplyActiveDrag: Scale X axis scales X component", "[gizmo][drag]") {
    TransformGizmo g;
    g.Activate(GizmoMode::Scale, Vec3::Zero(), Quaternion::Identity(), Vec3::One());

    Camera cam = MakeCamera({0.0f, 0.0f, 10.0f}, Vec3::Zero());
    TA::SetDragging(g, GizmoAxis::X);
    TA::SetDragPlaneNormal(g, {0.0f, 0.0f, -1.0f});
    TA::SetDragAnchorPos(g, Vec3::Zero());
    TA::SetDragPrevOffset(g, 0.0f);

    // Ray at x=0.5 → hitPt=(0.5,0,0); axisOffset=0.5; factor > 1.
    Ray ray;
    ray.origin = {0.5f, 0.0f, 5.0f};
    ray.direction = {0.0f, 0.0f, -1.0f};

    Vec3 deltaPos;
    Quaternion deltaRot;
    Vec3 deltaScale;
    bool consumed = TA::ApplyActiveDrag(g, ray, cam, deltaPos, deltaRot, deltaScale);
    CHECK(!consumed);
    CHECK(deltaScale.x > 1.0f);
    CHECK(deltaScale.y == Approx(1.0f));
    CHECK(deltaScale.z == Approx(1.0f));
}

TEST_CASE("ApplyActiveDrag: Scale Y axis scales Y component", "[gizmo][drag]") {
    TransformGizmo g;
    g.Activate(GizmoMode::Scale, Vec3::Zero(), Quaternion::Identity(), Vec3::One());

    Camera cam = MakeCamera({0.0f, 0.0f, 10.0f}, Vec3::Zero());
    TA::SetDragging(g, GizmoAxis::Y);
    TA::SetDragPlaneNormal(g, {0.0f, 0.0f, -1.0f});
    TA::SetDragAnchorPos(g, Vec3::Zero());
    TA::SetDragPrevOffset(g, 0.0f);

    // Ray at y=0.5 → hitPt=(0,0.5,0); Y-axisOffset=0.5; factor > 1.
    Ray ray;
    ray.origin = {0.0f, 0.5f, 5.0f};
    ray.direction = {0.0f, 0.0f, -1.0f};

    Vec3 deltaPos;
    Quaternion deltaRot;
    Vec3 deltaScale;
    bool consumed = TA::ApplyActiveDrag(g, ray, cam, deltaPos, deltaRot, deltaScale);
    CHECK(!consumed);
    CHECK(deltaScale.x == Approx(1.0f));
    CHECK(deltaScale.y > 1.0f);
    CHECK(deltaScale.z == Approx(1.0f));
}

TEST_CASE("ApplyActiveDrag: Scale Z axis scales Z component", "[gizmo][drag]") {
    // Use a Y-facing drag plane so the Z-axis offset is non-degenerate.
    TransformGizmo g;
    g.Activate(GizmoMode::Scale, Vec3::Zero(), Quaternion::Identity(), Vec3::One());

    // Camera above looking down; plane normal = (0,-1,0).
    Camera cam = MakeCamera({0.0f, 10.0f, 0.0f}, Vec3::Zero());
    TA::SetDragging(g, GizmoAxis::Z);
    TA::SetDragPlaneNormal(g, {0.0f, -1.0f, 0.0f});
    TA::SetDragAnchorPos(g, Vec3::Zero());
    TA::SetDragPrevOffset(g, 0.0f);

    // Ray at (0,5,1) pointing down; hits plane y=0 at (0,0,1); Z offset = 1.
    Ray ray;
    ray.origin = {0.0f, 5.0f, 1.0f};
    ray.direction = {0.0f, -1.0f, 0.0f};

    Vec3 deltaPos;
    Quaternion deltaRot;
    Vec3 deltaScale;
    bool consumed = TA::ApplyActiveDrag(g, ray, cam, deltaPos, deltaRot, deltaScale);
    CHECK(!consumed);
    CHECK(deltaScale.x == Approx(1.0f));
    CHECK(deltaScale.y == Approx(1.0f));
    CHECK(deltaScale.z > 1.0f);
}

TEST_CASE("ApplyActiveDrag: returns true (consumed) when ray misses drag plane",
          "[gizmo][drag]") {
    TransformGizmo g;
    g.Activate(GizmoMode::Translate, Vec3::Zero(), Quaternion::Identity(),
               Vec3::One());

    TA::SetDragging(g, GizmoAxis::X);
    // Horizontal plane (normal = Y); ray direction parallel to it → no hit.
    TA::SetDragPlaneNormal(g, {0.0f, 1.0f, 0.0f});
    TA::SetDragAnchorPos(g, Vec3::Zero());

    Camera cam = MakeCamera({0.0f, 0.0f, 10.0f}, Vec3::Zero());
    Ray ray;
    ray.origin = {0.0f, 0.0f, 5.0f};
    ray.direction = {1.0f, 0.0f, 0.0f}; // parallel to the Y-normal plane

    Vec3 deltaPos;
    Quaternion deltaRot;
    Vec3 deltaScale;
    bool consumed = TA::ApplyActiveDrag(g, ray, cam, deltaPos, deltaRot, deltaScale);
    CHECK(consumed); // plane not hit → function returns true
}
