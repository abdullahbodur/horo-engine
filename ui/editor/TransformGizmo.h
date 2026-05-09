#pragma once
#include "ui/editor/Raycaster.h"
#include "math/Quaternion.h"
#include "math/Vec3.h"
#include "renderer/Camera.h"

struct GLFWwindow;
struct TransformGizmoTestAccessor; // forward declaration for test seam

namespace Horo::Editor {
    enum class GizmoMode { None, Translate, Rotate, Scale };

    enum class GizmoAxis { None, X, Y, Z };

    class TransformGizmo {
    public:
        void Activate(GizmoMode mode, Vec3 targetPos, Quaternion targetRot,
                      Vec3 targetScale);

        void Deactivate();

        // Update gizmo's internal copy each frame before calling Update().
        void SyncTarget(Vec3 pos, Quaternion rot, Vec3 scale);

        bool IsActive() const { return m_mode != GizmoMode::None; }
        GizmoMode GetMode() const { return m_mode; }
        GizmoAxis GetDragAxis() const { return m_dragging; }

        // Returns true when gizmo consumed the mouse (caller should suppress scene
        // picking). outDeltaPos/Rot/Scale: incremental delta for this frame
        // (identity/zero if nothing dragged).
        bool Update(GLFWwindow *window, const Camera &cam, int screenW, int screenH,
                    Vec3 &outDeltaPos, Quaternion &outDeltaRot, Vec3 &outDeltaScale);

        // Queue DebugDraw lines; call before DebugDraw::Flush.
        void Draw(const Camera &cam, int screenW, int screenH) const;

        // ---- Math helpers (public for testability)
        // ----------------------------------

        // World-space handle length. Uses a fixed 1.0 unit length so gizmo
        // naturally scales on screen when zooming in/out.
        float HandleSize(const Camera &cam) const;

        // World-space direction for each axis (world-aligned, not local).
        Vec3 AxisDir(GizmoAxis axis) const;

        // Screen-space axis hit test. Returns nearest axis whose 2D segment is within
        // 12 px, or GizmoAxis::None.
        GizmoAxis PickAxis(float mx, float my, const Camera &cam, int screenW,
                           int screenH) const;

        // Project a world point to screen pixels. Returns false if behind camera.
        // (0,0) = top-left, (w,h) = bottom-right.
        static bool WorldToScreen(const Vec3 &p, const Camera &cam, int w, int h,
                                  float &sx, float &sy);

        // Ray-plane intersection. Returns false if ray is parallel or hit is behind
        // origin.
        static bool RayHitPlane(const Ray &ray, const Vec3 &normal, const Vec3 &point,
                                Vec3 &outHit);

        // Closest point on an infinite line to a ray's nearest approach.
        static Vec3 RayClosestOnLine(const Ray &ray, const Vec3 &lineOrigin,
                                     const Vec3 &lineDir);

    private:
        GizmoMode m_mode = GizmoMode::None;
        GizmoAxis m_hovered = GizmoAxis::None;
        GizmoAxis m_dragging = GizmoAxis::None;
        Vec3 m_pos;
        Quaternion m_rot;
        Vec3 m_scale;

        // Drag bookkeeping
        bool m_prevMouseL = false;
        Vec3 m_dragAnchorPos; // gizmo position at drag start (plane reference)
        Vec3 m_dragPlaneNormal; // plane used for ray-hit each frame
        float m_dragPrevOffset = 0.0f; // previous axis projection (translate / scale)
        float m_dragPrevAngle = 0.0f; // previous cumulative angle (rotate)
        Vec3 m_dragStartDir; // initial tangent direction (rotate)

        void DrawTranslate(const Camera &cam, int screenW, int screenH) const;

        void DrawRotate(const Camera &cam, int screenW, int screenH) const;

        void DrawScale(const Camera &cam, int screenW, int screenH) const;

        // Drag helpers (reduce Update complexity)
        void BeginDrag(const Ray &ray, const Camera &cam);

        bool ApplyActiveDrag(const Ray &ray, const Camera &cam, Vec3 &outDeltaPos,
                             Quaternion &outDeltaRot, Vec3 &outDeltaScale);

        // Test seam: grants test code access to private drag state and methods.
        friend struct ::TransformGizmoTestAccessor;
    };
} // namespace Horo::Editor
