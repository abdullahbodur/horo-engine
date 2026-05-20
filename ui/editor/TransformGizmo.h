/** @file TransformGizmo.h
 *  @brief Interactive 3-D transform gizmo for translating, rotating, and scaling scene objects.
 */
#pragma once
#include "ui/editor/Raycaster.h"
#include "math/Quaternion.h"
#include "math/Vec3.h"
#include "renderer/Camera.h"

struct GLFWwindow;
struct TransformGizmoTestAccessor; // forward declaration for test seam

namespace Horo::Editor {
    /** @brief Operation mode of the transform gizmo. */
    enum class GizmoMode {
        None,      /**< Gizmo inactive; does not render or consume input. */
        Translate, /**< Drag handles adjust world-space position. */
        Rotate,    /**< Drag arcs adjust world-space rotation. */
        Scale,     /**< Drag handles adjust world-space scale. */
    };

    /** @brief World-space axis currently highlighted or being dragged. */
    enum class GizmoAxis {
        None, /**< No axis selected or drag inactive. */
        X,    /**< Local/world X axis handle. */
        Y,    /**< Local/world Y axis handle. */
        Z,    /**< Local/world Z axis handle. */
    };

    struct TransformGizmoResult {
        bool consumedMouse = false;
        Vec3 deltaPos = Vec3::Zero();
        Quaternion deltaRot = Quaternion::Identity();
        Vec3 deltaScale = Vec3::One();
    };

    struct TransformGizmoUpdateParams {
        GLFWwindow* window = nullptr;
        const Camera* cam = nullptr;
        int screenW = 0;
        int screenH = 0;
        float viewportX = 0.0f;
        float viewportY = 0.0f;
        float viewportW = 0.0f;
        float viewportH = 0.0f;
        float translateSnapStep = 0.0f;
        float rotateSnapRadians = 0.0f;
        float scaleSnapStep = 0.0f;
    };

    /** @brief Renders and drives an interactive per-object transform gizmo in the viewport. */
    class TransformGizmo {
    public:
        /** @brief Activates the gizmo in the given mode, snapping it to the target transform.
         *  @param mode        Operation mode to enter.
         *  @param targetPos   Initial world-space position of the target object.
         *  @param targetRot   Initial world-space rotation of the target object.
         *  @param targetScale Initial world-space scale of the target object.
         */
        void Activate(GizmoMode mode, Vec3 targetPos, Quaternion targetRot,
                      Vec3 targetScale);

        /** @brief Deactivates the gizmo and clears all drag state. */
        void Deactivate();

        // Update gizmo's internal copy each frame before calling Update().
        /** @brief Synchronises the gizmo's internal transform copy with the target object.
         *  @param pos   Current world-space position of the target.
         *  @param rot   Current world-space rotation of the target.
         *  @param scale Current world-space scale of the target.
         */
        void SyncTarget(Vec3 pos, Quaternion rot, Vec3 scale);

        /** @brief Returns true when the gizmo is in an active operation mode.
         *  @return True if mode is not GizmoMode::None.
         */
        bool IsActive() const { return m_mode != GizmoMode::None; }

        /** @brief Returns the current operation mode.
         *  @return The active GizmoMode.
         */
        GizmoMode GetMode() const { return m_mode; }

        /** @brief Returns the axis currently being dragged.
         *  @return The active GizmoAxis, or GizmoAxis::None when not dragging.
         */
        GizmoAxis GetDragAxis() const { return m_dragging; }

        // Returns true when gizmo consumed the mouse (caller should suppress scene
        // picking). outDeltaPos/Rot/Scale: incremental delta for this frame
        // (identity/zero if nothing dragged).
        /** @brief Processes mouse input and outputs the frame's transform delta.
         *  @param params     Update parameters including window, camera, and viewport dimensions.
         *  @return Result struct containing whether the gizmo consumed the mouse event and the transform deltas.
         */
        TransformGizmoResult Update(const TransformGizmoUpdateParams& params);

        // Queue DebugDraw lines; call before DebugDraw::Flush.
        /** @brief Queues debug-draw lines for the gizmo handles; must be called before DebugDraw::Flush.
         *  @param cam     Current viewport camera.
         *  @param screenW Viewport width in pixels.
         *  @param screenH Viewport height in pixels.
         */
        void Draw(const Camera &cam, int screenW, int screenH) const;


        // World-space handle length. Uses a fixed 1.0 unit length so gizmo
        // naturally scales on screen when zooming in/out.
        /** @brief Returns the world-space handle length for rendering and hit-testing.
         *  @param cam Current viewport camera used to derive perspective scale.
         *  @return Handle length in world units.
         */
        float HandleSize(const Camera &cam) const;

        // World-space direction for each target-local gizmo axis.
        /** @brief Returns the world-space unit direction for the given axis.
         *  @param axis The axis whose direction is requested.
         *  @return Normalised target-local axis transformed into world space.
         */
        Vec3 AxisDir(GizmoAxis axis) const;

