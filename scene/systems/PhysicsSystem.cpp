#include "scene/systems/PhysicsSystem.h"

#include "physics/PhysicsWorld.h"
#include "scene/Registry.h"
#include "scene/components/RigidBodyComponent.h"
#include "scene/components/TransformComponent.h"

namespace Monolith {
    void PhysicsSystem::OnUpdate(Registry &registry, float dt) {
        m_world.Step(dt);

        // Sync physics bodies back to TransformComponents
        for (Entity e: registry.GetEntities<TransformComponent>()) {
            if (!registry.Has<RigidBodyComponent>(e))
                continue;

            const auto *body = registry.Get<RigidBodyComponent>(e).body;
            if (!body)
                continue;

            auto &tc = registry.Get<TransformComponent>(e);
            tc.previous = tc.current;
            tc.current.position = body->position;
            tc.current.rotation = body->orientation;
        }
    }
} // namespace Monolith
