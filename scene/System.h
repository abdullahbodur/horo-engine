#pragma once

namespace Monolith {
class Registry;

class System {
public:
  virtual ~System() = default;

  virtual void OnUpdate(Registry &registry, float dt) = 0;
};
} // namespace Monolith
