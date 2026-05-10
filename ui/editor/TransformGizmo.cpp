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

    void TransformGizmo::Activate(GizmoMode mode, Vec3 targetPos,
                                  Quaternion targetRot, Vec3 targetScale) {
        m_mode = mode;
        m_pos = targetPos;
        m_rot = targetRot;
        m_scale = targetScale;
        m_hovered = GizmoAxis::None;
        m_dragging = GizmoAxis::None;
        m_prevMouseL = false;
    }

    void TransformGizmo::Deactivate() {
        m_mode = GizmoMode::None;
        m_dragging = GizmoAxis::None;
        m_hovered = GizmoAxis::None;
    }

    void TransformGizmo::SyncTarget(Vec3 pos, Quaternion rot, Vec3 scale) {
        // Always sync — EditorLayer calls this with the already-updated position
        // from the previous frame's delta, so there is no double-counting.
        m_pos = pos;
        m_rot = rot;
        m_scale = scale;
    }


    float TransformGizmo::HandleSize(const Camera &cam) const {
        (void)cam;
        return 1.0f;
    }

    Vec3 TransformGizmo::AxisDir(GizmoAxis axis) const {
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


    GizmoAxis TransformGizmo::PickAxis(float mx, float my, const Camera &cam,
                                       int screenW, int screenH) const {
        // In rotate mode the visual handle is a circular ring, not a line.
        // Testing distance to the axis line would let clicks far from the ring
        // (but along the axis direction) trigger rotation, so dispatch to a
        // dedicated ring-aware picker. Translate and scale share the
        // segment-distance test below because their visible shafts are lines.
        if (m_mode == GizmoMode::Rotate) {
            const float radius = HandleSize(cam);
            constexpr float kRotateHitRadiusSq = 7.0f * 7.0f;
            constexpr int kSegments = 48;
            constexpr float kRingTwoPi = 6.28318530718f;

            const std::array<Vec3, 3> kBasisU = {
                Vec3{0.0f, 1.0f, 0.0f}, Vec3{1.0f, 0.0f, 0.0f},
                Vec3{1.0f, 0.0f, 0.0f}
            };
            const std::array<Vec3, 3> kBasisV = {
                Vec3{0.0f, 0.0f, 1.0f}, Vec3{0.0f, 0.0f, 1.0f},
                Vec3{0.0f, 1.0f, 0.0f}
            };

            float bestRingDist = kRotateHitRadiusSq;
            GizmoAxis bestRingAxis = GizmoAxis::None;

            for (int i = 0; i < 3; ++i) {
                const auto ringAxis = static_cast<GizmoAxis>(i + 1);
                const Vec3 uVec = kBasisU[i];
                const Vec3 vVec = kBasisV[i];

                float prevX = 0.0f;
                float prevY = 0.0f;
                bool prevValid = false;

                for (int j = 0; j <= kSegments; ++j) {
                    const float t = kRingTwoPi * static_cast<float>(j) /
                                    static_cast<float>(kSegments);
                    const Vec3 p = m_pos + uVec * (std::cos(t) * radius) +
                                   vVec * (std::sin(t) * radius);
                    float sx;
                    float sy;
                    if (!WorldToScreen(p, cam, screenW, screenH, sx, sy)) {
                        prevValid = false;
                        continue;
                    }
                    if (prevValid) {
                        const float abx = sx - prevX;
                        const float aby = sy - prevY;
                        const float apx = mx - prevX;
                        const float apy = my - prevY;
                        const float abLen2 = abx * abx + aby * aby;
                        float segT = 0.0f;
                        if (abLen2 > 1e-8f)
                            segT = std::clamp(
                                (apx * abx + apy * aby) / abLen2, 0.0f, 1.0f);
                        const float cx = prevX + segT * abx;
                        const float cy = prevY + segT * aby;
                        const float dx = mx - cx;
                        const float dy = my - cy;
                        const float distSq = dx * dx + dy * dy;
                        if (distSq < bestRingDist) {
                            bestRingDist = distSq;
                            bestRingAxis = ringAxis;
                        }
                    }
                    prevX = sx;
                    prevY = sy;
                    prevValid = true;
                }
            }
            return bestRingAxis;
        }

        const float handleLen = HandleSize(cam);
        // Hit radius tuned to visual thickness (shaft ≈ 4-5 px, head ≈ 12 px on
        // screen). 7 px keeps clicks feeling "on the arrow" without requiring
        // pixel-perfect accuracy on the shaft.
        constexpr float kHitRadiusSq = 7.0f * 7.0f;
        constexpr float kAxisStartT =
                0.22f; // avoid accidental picks at the gizmo origin

        float bestDist = kHitRadiusSq;
        GizmoAxis bestAxis = GizmoAxis::None;

        for (int i = 0; i < 3; ++i) {
            auto axis = static_cast<GizmoAxis>(i + 1); // X=1, Y=2, Z=3
            Vec3 tip = m_pos + AxisDir(axis) * handleLen;

            float ox;
            float oy;
            float tx;
            float ty;
            if (!WorldToScreen(m_pos, cam, screenW, screenH, ox, oy))
                continue;
            if (!WorldToScreen(tip, cam, screenW, screenH, tx, ty))
                continue;

            // 2D point-to-segment distance squared
            float abx = tx - ox;
            float aby = ty - oy;
            float apx = mx - ox;
            float apy = my - oy;
            float abLen2 = abx * abx + aby * aby;
            float t = 0.0f;
            if (abLen2 > 1e-8f)
                t = std::clamp((apx * abx + apy * aby) / abLen2, 0.0f, 1.0f);

            // Do not let clicks near the gizmo origin select an arbitrary axis.
            // This keeps object-center clicks from instantly becoming Y-axis drags.
            if (t < kAxisStartT)
                continue;

            float cx = ox + t * abx;
            float cy = oy + t * aby;
            float dx = mx - cx;
            float dy = my - cy;
            float distSq = dx * dx + dy * dy;

            if (distSq < bestDist) {
                bestDist = distSq;
                bestAxis = axis;
            }
        }
        return bestAxis;
    }


    void TransformGizmo::BeginDrag(const Ray &ray, const Camera &cam) {
        m_dragging = m_hovered;
        m_dragAnchorPos = m_pos;

        if (m_mode == GizmoMode::Rotate) {
            Vec3 axisDir = AxisDir(m_dragging);
            if (Vec3 hitPt{}; RayHitPlane(ray, axisDir, m_pos, hitPt)) {
                Vec3 diff = hitPt - m_pos;
                m_dragStartDir =
                        (diff.LengthSq() > 1e-8f) ? diff.Normalized() : Vec3::Right();
            } else {
                m_dragStartDir = Vec3::Right();
            }
            m_dragPrevAngle = 0.0f;
            m_dragPlaneNormal = axisDir;
        } else {
            // Translate / Scale: use camera-facing plane through object
            Vec3 camFwd = cam.GetForward();
            if (Vec3 hitPt{}; RayHitPlane(ray, camFwd, m_pos, hitPt)) {
                Vec3 axisDir = AxisDir(m_dragging);
                m_dragPrevOffset = Vec3::Dot(hitPt - m_dragAnchorPos, axisDir);
            } else {
                m_dragPrevOffset = 0.0f;
            }
            m_dragPlaneNormal = camFwd;
        }
    }

    bool TransformGizmo::ApplyActiveDrag(const Ray &ray, const Camera &cam,
                                         Vec3 &outDeltaPos, Quaternion &outDeltaRot,
                                         Vec3 &outDeltaScale) {
        Vec3 hitPt;
        if (!RayHitPlane(ray, m_dragPlaneNormal, m_dragAnchorPos, hitPt))
            return true; // can't hit plane — consume without delta

        if (m_mode == GizmoMode::Translate) {
            Vec3 axisDir = AxisDir(m_dragging);
            float curOffset = Vec3::Dot(hitPt - m_dragAnchorPos, axisDir);
            float delta = curOffset - m_dragPrevOffset;
            m_dragPrevOffset = curOffset;
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
            float deltaAngle = curAngle - m_dragPrevAngle;
            m_dragPrevAngle = curAngle;
            outDeltaRot = Quaternion::FromAxisAngle(axisNormal, deltaAngle);
        } else if (m_mode == GizmoMode::Scale) {
            float handleLen = HandleSize(cam);
            Vec3 axisDir = AxisDir(m_dragging);
            float curOffset = Vec3::Dot(hitPt - m_dragAnchorPos, axisDir);
            float delta = curOffset - m_dragPrevOffset;
            m_dragPrevOffset = curOffset;
            float divisor = (handleLen > 1e-6f) ? handleLen : 1.0f;
            float factor = 1.0f + delta / divisor;
            if (m_dragging == GizmoAxis::X)
                outDeltaScale = {factor, 1.0f, 1.0f};
            else if (m_dragging == GizmoAxis::Y)
                outDeltaScale = {1.0f, factor, 1.0f};
            else
                outDeltaScale = {1.0f, 1.0f, factor};
        }
        return false;
    }

    bool TransformGizmo::Update(GLFWwindow *window, const Camera &cam, int screenW,
                                int screenH, Vec3 &outDeltaPos,
                                Quaternion &outDeltaRot, Vec3 &outDeltaScale) {
        outDeltaPos = Vec3::Zero();
        outDeltaRot = Quaternion::Identity();
        outDeltaScale = Vec3::One();

        if (m_mode == GizmoMode::None)
            return false;

        double dmx;
        double dmy;
        glfwGetCursorPos(window, &dmx, &dmy);
        auto mx = static_cast<float>(dmx);
        auto my = static_cast<float>(dmy);

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
                if (ApplyActiveDrag(ray, cam, outDeltaPos, outDeltaRot, outDeltaScale))
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

    void TransformGizmo::DrawRotate(const Camera &cam, int /*screenW*/,
                                    int /*screenH*/) const {
        const float radius = HandleSize(cam);
        const float halfThickness = radius * 0.018f;
        constexpr int kSegments = 48;

        // Axis-plane basis vectors: each ring lies on the plane perpendicular
        // to its rotation axis. Axis normals double as the "ribbon thickness"
        // direction so the ring renders as a flat 3D band rather than a hair.
        const std::array<Vec3, 3> kBasisU = {
            Vec3{0.0f, 1.0f, 0.0f}, Vec3{1.0f, 0.0f, 0.0f}, Vec3{1.0f, 0.0f, 0.0f}
        };
        const std::array<Vec3, 3> kBasisV = {
            Vec3{0.0f, 0.0f, 1.0f}, Vec3{0.0f, 0.0f, 1.0f}, Vec3{0.0f, 1.0f, 0.0f}
        };

        for (int i = 0; i < 3; ++i) {
            auto axis = static_cast<GizmoAxis>(i + 1);
            const Vec4 color = (axis == m_hovered || axis == m_dragging)
                                   ? kHoverColor
                                   : kAxisColors[i];
            const Vec3 uVec = kBasisU[i];
            const Vec3 vVec = kBasisV[i];
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
