#pragma once
#include "math/Vec3.h"

namespace Monolith {
struct Light {
  enum class Type { Directional = 0, Point = 1 };

  Type type = Type::Point;
  Vec3 position = {};          // world-space (Point lights)
  Vec3 direction = {0, -1, 0}; // world-space, normalised (Directional)
  Vec3 color = {1, 1, 1};
  float intensity = 1.0f;
  float radius = 10.0f; // effective range in metres (Point only)
};
} // namespace Monolith
