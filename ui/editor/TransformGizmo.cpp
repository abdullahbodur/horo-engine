/**
 * @file TransformGizmo.cpp
 * @brief Implementation for TransformGizmo editor functionality.
 */
#include "ui/editor/TransformGizmo.h"

#include <GLFW/glfw3.h>
#include <algorithm>
#include <array>
#include <cmath>

#include "math/Mat4.h"
#include "math/MathUtils.h"
#include "math/Vec4.h"
#include "renderer/DebugDraw.h"

namespace Horo::Editor {
    namespace {
        /** @brief Returns the unrotated local direction for a gizmo axis. */
        Vec3 LocalAxisDir(GizmoAxis axis) {
            using enum GizmoAxis;
            switch (axis) {
                case X:
                    return {1.0f, 0.0f, 0.0f};
                case Y:
                    return {0.0f, 1.0f, 0.0f};
                case Z:
                    return {0.0f, 0.0f, 1.0f};
                default:
                    return Vec3::Zero();
            }
        }

        /** @brief Stores a rotation ring's oriented plane basis. */
        struct RingBasis {
            Vec3 u;
            Vec3 v;
        };

        /** @brief Builds the oriented plane basis for the requested rotation ring. */
        RingBasis MakeRingBasis(const Vec3 &xAxis, const Vec3 &yAxis,
                                const Vec3 &zAxis, GizmoAxis axis) {
            using enum GizmoAxis;
            switch (axis) {
                case X:
                    return {yAxis, zAxis};
                case Y:
                    return {zAxis, xAxis};
                case Z:
                    return {xAxis, yAxis};
                default:
                    return {Vec3::Zero(), Vec3::Zero()};
            }
        }

        /** @brief Returns 2-D point-to-segment distance squared. */
        float ScreenSegmentDistanceSq(float px, float py, float ax, float ay,
                                      float bx, float by, float minT) {
            const float abx = bx - ax;
            const float aby = by - ay;
            const float apx = px - ax;
            const float apy = py - ay;
            const float abLen2 = abx * abx + aby * aby;
            float t = 0.0f;
            if (abLen2 > 1e-8f)
                t = std::clamp((apx * abx + apy * aby) / abLen2, minT, 1.0f);

            const float cx = ax + t * abx;
            const float cy = ay + t * aby;
            const float dx = px - cx;
            const float dy = py - cy;
            return dx * dx + dy * dy;
        }

        /** @brief Rounds @p value to the nearest positive snap step. */
        float SnapValueToStep(float value, float step) {
            if (step <= 0.0f)
                return value;
            return std::round(value / step) * step;
        }

        /** @brief Snaps scale while avoiding accidental zero scale collapse. */
        float SnapScaleToStep(float value, float step) {
            const float snapped = SnapValueToStep(value, step);
            if (std::abs(snapped) > 1e-6f || std::abs(value) <= 1e-6f)
                return snapped;
            return value > 0.0f ? step : -step;
        }

        /** @brief Returns the scalar component for @p axis. */
        float AxisValue(const Vec3 &value, GizmoAxis axis) {
            if (axis == GizmoAxis::X)
                return value.x;
            if (axis == GizmoAxis::Y)
                return value.y;
            return value.z;
        }
    } // namespace

    /** @copydoc TransformGizmo::Activate */
    void TransformGizmo::Activate(GizmoMode mode, Vec3 targetPos,
                                  Quaternion targetRot, Vec3 targetScale) {
        m_mode = mode;
        m_pos = targetPos;
        m_rot = targetRot;
        m_scale = targetScale;
        m_hovered = GizmoAxis::None;
        m_dragging = GizmoAxis::None;
        m_prevMouseL = false;
        m_dragStartOffset = 0.0f;
        m_dragPrevOffset = 0.0f;
        m_dragPrevSnappedOffset = 0.0f;
        m_dragPrevAngle = 0.0f;
        m_dragPrevSnappedAngle = 0.0f;
    }

    /** @copydoc TransformGizmo::Deactivate */
    void TransformGizmo::Deactivate() {
        m_mode = GizmoMode::None;
        m_dragging = GizmoAxis::None;
        m_hovered = GizmoAxis::None;
        m_dragStartOffset = 0.0f;
        m_dragPrevOffset = 0.0f;
        m_dragPrevSnappedOffset = 0.0f;
        m_dragPrevAngle = 0.0f;
        m_dragPrevSnappedAngle = 0.0f;
    }