        // Screen-space handle hit test. Returns nearest handle under the cursor,
        // or GizmoAxis::None.
        /** @brief Performs a screen-space hit test against all gizmo axes.
         *  @param mx      Mouse X in screen pixels (top-left origin).
         *  @param my      Mouse Y in screen pixels (top-left origin).
         *  @param cam     Current viewport camera.
         *  @param screenW Viewport width in pixels.
         *  @param screenH Viewport height in pixels.
         *  @return Nearest visible handle under the cursor, or GizmoAxis::None.
         */
        GizmoAxis PickAxis(float mx, float my, const Camera &cam, int screenW,
                           int screenH) const;

        // Project a world point to screen pixels. Returns false if behind camera.
        // (0,0) = top-left, (w,h) = bottom-right.
        /** @brief Projects a world-space point to screen pixels.
         *  @param p   World-space point to project.
         *  @param cam Current viewport camera.
         *  @param w   Viewport width in pixels.
         *  @param h   Viewport height in pixels.
         *  @param sx  Receives the screen X coordinate (0 = left).
         *  @param sy  Receives the screen Y coordinate (0 = top).
         *  @return False if the point is behind the camera near plane.
         */
        static bool WorldToScreen(const Vec3 &p, const Camera &cam, int w, int h,
                                  float &sx, float &sy);

        // Ray-plane intersection. Returns false if ray is parallel or hit is behind
        // origin.
        /** @brief Computes the intersection of a ray with a plane.
         *  @param ray    Ray to test.
         *  @param normal Plane normal (need not be normalised).
         *  @param point  Any point on the plane.
         *  @param outHit Receives the world-space intersection point on success.
         *  @return False if the ray is parallel to the plane or the hit is behind the ray origin.
         */
        static bool RayHitPlane(const Ray &ray, const Vec3 &normal, const Vec3 &point,
                                Vec3 &outHit);

        // Closest point on an infinite line to a ray's nearest approach.
        /** @brief Finds the point on an infinite line closest to a ray.
         *  @param ray        The ray to measure from.
         *  @param lineOrigin Any point on the line.
         *  @param lineDir    Direction of the line (need not be normalised).
         *  @return World-space point on the line nearest to the ray.
         */
        static Vec3 RayClosestOnLine(const Ray &ray, const Vec3 &lineOrigin,
                                     const Vec3 &lineDir);

    private:
        GizmoMode m_mode = GizmoMode::None;      /**< Current gizmo operation mode. */
        GizmoAxis m_hovered = GizmoAxis::None;   /**< Axis currently under the cursor. */
        GizmoAxis m_dragging = GizmoAxis::None;  /**< Axis currently being dragged. */
        Vec3 m_pos;                              /**< Current gizmo world-space position. */
        Quaternion m_rot;                        /**< Current gizmo world-space rotation. */
        Vec3 m_scale;                            /**< Current gizmo world-space scale. */

        // Drag bookkeeping
        bool m_prevMouseL = false;               /**< Previous-frame left mouse button state. */
        Vec3 m_dragAnchorPos;                    /**< Gizmo position captured at drag start. */
        Vec3 m_dragAnchorScale = Vec3::One();    /**< Target scale captured at drag start. */
        Vec3 m_dragPlaneNormal;                  /**< Drag plane normal used for per-frame ray hits. */
        float m_dragStartOffset = 0.0f;          /**< Initial axis projection captured at drag start. */
        float m_dragPrevOffset = 0.0f;           /**< Previous axis projection used for incremental translate/scale deltas. */
        float m_dragPrevSnappedOffset = 0.0f;    /**< Previous cumulative snapped translate/scale offset. */
        float m_dragPrevAngle = 0.0f;            /**< Previous cumulative angle used for incremental rotate deltas. */
        float m_dragPrevSnappedAngle = 0.0f;     /**< Previous cumulative snapped rotate angle. */
        Vec3 m_dragStartDir;                     /**< Initial tangent direction captured at drag start for rotation. */

        /** @brief Draws translate-mode gizmo handles. */
        void DrawTranslate(const Camera &cam, int screenW, int screenH) const;

        /** @brief Draws rotate-mode gizmo handles. */
        void DrawRotate(const Camera &cam, int screenW, int screenH) const;

        /** @brief Draws scale-mode gizmo handles. */
        void DrawScale(const Camera &cam, int screenW, int screenH) const;

        // Drag helpers (reduce Update complexity)
        /** @brief Initializes drag state for the currently hovered axis. */
        void BeginDrag(const Ray &ray, const Camera &cam);

        /** @brief Applies one frame of active drag input and outputs incremental transform deltas. */
        bool ApplyActiveDrag(const Ray &ray, const Camera &cam,
                             TransformGizmoResult &outResult,
                             float translateSnapStep = 0.0f,
                             float rotateSnapRadians = 0.0f,
                             float scaleSnapStep = 0.0f);

        // Test seam: grants test code access to private drag state and methods.
        friend struct ::TransformGizmoTestAccessor;
    };
} // namespace Horo::Editor
