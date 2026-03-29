#include "scene/systems/CameraSystem.h"

#include "scene/Registry.h"
#include "scene/components/CameraComponent.h"
#include "scene/components/TransformComponent.h"

namespace Monolith {

void CameraSystem::OnUpdate(Registry& registry, float dt) {
  (void)dt;

  for (Entity e : registry.GetEntities<CameraComponent>()) {
    auto& cc = registry.Get<CameraComponent>(e);
    if (!cc.isActive)
      continue;

    if (registry.Has<TransformComponent>(e)) {
      auto& tc = registry.Get<TransformComponent>(e);
      cc.camera.position = tc.current.position;
      cc.camera.target = tc.current.position + tc.current.Forward();
    }
  }
}

}  // namespace Monolith
