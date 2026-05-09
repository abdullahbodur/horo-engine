/** @file Raycaster.h
 *  @brief Ray construction and intersection utilities for viewport picking.
 */
#pragma once
#include "math/Vec3.h"
#include "renderer/Camera.h"

namespace Horo::Editor {
    /** @brief A world-space ray defined by an origin and a normalised direction. */
    struct Ray {
        Vec3 origin;    /**< Ray origin in world space. */
        Vec3 direction; /**< Normalised ray direction. */
    };

    /** @brief Result of a successful ray vs. AABB intersection test. */
    struct RayAabbHit {
        float distance = -1.0f;     /**< Distance along the ray to the hit point; negative on miss. */
        Vec3 point = Vec3::Zero();  /**< World-space hit point. */
        Vec3 normal = Vec3::Zero(); /**< Outward surface normal at the hit point. */
    };

    // Unproject a screen-space pixel (top-left = 0,0) to a world-space ray.
    /** @brief Unprojects a screen-space pixel coordinate to a world-space ray.
     *  @param mouseX  Pixel X coordinate (0 = left edge).
     *  @param mouseY  Pixel Y coordinate (0 = top edge).
     *  @param screenW Viewport width in pixels.
     *  @param screenH Viewport height in pixels.
     *  @param cam     Camera providing the view/projection matrices.
     *  @return World-space ray passing through the given pixel.
     */
    Ray ScreenToRay(float mouseX, float mouseY, int screenW, int screenH,
                    const Camera &cam);

    // Ray vs axis-aligned bounding box (slab method).
    // center: box centre world-space; half: box half-extents.
    // Returns distance along ray to the nearest hit (>= 0), or -1 on miss.
    /** @brief Tests a ray against an axis-aligned bounding box (slab method) and fills hit details.
     *  @param ray    The ray to test.
     *  @param center Box centre in world space.
     *  @param half   Box half-extents.
     *  @param outHit Receives distance, point, and normal on a successful hit.
     *  @return True on intersection; false on miss.
     */
    bool RayVsAABBHit(const Ray &ray, const Vec3 &center, const Vec3 &half,
                      RayAabbHit *outHit);

    /** @brief Tests a ray against an axis-aligned bounding box and returns the hit distance.
     *  @param ray    The ray to test.
     *  @param center Box centre in world space.
     *  @param half   Box half-extents.
     *  @return Distance along the ray to the nearest hit (>= 0), or -1 on miss.
     */
    float RayVsAABB(const Ray &ray, const Vec3 &center, const Vec3 &half);

    /** @brief Tests whether a ray intersects the world Y=0 ground plane.
     *  @param ray         The ray to test.
     *  @param outHitPoint Receives the world-space intersection point on success.
     *  @return True when the ray hits the ground plane at a positive distance.
     */
    bool TryIntersectGroundPlane(const Ray &ray, Vec3 *outHitPoint);
} // namespace Horo::Editor
