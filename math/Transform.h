#pragma once
#include <algorithm>
#include <limits>

#include "math/Mat4.h"
#include "math/Quaternion.h"
#include "math/Vec3.h"

namespace Monolith {

struct Transform {
  Vec3 position = Vec3::Zero();
  Quaternion rotation = Quaternion::Identity();
  Vec3 scale = Vec3::One();

  Transform() = default;
  Transform(const Vec3& pos,
            const Quaternion& rot = Quaternion::Identity(),
            const Vec3& scale = Vec3::One())
      : position(pos), rotation(rot), scale(scale) {}

  Mat4 ToMatrix() const {
    return Mat4::Translate(position) * Mat4::Rotate(rotation) * Mat4::Scale(scale);
  }

  // Transform a point from local space to world space
  Vec3 TransformPoint(const Vec3& local) const { return position + rotation * (local * scale); }

  Vec3 Forward() const { return rotation * Vec3::Forward(); }
  Vec3 Up() const { return rotation * Vec3::Up(); }
  Vec3 Right() const { return rotation * Vec3::Right(); }

  static Transform Identity() { return {}; }
};

// World-space axis-aligned bounds for a mesh-local AABB (center ± half) after Transform.
inline void WorldAabbFromLocalBox(const Vec3& localCenter,
                                  const Vec3& localHalf,
                                  const Transform& worldFromLocal,
                                  Vec3& outWorldCenter,
                                  Vec3& outWorldHalf) {
  const float inf = std::numeric_limits<float>::max();
  Vec3 wmin{inf, inf, inf};
  Vec3 wmax{-inf, -inf, -inf};
  for (int ix = 0; ix < 2; ++ix)
    for (int iy = 0; iy < 2; ++iy)
      for (int iz = 0; iz < 2; ++iz) {
        Vec3 local = localCenter;
        local.x += (ix ? localHalf.x : -localHalf.x);
        local.y += (iy ? localHalf.y : -localHalf.y);
        local.z += (iz ? localHalf.z : -localHalf.z);
        Vec3 w = worldFromLocal.TransformPoint(local);
        wmin.x = std::min(wmin.x, w.x);
        wmin.y = std::min(wmin.y, w.y);
        wmin.z = std::min(wmin.z, w.z);
        wmax.x = std::max(wmax.x, w.x);
        wmax.y = std::max(wmax.y, w.y);
        wmax.z = std::max(wmax.z, w.z);
      }
  outWorldCenter = (wmin + wmax) * 0.5f;
  outWorldHalf = (wmax - wmin) * 0.5f;
}

}  // namespace Monolith
