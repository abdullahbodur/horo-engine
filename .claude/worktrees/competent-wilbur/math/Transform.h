#pragma once
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

}  // namespace Monolith
