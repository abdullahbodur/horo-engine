#pragma once

namespace Horo {

enum class ColliderType { Sphere, Box, Capsule, Plane };

struct Collider {
  ColliderType type;
  explicit Collider(ColliderType t) : type(t) {}
  virtual ~Collider() = default;
};

}  // namespace Horo
