#pragma once
#include "scene/System.h"

namespace Monolith {
class BehaviorSystem : public System {
public:
  void OnUpdate(Registry &registry, float dt) override;
};
} // namespace Monolith
