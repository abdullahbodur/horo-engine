#include "scene/systems/BehaviorSystem.h"

#include "scene/Registry.h"
#include "scene/components/BehaviorComponent.h"

namespace Monolith {
void BehaviorSystem::OnUpdate(Registry &registry, float dt) {
  for (Entity e : registry.GetEntities<BehaviorComponent>())
    registry.Get<BehaviorComponent>(e).behavior->OnUpdate(e, registry, dt);
}
} // namespace Monolith
