#pragma once
#include "math/Vec3.h"
#include "renderer/Camera.h"

namespace Monolith {
namespace Editor {

struct Ray {
  Vec3 origin;
  Vec3 direction;  // normalised
};

// Unproject a screen-space pixel (top-left = 0,0) to a world-space ray.
Ray ScreenToRay(float mouseX, float mouseY, int screenW, int screenH, const Camera& cam);

// Ray vs axis-aligned bounding box (slab method).
// center: box centre world-space; half: box half-extents.
// Returns distance along ray to the nearest hit (>= 0), or -1 on miss.
float RayVsAABB(const Ray& ray, const Vec3& center, const Vec3& half);
bool TryIntersectGroundPlane(const Ray& ray, Vec3* outHitPoint);

}  // namespace Editor
}  // namespace Monolith