    /** @copydoc TransformGizmo::SyncTarget */
    void TransformGizmo::SyncTarget(Vec3 pos, Quaternion rot, Vec3 scale) {
        // Always sync — EditorLayer calls this with the already-updated position
        // from the previous frame's delta, so there is no double-counting.
        m_pos = pos;
        m_rot = rot;
        m_scale = scale;
    }


    /** @copydoc TransformGizmo::HandleSize */
    float TransformGizmo::HandleSize(const Camera &cam) const {
        (void)cam;
        return 1.0f;
    }

    /** @copydoc TransformGizmo::AxisDir */
    Vec3 TransformGizmo::AxisDir(GizmoAxis axis) const {
        const Vec3 localDir = LocalAxisDir(axis);
        if (localDir.LengthSq() <= 1e-10f)
            return Vec3::Zero();

        const Vec3 worldDir = m_rot.Normalized() * localDir;
        return (worldDir.LengthSq() > 1e-10f) ? worldDir.Normalized()
                                              : localDir;
    }

    /** @copydoc TransformGizmo::WorldToScreen */
    bool TransformGizmo::WorldToScreen(const Vec3 &p, const Camera &cam, int w,
                                       int h, float &sx, float &sy) {
        Mat4 vp = cam.GetViewProjection();
        Vec4 clip = vp * Vec4(p.x, p.y, p.z, 1.0f);
        if (clip.w < 1e-6f)
            return false;
        float ndcX = clip.x / clip.w;
        float ndcY = clip.y / clip.w;
        // Screen: (0,0) = top-left; NDC Y is flipped relative to screen Y.
        sx = (ndcX * 0.5f + 0.5f) * static_cast<float>(w);
        sy = (1.0f - (ndcY * 0.5f + 0.5f)) * static_cast<float>(h);
        return true;
    }

    /** @copydoc TransformGizmo::RayHitPlane */
    bool TransformGizmo::RayHitPlane(const Ray &ray, const Vec3 &normal,
                                     const Vec3 &point, Vec3 &outHit) {
        float denom = Vec3::Dot(normal, ray.direction);
        if (std::abs(denom) < 1e-6f)
            return false;
        float t = Vec3::Dot(normal, point - ray.origin) / denom;
        if (t < 0.0f)
            return false;
        outHit = ray.origin + ray.direction * t;
        return true;
    }

