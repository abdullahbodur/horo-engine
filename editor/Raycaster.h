#pragma once
#include "math/Vec3.h"
#include "renderer/Camera.h"

namespace Monolith::Editor {
struct Ray {
  Vec3 origin;
  Vec3 direction; // normalised
};

struct RayAabbHit {
  float distance = -1.0f;
  Vec3 point = Vec3::Zero();
  Vec3 normal = Vec3::Zero();
};

// Unproject a screen-space pixel (top-left = 0,0) to a world-space ray.
Ray ScreenToRay(float mouseX, float mouseY, int screenW, int screenH,
                const Camera &cam);

// Ray vs axis-aligned bounding box (slab method).
// center: box centre world-space; half: box half-extents.
// Returns distance along ray to the nearest hit (>= 0), or -1 on miss.
bool RayVsAABBHit(const Ray &ray, const Vec3 &center, const Vec3 &half,
                  RayAabbHit *outHit);

float RayVsAABB(const Ray &ray, const Vec3 &center, const Vec3 &half);

bool TryIntersectGroundPlane(const Ray &ray, Vec3 *outHitPoint);
} // namespace Monolith::Editor
