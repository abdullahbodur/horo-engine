#include "editor/Raycaster.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "math/Mat4.h"
#include "math/Vec4.h"

namespace Monolith::Editor {
Ray ScreenToRay(float mouseX, float mouseY, int screenW, int screenH,
                const Camera &cam) {
  // Convert pixel coords to NDC [-1, 1].
  // Screen Y=0 is top; NDC Y=+1 is top, so flip Y.
  float ndcX = (2.0f * mouseX) / static_cast<float>(screenW) - 1.0f;
  float ndcY = -(2.0f * mouseY) / static_cast<float>(screenH) + 1.0f;

  Mat4 vpInv = cam.GetViewProjection().Inverse();

  // Unproject near (z=-1) and far (z=+1) clip-space points
  Vec4 nearClip = vpInv * Vec4(ndcX, ndcY, -1.0f, 1.0f);
  Vec4 farClip = vpInv * Vec4(ndcX, ndcY, 1.0f, 1.0f);

  Vec3 nearW = (std::abs(nearClip.w) > 1e-7f)
                   ? nearClip.XYZ() * (1.0f / nearClip.w)
                   : nearClip.XYZ();
  Vec3 farW = (std::abs(farClip.w) > 1e-7f) ? farClip.XYZ() * (1.0f / farClip.w)
                                            : farClip.XYZ();

  Ray r;
  r.origin = nearW;
  r.direction = (farW - nearW).Normalized();
  return r;
}

bool RayVsAABBHit(const Ray &ray, const Vec3 &center, const Vec3 &half,
                  RayAabbHit *outHit) {
  constexpr float kEps = 1e-6f;
  float tMin = 0.0f;
  float tMax = std::numeric_limits<float>::max();
  Vec3 enterNormal = Vec3::Zero();
  Vec3 exitNormal = Vec3::Zero();

  for (int i = 0; i < 3; ++i) {
    const float minB = center[i] - half[i];
    const float maxB = center[i] + half[i];
    const float d = ray.direction[i];
    const float o = ray.origin[i];

    if (std::abs(d) < kEps) {
      if (o < minB || o > maxB)
        return false;
      continue;
    }

    float t1 = (minB - o) / d;
    float t2 = (maxB - o) / d;
    Vec3 axisEnter = Vec3::Zero();
    Vec3 axisExit = Vec3::Zero();
    axisEnter[i] = -1.0f;
    axisExit[i] = 1.0f;

    if (t1 > t2) {
      std::swap(t1, t2);
      std::swap(axisEnter, axisExit);
    }

    if (t1 > tMin) {
      tMin = t1;
      enterNormal = axisEnter;
    }
    if (t2 < tMax) {
      tMax = t2;
      exitNormal = axisExit;
    }
    if (tMin > tMax)
      return false;
  }

  float hitDistance = tMin;
  Vec3 hitNormal = enterNormal;
  if (hitDistance <= kEps) {
    hitDistance = tMax;
    hitNormal = exitNormal;
  }
  if (hitDistance <= kEps)
    return false;

  if (outHit) {
    outHit->distance = hitDistance;
    outHit->point = ray.origin + ray.direction * hitDistance;
    outHit->normal = hitNormal;
  }
  return true;
}

float RayVsAABB(const Ray &ray, const Vec3 &center, const Vec3 &half) {
  constexpr float kEps = 1e-6f;
  if (const bool originInside =
          std::abs(ray.origin.x - center.x) <= half.x + kEps &&
          std::abs(ray.origin.y - center.y) <= half.y + kEps &&
          std::abs(ray.origin.z - center.z) <= half.z + kEps;
      originInside)
    return 0.0f;

  RayAabbHit hit;
  return RayVsAABBHit(ray, center, half, &hit) ? hit.distance : -1.0f;
}

bool TryIntersectGroundPlane(const Ray &ray, Vec3 *outHitPoint) {
  if (!outHitPoint)
    return false;

  if (constexpr float kEps = 1e-5f; std::abs(ray.direction.y) <= kEps)
    return false;

  const float t = -ray.origin.y / ray.direction.y;
  if (t <= 0.0f)
    return false;

  *outHitPoint = {ray.origin.x + ray.direction.x * t, 0.0f,
                  ray.origin.z + ray.direction.z * t};
  return true;
}
} // namespace Monolith::Editor