    /** @copydoc TransformGizmo::RayClosestOnLine */
    Vec3 TransformGizmo::RayClosestOnLine(const Ray &ray, const Vec3 &lineOrigin,
                                          const Vec3 &lineDir) {
        // Minimize squared distance between ray point and line point.
        Vec3 w0 = ray.origin - lineOrigin;
        float a = Vec3::Dot(lineDir, lineDir);
        float b = Vec3::Dot(lineDir, ray.direction);
        float c = Vec3::Dot(ray.direction, ray.direction);
        float d = Vec3::Dot(lineDir, w0);
        float e = Vec3::Dot(ray.direction, w0);
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


namespace {
/** @brief Picks the closest rotation ring axis to the given screen point. */
GizmoAxis PickRotateRingAxis(float mx, float my, const Vec3& pos,
                             const Vec3& xAxis, const Vec3& yAxis,
                             const Vec3& zAxis, float radius,
                             const Camera& cam, int screenW, int screenH) {
    constexpr float kRotateHitRadiusSq = 9.0f * 9.0f;
    constexpr int kSegments = 48;
    constexpr float kRingTwoPi = 6.28318530718f;

    float bestRingDist = kRotateHitRadiusSq;
    GizmoAxis bestRingAxis = GizmoAxis::None;

    for (int i = 0; i < 3; ++i) {
        const auto ringAxis = static_cast<GizmoAxis>(i + 1);
        const RingBasis basis = MakeRingBasis(xAxis, yAxis, zAxis, ringAxis);
        const Vec3 uVec = basis.u;
        const Vec3 vVec = basis.v;

        float prevX = 0.0f;
        float prevY = 0.0f;
        bool prevValid = false;

        for (int j = 0; j <= kSegments; ++j) {
            const float t = kRingTwoPi * static_cast<float>(j) /
                            static_cast<float>(kSegments);
            const Vec3 p = pos + uVec * (std::cos(t) * radius) +
                           vVec * (std::sin(t) * radius);
            float sx;
            float sy;
            if (!TransformGizmo::WorldToScreen(p, cam, screenW, screenH, sx, sy)) {
                prevValid = false;
                continue;
            }
            if (!prevValid) {
                prevX = sx;
                prevY = sy;
                prevValid = true;
                continue;
            }
            if (const float distSq = ScreenSegmentDistanceSq(mx, my, prevX, prevY,
                                                             sx, sy, 0.0f);
                distSq < bestRingDist) {
                bestRingDist = distSq;
                bestRingAxis = ringAxis;
            }
            prevX = sx;
            prevY = sy;
        }
    }
    return bestRingAxis;
}
}  // namespace

    /** @copydoc TransformGizmo::PickAxis */
    GizmoAxis TransformGizmo::PickAxis(float mx, float my, const Camera &cam,
                                       int screenW, int screenH) const {
        const Vec3 xAxis = AxisDir(GizmoAxis::X);
        const Vec3 yAxis = AxisDir(GizmoAxis::Y);
        const Vec3 zAxis = AxisDir(GizmoAxis::Z);
        if (m_mode == GizmoMode::Rotate)
            return PickRotateRingAxis(mx, my, m_pos, xAxis, yAxis, zAxis,
                                      HandleSize(cam), cam, screenW, screenH);

        const float handleLen = HandleSize(cam);
        constexpr float kShaftHitRadiusSq = 7.0f * 7.0f;
        constexpr float kHeadHitRadiusSq = 10.0f * 10.0f;
        constexpr float kAxisStartT =
                0.22f; // avoid accidental picks at the gizmo origin

        float bestDist = kHeadHitRadiusSq;
        GizmoAxis bestAxis = GizmoAxis::None;

        for (int i = 0; i < 3; ++i) {
            auto axis = static_cast<GizmoAxis>(i + 1); // X=1, Y=2, Z=3
            const Vec3 axisDir = AxisDir(axis);
            Vec3 tip = m_pos + axisDir * handleLen;

            float ox;
            float oy;
            float tx;
            float ty;
            if (!WorldToScreen(m_pos, cam, screenW, screenH, ox, oy))
                continue;
            if (!WorldToScreen(tip, cam, screenW, screenH, tx, ty))
                continue;

            const float handleScreenDx = tx - ox;
            const float handleScreenDy = ty - oy;
            if (handleScreenDx * handleScreenDx + handleScreenDy * handleScreenDy <
                16.0f)
                continue;

            const float distSq = ScreenSegmentDistanceSq(
                    mx, my, ox, oy, tx, ty, kAxisStartT);
            const bool insideShaft =
                    distSq <= kShaftHitRadiusSq && distSq < bestDist;

            const float tipDx = mx - tx;
            const float tipDy = my - ty;
            const float tipDistSq = tipDx * tipDx + tipDy * tipDy;
            const bool insideHead =
                    tipDistSq <= kHeadHitRadiusSq && tipDistSq < bestDist;

            if (insideShaft || insideHead) {
                bestDist = insideHead ? tipDistSq : distSq;
                bestAxis = axis;
            }
        }
        return bestAxis;
    }


    /** @copydoc TransformGizmo::BeginDrag */
    void TransformGizmo::BeginDrag(const Ray &ray, const Camera &cam) {
        m_dragging = m_hovered;
        m_dragAnchorPos = m_pos;
        m_dragAnchorScale = m_scale;
        m_dragStartOffset = 0.0f;
        m_dragPrevSnappedOffset = 0.0f;
        m_dragPrevSnappedAngle = 0.0f;

        if (m_mode == GizmoMode::Rotate) {
            const Vec3 axisDir = AxisDir(m_dragging);
            if (Vec3 hitPt{}; RayHitPlane(ray, axisDir, m_pos, hitPt)) {
                Vec3 diff = hitPt - m_pos;
                m_dragStartDir =
                        (diff.LengthSq() > 1e-8f) ? diff.Normalized()
                                                  : AxisDir(GizmoAxis::X);
            } else {
                m_dragStartDir = AxisDir(GizmoAxis::X);
            }
            m_dragPrevAngle = 0.0f;
            m_dragPlaneNormal = axisDir;
        } else {
            // Translate / Scale: use camera-facing plane through object
            Vec3 camFwd = cam.GetForward();
            if (Vec3 hitPt{}; RayHitPlane(ray, camFwd, m_pos, hitPt)) {
                Vec3 axisDir = AxisDir(m_dragging);
                m_dragStartOffset =
                        Vec3::Dot(hitPt - m_dragAnchorPos, axisDir);
                m_dragPrevOffset = m_dragStartOffset;
            } else {
                m_dragStartOffset = 0.0f;
                m_dragPrevOffset = 0.0f;
            }
            m_dragPlaneNormal = camFwd;
        }
    }

    /** @copydoc TransformGizmo::ApplyActiveDrag */
    bool TransformGizmo::ApplyActiveDrag(const Ray &ray, const Camera &cam,
                                         Vec3 &outDeltaPos, Quaternion &outDeltaRot,
                                         Vec3 &outDeltaScale,
                                         float translateSnapStep,
                                         float rotateSnapRadians,
                                         float scaleSnapStep) {
        Vec3 hitPt;
        if (!RayHitPlane(ray, m_dragPlaneNormal, m_dragAnchorPos, hitPt))
            return true; // can't hit plane — consume without delta

        if (m_mode == GizmoMode::Translate) {
            Vec3 axisDir = AxisDir(m_dragging);
            float curOffset = Vec3::Dot(hitPt - m_dragAnchorPos, axisDir);
            float delta = 0.0f;
            if (translateSnapStep > 0.0f) {
                const float snappedOffset = SnapValueToStep(
                        curOffset - m_dragStartOffset, translateSnapStep);
                delta = snappedOffset - m_dragPrevSnappedOffset;
                m_dragPrevSnappedOffset = snappedOffset;
            } else {
                delta = curOffset - m_dragPrevOffset;
                m_dragPrevOffset = curOffset;
            }
            outDeltaPos = axisDir * delta;
            m_pos = m_pos + outDeltaPos; // keep gizmo visual in sync
        } else if (m_mode == GizmoMode::Rotate) {
            Vec3 axisNormal = AxisDir(m_dragging);
            Vec3 diff = hitPt - m_dragAnchorPos;
            Vec3 curDir =
                    (diff.LengthSq() > 1e-8f) ? diff.Normalized() : m_dragStartDir;
            float cosA = Vec3::Dot(m_dragStartDir, curDir);
            float sinA = Vec3::Dot(Vec3::Cross(m_dragStartDir, curDir), axisNormal);
            float curAngle = std::atan2(sinA, cosA);
            float deltaAngle = 0.0f;
            if (rotateSnapRadians > 0.0f) {
                const float snappedAngle =
                        SnapValueToStep(curAngle, rotateSnapRadians);
                deltaAngle = snappedAngle - m_dragPrevSnappedAngle;
                m_dragPrevSnappedAngle = snappedAngle;
            } else {
                deltaAngle = curAngle - m_dragPrevAngle;
                m_dragPrevAngle = curAngle;
            }
            outDeltaRot = Quaternion::FromAxisAngle(axisNormal, deltaAngle);
        } else if (m_mode == GizmoMode::Scale) {
            float handleLen = HandleSize(cam);
            Vec3 axisDir = AxisDir(m_dragging);
            float curOffset = Vec3::Dot(hitPt - m_dragAnchorPos, axisDir);
            float divisor = (handleLen > 1e-6f) ? handleLen : 1.0f;
            float factor = 1.0f;
            if (scaleSnapStep > 0.0f) {
                const float totalFactor =
                        1.0f + (curOffset - m_dragStartOffset) / divisor;
                const float snappedScale = SnapScaleToStep(
                        AxisValue(m_dragAnchorScale, m_dragging) * totalFactor,
                        scaleSnapStep);
                const float currentScale = AxisValue(m_scale, m_dragging);
                if (std::abs(currentScale) > 1e-6f)
                    factor = snappedScale / currentScale;
            } else {
                const float delta = curOffset - m_dragPrevOffset;
                m_dragPrevOffset = curOffset;
                factor = 1.0f + delta / divisor;
            }
            if (m_dragging == GizmoAxis::X)
                outDeltaScale = {factor, 1.0f, 1.0f};
            else if (m_dragging == GizmoAxis::Y)
                outDeltaScale = {1.0f, factor, 1.0f};
            else
                outDeltaScale = {1.0f, 1.0f, factor};
            m_scale.x *= outDeltaScale.x;
            m_scale.y *= outDeltaScale.y;
            m_scale.z *= outDeltaScale.z;
        }
        return false;
    }

    /** @copydoc TransformGizmo::Update */
    bool TransformGizmo::Update(GLFWwindow *window, const Camera &cam, int screenW,
                                int screenH, Vec3 &outDeltaPos,
                                Quaternion &outDeltaRot, Vec3 &outDeltaScale,
                                float viewportX, float viewportY, float viewportW,
                                float viewportH, float translateSnapStep,
                                float rotateSnapRadians, float scaleSnapStep) {
        outDeltaPos = Vec3::Zero();
        outDeltaRot = Quaternion::Identity();
        outDeltaScale = Vec3::One();

        if (m_mode == GizmoMode::None)
            return false;

        double dmx;
        double dmy;
        glfwGetCursorPos(window, &dmx, &dmy);
        int windowW = 0;
        int windowH = 0;
        glfwGetWindowSize(window, &windowW, &windowH);
        const float sourceX = (viewportW > 0.0f && viewportH > 0.0f)
                                  ? viewportX
                                  : 0.0f;
        const float sourceY = (viewportW > 0.0f && viewportH > 0.0f)
                                  ? viewportY
                                  : 0.0f;
        const int sourceW = (viewportW > 0.0f)
                                ? static_cast<int>(std::round(viewportW))
                                : windowW;
        const int sourceH = (viewportH > 0.0f)
                                ? static_cast<int>(std::round(viewportH))
                                : windowH;
        const Vec2 mouse = ScaleScreenPointToRenderTarget(
            static_cast<float>(dmx) - sourceX, static_cast<float>(dmy) - sourceY,
            sourceW, sourceH, screenW, screenH);
        const float mx = mouse.x;
        const float my = mouse.y;

        bool currMouseL =
                glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
        bool clicked = currMouseL && !m_prevMouseL;
        bool released = !currMouseL && m_prevMouseL;
        m_prevMouseL = currMouseL;

        Ray ray = ScreenToRay(mx, my, screenW, screenH, cam);

        if (m_dragging == GizmoAxis::None) {
            m_hovered = PickAxis(mx, my, cam, screenW, screenH);
            if (clicked && m_hovered != GizmoAxis::None)
                BeginDrag(ray, cam);
        } else {
            if (released) {
                m_dragging = GizmoAxis::None;
            } else {
                if (ApplyActiveDrag(ray, cam, outDeltaPos, outDeltaRot,
                                    outDeltaScale, translateSnapStep,
                                    rotateSnapRadians, scaleSnapStep))
                    return true; // consumed, no delta
            }
        }

        return m_dragging != GizmoAxis::None || m_hovered != GizmoAxis::None;
    }


    namespace {
        const std::array<Vec4, 3> kAxisColors = {
            {
                {0.9f, 0.2f, 0.2f, 1.0f}, // X = red
                {0.2f, 0.9f, 0.2f, 1.0f}, // Y = green
                {0.2f, 0.4f, 1.0f, 1.0f}, // Z = blue
            }
        };
        const Vec4 kHoverColor = {1.0f, 1.0f, 1.0f, 1.0f};
        constexpr float kTwoPi = 6.28318530718f;
    } // namespace

    /** @copydoc TransformGizmo::DrawTranslate */
    void TransformGizmo::DrawTranslate(const Camera &cam, int /*screenW*/,
                                       int /*screenH*/) const {
        const float handleLen = HandleSize(cam);

        // Visual thickness: axis shaft is a thin rectangular prism built from
        // two perpendicular flat quads ("+" cross section). Width is a fraction
        // of handle length so the gizmo scales naturally with the camera.
        const float shaftHalfWidth = handleLen * 0.018f;
        const float headLen = handleLen * 0.22f;
        const float headRadius = handleLen * 0.055f;
        const float shaftLen = handleLen - headLen;

        for (int i = 0; i < 3; ++i) {
            auto axis = static_cast<GizmoAxis>(i + 1);
            const Vec3 dir = AxisDir(axis);
            const Vec3 tip = m_pos + dir * handleLen;
            const Vec3 shaftEnd = m_pos + dir * shaftLen;
            const Vec4 color = (axis == m_hovered || axis == m_dragging)
                                   ? kHoverColor
                                   : kAxisColors[i];

            // Build two perpendicular basis vectors for the cross-section.
            Vec3 perp1 = Vec3::Cross(dir, {0.0f, 1.0f, 0.0f});
            if (perp1.LengthSq() < 1e-4f)
                perp1 = Vec3::Cross(dir, {1.0f, 0.0f, 0.0f});
            perp1 = perp1.Normalized();
            const Vec3 perp2 = Vec3::Cross(dir, perp1).Normalized();

            // Thick shaft: two crossed flat quads ("+") from origin to shaftEnd.
            // Each quad is 2 triangles. The two-quad cross looks uniformly thick
            // from any viewing angle without needing a full cylinder.
            const auto drawShaftQuad = [&](const Vec3 &side) {
                const Vec3 a = m_pos + side * shaftHalfWidth;
                const Vec3 b = m_pos - side * shaftHalfWidth;
                const Vec3 c = shaftEnd - side * shaftHalfWidth;
                const Vec3 d = shaftEnd + side * shaftHalfWidth;
                DebugDraw::Triangle(a, b, c, color);
                DebugDraw::Triangle(a, c, d, color);
            };
            drawShaftQuad(perp1);
            drawShaftQuad(perp2);

            // Solid pyramid arrowhead: 4 triangular faces from tip down to a
            // square base at shaftEnd, plus two base triangles so the bottom
            // is closed when seen from behind.
            const Vec3 baseCenter = shaftEnd;
            const Vec3 baseP1 = baseCenter + perp1 * headRadius;
            const Vec3 baseP2 = baseCenter + perp2 * headRadius;
            const Vec3 baseM1 = baseCenter - perp1 * headRadius;
            const Vec3 baseM2 = baseCenter - perp2 * headRadius;

            DebugDraw::Triangle(tip, baseP1, baseP2, color);
            DebugDraw::Triangle(tip, baseP2, baseM1, color);
            DebugDraw::Triangle(tip, baseM1, baseM2, color);
            DebugDraw::Triangle(tip, baseM2, baseP1, color);

            // Base cap (slightly darker tint would need a shader path; keep
            // same color — the cap just prevents a see-through look from behind).
            DebugDraw::Triangle(baseP1, baseM1, baseP2, color);
            DebugDraw::Triangle(baseP2, baseM1, baseM2, color);
        }
    }

    /** @copydoc TransformGizmo::DrawRotate */
    void TransformGizmo::DrawRotate(const Camera &cam, int /*screenW*/,
                                    int /*screenH*/) const {
        const float radius = HandleSize(cam);
        const float halfThickness = radius * 0.018f;
        constexpr int kSegments = 48;

        const Vec3 xAxis = AxisDir(GizmoAxis::X);
        const Vec3 yAxis = AxisDir(GizmoAxis::Y);
        const Vec3 zAxis = AxisDir(GizmoAxis::Z);

        for (int i = 0; i < 3; ++i) {
            auto axis = static_cast<GizmoAxis>(i + 1);
            const Vec4 color = (axis == m_hovered || axis == m_dragging)
                                   ? kHoverColor
                                   : kAxisColors[i];
            const RingBasis basis = MakeRingBasis(xAxis, yAxis, zAxis, axis);
            const Vec3 uVec = basis.u;
            const Vec3 vVec = basis.v;
            const Vec3 axisN = Vec3::Cross(uVec, vVec).Normalized();

            Vec3 prev;
            for (int j = 0; j <= kSegments; ++j) {
                const float t = kTwoPi * static_cast<float>(j) /
                                static_cast<float>(kSegments);
                const Vec3 p = m_pos + uVec * (std::cos(t) * radius) +
                               vVec * (std::sin(t) * radius);
                if (j > 0) {
                    // Ribbon quad perpendicular to the ring plane (thickness
                    // along axisN) + a radial ribbon so the ring stays visible
                    // when viewed edge-on to the first ribbon's plane.
                    const Vec3 off_n = axisN * halfThickness;

                    // midpoint-to-center for the radial direction
                    Vec3 radialMid = ((prev - m_pos) + (p - m_pos));
                    radialMid = (radialMid.LengthSq() > 1e-8f)
                                    ? radialMid.Normalized()
                                    : uVec;
                    const Vec3 off_r = radialMid * halfThickness;

                    // Axis-normal ribbon (sticks out of ring plane)
                    DebugDraw::Triangle(prev + off_n, prev - off_n, p - off_n,
                                        color);
                    DebugDraw::Triangle(prev + off_n, p - off_n, p + off_n,
                                        color);

                    // Radial ribbon (widens ring in its own plane)
                    DebugDraw::Triangle(prev + off_r, prev - off_r, p - off_r,
                                        color);
                    DebugDraw::Triangle(prev + off_r, p - off_r, p + off_r,
                                        color);
                }
                prev = p;
            }
        }
    }

    /** @copydoc TransformGizmo::DrawScale */
    void TransformGizmo::DrawScale(const Camera &cam, int /*screenW*/,
                                   int /*screenH*/) const {
        const float handleLen = HandleSize(cam);
        const float shaftHalfWidth = handleLen * 0.018f;
        const float boxHalf = handleLen * 0.08f;
        // Shaft ends just before the cube so the cube sits on the shaft tip
        // rather than encapsulating its final segment.
        const float shaftLen = handleLen - boxHalf;

        for (int i = 0; i < 3; ++i) {
            auto axis = static_cast<GizmoAxis>(i + 1);
            const Vec3 dir = AxisDir(axis);
            const Vec3 tip = m_pos + dir * handleLen;
            const Vec3 shaftEnd = m_pos + dir * shaftLen;
            const Vec4 color = (axis == m_hovered || axis == m_dragging)
                                   ? kHoverColor
                                   : kAxisColors[i];

            Vec3 perp1 = Vec3::Cross(dir, {0.0f, 1.0f, 0.0f});
            if (perp1.LengthSq() < 1e-4f)
                perp1 = Vec3::Cross(dir, {1.0f, 0.0f, 0.0f});
            perp1 = perp1.Normalized();
            const Vec3 perp2 = Vec3::Cross(dir, perp1).Normalized();

            // Thick cross-quad shaft (matches translate visuals).
            const auto drawShaftQuad = [&](const Vec3 &side) {
                const Vec3 a = m_pos + side * shaftHalfWidth;
                const Vec3 b = m_pos - side * shaftHalfWidth;
                const Vec3 c = shaftEnd - side * shaftHalfWidth;
                const Vec3 d = shaftEnd + side * shaftHalfWidth;
                DebugDraw::Triangle(a, b, c, color);
                DebugDraw::Triangle(a, c, d, color);
            };
            drawShaftQuad(perp1);
            drawShaftQuad(perp2);

            // Solid cube handle at the axis tip.
            DebugDraw::SolidBox(tip, {boxHalf, boxHalf, boxHalf}, color);
        }

        // Centre uniform-scale handle (solid white cube).
        DebugDraw::SolidBox(m_pos, {boxHalf, boxHalf, boxHalf},
                            {1.0f, 1.0f, 1.0f, 1.0f});
    }

    /** @copydoc TransformGizmo::Draw */
    void TransformGizmo::Draw(const Camera &cam, int screenW, int screenH) const {
        using enum GizmoMode;
        if (m_mode == None)
            return;
        switch (m_mode) {
            case Translate:
                DrawTranslate(cam, screenW, screenH);
                break;
            case Rotate:
                DrawRotate(cam, screenW, screenH);
                break;
            case Scale:
                DrawScale(cam, screenW, screenH);
                break;
            default:
                break;
        }
    }
} // namespace Horo::Editor
