#pragma once
#include "math/Mat4.h"
#include "math/Vec3.h"

namespace Monolith {
class Camera {
public:
  Vec3 position = {0, 3, 8};
  Vec3 target = Vec3::Zero();
  Vec3 up = Vec3::Up();

  float fovY = 60.0f; // degrees
  float zNear = 0.1f;
  float zFar = 1000.0f;

  // Updated by Application on resize
  float aspect = 16.0f / 9.0f;

  Mat4 GetView() const;

  Mat4 GetProjection() const;

  // Combined VP — use in shaders
  Mat4 GetViewProjection() const { return GetProjection() * GetView(); }

  Vec3 GetForward() const { return (target - position).Normalized(); }
  Vec3 GetRight() const { return Vec3::Cross(GetForward(), up).Normalized(); }
};
} // namespace Monolith
