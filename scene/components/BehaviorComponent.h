#pragma once
#include <memory>

#include "scene/Entity.h"

namespace Horo {

class Registry;

// Abstract per-entity behavior.
class Behavior {
 public:
  virtual ~Behavior() = default;
  virtual void OnUpdate(Entity self, Registry& reg, float dt) = 0;
};

// Component that owns one behavior instance.
struct BehaviorComponent {
  std::unique_ptr<Behavior> behavior;
};

}  // namespace Horo
