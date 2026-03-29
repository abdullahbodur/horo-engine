#pragma once
#include "scene/Entity.h"

namespace Monolith {

// Non-template base for ComponentPool<T>, used by Registry::Destroy to remove
// a component from all pools without knowing the type.
class IComponentPoolBase {
 public:
  virtual ~IComponentPoolBase() = default;
  virtual void Remove(Entity e) = 0;
  virtual void ClearAll() = 0;
};

}  // namespace Monolith
