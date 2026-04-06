#include "editor/Raycaster.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "math/Mat4.h"
#include "math/Vec4.h"

namespace Monolith {
namespace Editor {

Ray ScreenToRay(float mouseX, float mouseY, int screenW, int screenH, const Camera& cam) {
  // Convert pixel coords to NDC [-1, 1].
  // Screen Y=0 is top; NDC Y=+1 is top, so flip Y.
  float ndcX = (2.0f * mouseX) / static_cast<float>(screenW) - 1.0f;
  float ndcY = -(2.0f * mouseY) / static_cast<float>(screenH) + 1.0f;

  Mat4 vpInv = cam.GetViewProjection().Inverse();

  // Unproject near (z=-1) and far (z=+1) clip-space points
  Vec4 nearClip = vpInv * Vec4(ndcX, ndcY, -1.0f, 1.0f);
  Vec4 farClip = vpInv * Vec4(ndcX, ndcY, 1.0f, 1.0f);

  Vec3 nearW =
      (std::abs(nearClip.w) > 1e-7f) ? nearClip.XYZ() * (1.0f / nearClip.w) : nearClip.XYZ();
  Vec3 farW = (std::abs(farClip.w) > 1e-7f) ? farClip.XYZ() * (1.0f / farClip.w) : farClip.XYZ();

  Ray r;
  r.origin = nearW;
  r.direction = (farW - nearW).Normalized();
  return r;
}

float RayVsAABB(const Ray& ray, const Vec3& center, const Vec3& half) {
  constexpr float kEps = 1e-6f;
  float tMin = 0.0f;
  float tMax = std::numeric_limits<float>::max();

  for (int i = 0; i < 3; ++i) {
    float minB = center[i] - half[i];
    float maxB = center[i] + half[i];
    float d = ray.direction[i];
    float o = ray.origin[i];

    if (std::abs(d) < kEps) {
      if (o < minB || o > maxB)
        return -1.0f;
    } else {
      float t1 = (minB - o) / d;
      float t2 = (maxB - o) / d;
      if (t1 > t2)
        std::swap(t1, t2);
      tMin = std::max(tMin, t1);
      tMax = std::min(tMax, t2);
      if (tMin > tMax)
        return -1.0f;
    }
  }

  return tMin;
}

bool TryIntersectGroundPlane(const Ray& ray, Vec3* outHitPoint) {
  if (!outHitPoint)
    return false;

  constexpr float kEps = 1e-5f;
  if (std::abs(ray.direction.y) <= kEps)
    return false;

  const float t = -ray.origin.y / ray.direction.y;
  if (t <= 0.0f)
    return false;

  *outHitPoint = {ray.origin.x + ray.direction.x * t,
                  0.0f,
                  ray.origin.z + ray.direction.z * t};
  return true;
}

}  // namespace Editor
}  // namespace Monolith
