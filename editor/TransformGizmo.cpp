#include "editor/TransformGizmo.h"

#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>

#include "math/Mat4.h"
#include "math/MathUtils.h"
#include "math/Vec4.h"
#include "renderer/DebugDraw.h"

namespace Monolith {
namespace Editor {

// ---- Activation / sync -------------------------------------------------------

void TransformGizmo::Activate(GizmoMode mode, Vec3 targetPos, Quaternion targetRot,
                               Vec3 targetScale) {
  m_mode     = mode;
  m_pos      = targetPos;
  m_rot      = targetRot;
  m_scale    = targetScale;
  m_hovered  = GizmoAxis::None;
  m_dragging = GizmoAxis::None;
  m_prevMouseL = false;
}

void TransformGizmo::Deactivate() {
  m_mode     = GizmoMode::None;
  m_dragging = GizmoAxis::None;
  m_hovered  = GizmoAxis::None;
}

void TransformGizmo::SyncTarget(Vec3 pos, Quaternion rot, Vec3 scale) {
  // Always sync — EditorLayer calls this with the already-updated position
  // from the previous frame's delta, so there is no double-counting.
  m_pos   = pos;
  m_rot   = rot;
  m_scale = scale;
}

// ---- Public math helpers ----------------------------------------------------

float TransformGizmo::HandleSize(const Camera& cam) const {
  float dist = (cam.position - m_pos).Length();
  return dist * 0.15f;
}

Vec3 TransformGizmo::AxisDir(GizmoAxis axis) const {
  switch (axis) {
    case GizmoAxis::X: return {1.0f, 0.0f, 0.0f};
    case GizmoAxis::Y: return {0.0f, 1.0f, 0.0f};
    case GizmoAxis::Z: return {0.0f, 0.0f, 1.0f};
    default:           return Vec3::Zero();
  }
}

bool TransformGizmo::WorldToScreen(const Vec3& p, const Camera& cam, int w, int h,
                                    float& sx, float& sy) {
  Mat4 vp    = cam.GetViewProjection();
  Vec4 clip  = vp * Vec4(p.x, p.y, p.z, 1.0f);
  if (clip.w < 1e-6f)
    return false;
  float ndcX = clip.x / clip.w;
  float ndcY = clip.y / clip.w;
  // Screen: (0,0) = top-left; NDC Y is flipped relative to screen Y.
  sx = (ndcX * 0.5f + 0.5f) * static_cast<float>(w);
  sy = (1.0f - (ndcY * 0.5f + 0.5f)) * static_cast<float>(h);
  return true;
}

bool TransformGizmo::RayHitPlane(const Ray& ray, const Vec3& normal, const Vec3& point,
                                  Vec3& outHit) {
  float denom = Vec3::Dot(normal, ray.direction);
  if (std::abs(denom) < 1e-6f)
    return false;
  float t = Vec3::Dot(normal, point - ray.origin) / denom;
  if (t < 0.0f)
    return false;
  outHit = ray.origin + ray.direction * t;
  return true;
}

Vec3 TransformGizmo::RayClosestOnLine(const Ray& ray, const Vec3& lineOrigin,
                                       const Vec3& lineDir) {
  // Minimize squared distance between ray point and line point.
  Vec3 w0    = ray.origin - lineOrigin;
  float a    = Vec3::Dot(lineDir, lineDir);
  float b    = Vec3::Dot(lineDir, ray.direction);
  float c    = Vec3::Dot(ray.direction, ray.direction);
  float d    = Vec3::Dot(lineDir, w0);
  float e    = Vec3::Dot(ray.direction, w0);
  float dnom = a * c - b * b;
  float s;
  if (std::abs(dnom) < 1e-10f) {
    // Lines are parallel — project ray origin onto line.
    s = (a > 1e-10f) ? d / a : 0.0f;
  } else {
    // Derived with w0 = ray.origin - lineOrigin; correct sign is c*d - b*e.
    s = (c * d - b * e) / dnom;
  }
  return lineOrigin + lineDir * s;
}

// ---- Axis picking (screen-space 2D) -----------------------------------------

GizmoAxis TransformGizmo::PickAxis(float mx, float my, const Camera& cam, int screenW,
                                    int screenH) const {
  const float handleLen        = HandleSize(cam);
  constexpr float kHitRadiusSq = 12.0f * 12.0f;  // 12-pixel threshold

  float     bestDist = kHitRadiusSq;
  GizmoAxis bestAxis = GizmoAxis::None;

  for (int i = 0; i < 3; ++i) {
    GizmoAxis axis = static_cast<GizmoAxis>(i + 1);  // X=1, Y=2, Z=3
    Vec3      tip  = m_pos + AxisDir(axis) * handleLen;

    float ox, oy, tx, ty;
    if (!WorldToScreen(m_pos, cam, screenW, screenH, ox, oy)) continue;
    if (!WorldToScreen(tip, cam, screenW, screenH, tx, ty)) continue;

    // 2D point-to-segment distance squared
    float abx    = tx - ox, aby = ty - oy;
    float apx    = mx - ox, apy = my - oy;
    float abLen2 = abx * abx + aby * aby;
    float t      = 0.0f;
    if (abLen2 > 1e-8f)
      t = std::clamp((apx * abx + apy * aby) / abLen2, 0.0f, 1.0f);
    float cx    = ox + t * abx, cy = oy + t * aby;
    float dx    = mx - cx, dy = my - cy;
    float distSq = dx * dx + dy * dy;

    if (distSq < bestDist) {
      bestDist = distSq;
      bestAxis = axis;
    }
  }
  return bestAxis;
}

// ---- Update ------------------------------------------------------------------

bool TransformGizmo::Update(GLFWwindow* window, const Camera& cam, int screenW, int screenH,
                             Vec3& outDeltaPos, Quaternion& outDeltaRot, Vec3& outDeltaScale) {
  outDeltaPos   = Vec3::Zero();
  outDeltaRot   = Quaternion::Identity();
  outDeltaScale = Vec3::One();

  if (m_mode == GizmoMode::None)
    return false;

  double dmx, dmy;
  glfwGetCursorPos(window, &dmx, &dmy);
  float mx = static_cast<float>(dmx);
  float my = static_cast<float>(dmy);

  bool currMouseL = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
  bool clicked    = currMouseL && !m_prevMouseL;
  bool released   = !currMouseL && m_prevMouseL;
  m_prevMouseL    = currMouseL;

  Ray ray = ScreenToRay(mx, my, screenW, screenH, cam);

  if (m_dragging == GizmoAxis::None) {
    // Update hover and detect drag start
    m_hovered = PickAxis(mx, my, cam, screenW, screenH);

    if (clicked && m_hovered != GizmoAxis::None) {
      m_dragging      = m_hovered;
      m_dragAnchorPos = m_pos;

      if (m_mode == GizmoMode::Rotate) {
        Vec3 axisDir = AxisDir(m_dragging);
        Vec3 hitPt;
        if (RayHitPlane(ray, axisDir, m_pos, hitPt)) {
          Vec3 diff = hitPt - m_pos;
          m_dragStartDir = (diff.LengthSq() > 1e-8f) ? diff.Normalized() : Vec3::Right();
        } else {
          m_dragStartDir = Vec3::Right();
        }
        m_dragPrevAngle  = 0.0f;
        m_dragPlaneNormal = axisDir;
      } else {
        // Translate / Scale: use camera-facing plane through object
        Vec3 camFwd = cam.GetForward();
        Vec3 hitPt;
        if (RayHitPlane(ray, camFwd, m_pos, hitPt)) {
          Vec3 axisDir     = AxisDir(m_dragging);
          m_dragPrevOffset = Vec3::Dot(hitPt - m_dragAnchorPos, axisDir);
        } else {
          m_dragPrevOffset = 0.0f;
        }
        m_dragPlaneNormal = camFwd;
      }
    }
  } else {
    // Currently dragging
    if (released) {
      m_dragging = GizmoAxis::None;
    } else {
      Vec3 hitPt;
      if (!RayHitPlane(ray, m_dragPlaneNormal, m_dragAnchorPos, hitPt))
        return true;  // can't hit plane — consume without delta

      if (m_mode == GizmoMode::Translate) {
        Vec3  axisDir   = AxisDir(m_dragging);
        float curOffset = Vec3::Dot(hitPt - m_dragAnchorPos, axisDir);
        float delta     = curOffset - m_dragPrevOffset;
        m_dragPrevOffset = curOffset;
        outDeltaPos = axisDir * delta;
        m_pos       = m_pos + outDeltaPos;  // keep gizmo visual in sync

      } else if (m_mode == GizmoMode::Rotate) {
        Vec3 axisNormal = AxisDir(m_dragging);
        Vec3 diff       = hitPt - m_dragAnchorPos;
        Vec3 curDir     = (diff.LengthSq() > 1e-8f) ? diff.Normalized() : m_dragStartDir;
        float cosA      = Vec3::Dot(m_dragStartDir, curDir);
        float sinA      = Vec3::Dot(Vec3::Cross(m_dragStartDir, curDir), axisNormal);
        float curAngle  = std::atan2(sinA, cosA);
        float deltaAngle = curAngle - m_dragPrevAngle;
        m_dragPrevAngle  = curAngle;
        outDeltaRot = Quaternion::FromAxisAngle(axisNormal, deltaAngle);

      } else if (m_mode == GizmoMode::Scale) {
        float handleLen = HandleSize(cam);
        Vec3  axisDir   = AxisDir(m_dragging);
        float curOffset = Vec3::Dot(hitPt - m_dragAnchorPos, axisDir);
        float delta     = curOffset - m_dragPrevOffset;
        m_dragPrevOffset = curOffset;
        float divisor   = (handleLen > 1e-6f) ? handleLen : 1.0f;
        float factor    = 1.0f + delta / divisor;
        if (m_dragging == GizmoAxis::X)
          outDeltaScale = {factor, 1.0f, 1.0f};
        else if (m_dragging == GizmoAxis::Y)
          outDeltaScale = {1.0f, factor, 1.0f};
        else
          outDeltaScale = {1.0f, 1.0f, factor};
      }
    }
  }

  return m_dragging != GizmoAxis::None || m_hovered != GizmoAxis::None;
}

// ---- Draw -------------------------------------------------------------------

namespace {
const Vec4 kAxisColors[3] = {
    {0.9f, 0.2f, 0.2f, 1.0f},  // X = red
    {0.2f, 0.9f, 0.2f, 1.0f},  // Y = green
    {0.2f, 0.4f, 1.0f, 1.0f},  // Z = blue
};
const Vec4 kHoverColor  = {1.0f, 1.0f, 1.0f, 1.0f};
constexpr float kTwoPi = 6.28318530718f;
}  // namespace

void TransformGizmo::DrawTranslate(const Camera& cam, int /*screenW*/, int /*screenH*/) const {
  const float handleLen = HandleSize(cam);

  for (int i = 0; i < 3; ++i) {
    GizmoAxis axis  = static_cast<GizmoAxis>(i + 1);
    Vec3      dir   = AxisDir(axis);
    Vec3      tip   = m_pos + dir * handleLen;
    Vec4      color = (axis == m_hovered || axis == m_dragging) ? kHoverColor : kAxisColors[i];

    DebugDraw::Line(m_pos, tip, color);

    // Arrowhead: 4 fan lines at 15° from tip
    Vec3 perp1 = Vec3::Cross(dir, {0.0f, 1.0f, 0.0f});
    if (perp1.LengthSq() < 1e-4f)
      perp1 = Vec3::Cross(dir, {1.0f, 0.0f, 0.0f});
    perp1       = perp1.Normalized();
    Vec3 perp2  = Vec3::Cross(dir, perp1).Normalized();

    const float arrowLen    = handleLen * 0.2f;
    const float arrowSpread = std::tan(ToRadians(15.0f)) * arrowLen;
    Vec3        arrowBase   = tip - dir * arrowLen;

    DebugDraw::Line(tip, arrowBase + perp1 * arrowSpread,  color);
    DebugDraw::Line(tip, arrowBase - perp1 * arrowSpread,  color);
    DebugDraw::Line(tip, arrowBase + perp2 * arrowSpread,  color);
    DebugDraw::Line(tip, arrowBase - perp2 * arrowSpread,  color);
  }
}

void TransformGizmo::DrawRotate(const Camera& cam, int /*screenW*/, int /*screenH*/) const {
  const float  radius    = HandleSize(cam);
  constexpr int kSegments = 32;

  // Axis-plane basis vectors: each ring lies on the plane perpendicular to its axis.
  const Vec3 kBasisU[3] = {{0.0f, 1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}};
  const Vec3 kBasisV[3] = {{0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f, 0.0f}};

  for (int i = 0; i < 3; ++i) {
    GizmoAxis axis  = static_cast<GizmoAxis>(i + 1);
    Vec4      color = (axis == m_hovered || axis == m_dragging) ? kHoverColor : kAxisColors[i];
    Vec3      uVec  = kBasisU[i];
    Vec3      vVec  = kBasisV[i];

    Vec3 prev;
    for (int j = 0; j <= kSegments; ++j) {
      float t  = kTwoPi * static_cast<float>(j) / static_cast<float>(kSegments);
      Vec3  p  = m_pos + uVec * (std::cos(t) * radius) + vVec * (std::sin(t) * radius);
      if (j > 0)
        DebugDraw::Line(prev, p, color);
      prev = p;
    }
  }
}

void TransformGizmo::DrawScale(const Camera& cam, int /*screenW*/, int /*screenH*/) const {
  const float handleLen  = HandleSize(cam);
  const float boxHalf    = handleLen * 0.08f;

  for (int i = 0; i < 3; ++i) {
    GizmoAxis axis  = static_cast<GizmoAxis>(i + 1);
    Vec3      dir   = AxisDir(axis);
    Vec3      tip   = m_pos + dir * handleLen;
    Vec4      color = (axis == m_hovered || axis == m_dragging) ? kHoverColor : kAxisColors[i];

    DebugDraw::Line(m_pos, tip, color);
    DebugDraw::Box(tip, {boxHalf, boxHalf, boxHalf}, color);
  }

  // Centre uniform-scale box (white)
  DebugDraw::Box(m_pos, {boxHalf, boxHalf, boxHalf}, {1.0f, 1.0f, 1.0f, 1.0f});
}

void TransformGizmo::Draw(const Camera& cam, int screenW, int screenH) const {
  if (m_mode == GizmoMode::None)
    return;
  switch (m_mode) {
    case GizmoMode::Translate: DrawTranslate(cam, screenW, screenH); break;
    case GizmoMode::Rotate:    DrawRotate(cam, screenW, screenH);    break;
    case GizmoMode::Scale:     DrawScale(cam, screenW, screenH);     break;
    default:                   break;
  }
}

}  // namespace Editor
}  // namespace Monolith
