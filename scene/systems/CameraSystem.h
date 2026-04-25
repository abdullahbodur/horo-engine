#pragma once
#include "scene/System.h"

namespace Monolith {
class CameraSystem : public System {
public:
  void OnUpdate(Registry &registry, float dt) override;
};
} // namespace Monolith
