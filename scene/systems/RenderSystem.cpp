#include "scene/systems/RenderSystem.h"

#include "math/Transform.h"
#include "renderer/Renderer.h"
#include "scene/Registry.h"
#include "scene/components/MeshComponent.h"
#include "scene/components/TransformComponent.h"

namespace Horo {
    // Submits all visible mesh entities into whichever explicit renderer pass is
    // currently active. Frame/pass orchestration stays with the scene owner.
    void RenderSystem::OnUpdate(Registry &registry, float dt) {
        (void) dt;

        for (Entity e: registry.GetEntities<MeshComponent>()) {
            const auto &mc = registry.Get<MeshComponent>(e);
            if (!mc.visible || !mc.mesh || !mc.material)
                continue;

            Mat4 model;
            if (registry.Has<TransformComponent>(e)) {
                const auto &tc = registry.Get<TransformComponent>(e);
                Transform interp;
                interp.position =
                        Vec3::Lerp(tc.previous.position, tc.current.position, m_alpha);
                interp.rotation =
                        Quaternion::Slerp(tc.previous.rotation, tc.current.rotation, m_alpha);
                interp.scale = tc.current.scale;
                model = interp.ToMatrix();
            }
            Renderer::Submit(*mc.mesh, model, *mc.material);
        }
    }
} // namespace Horo
