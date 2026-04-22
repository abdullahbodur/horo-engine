#include "scene/systems/SkinnedRenderSystem.h"

#include <vector>

#include "math/Mat4.h"
#include "math/Quaternion.h"
#include "math/Transform.h"
#include "math/Vec3.h"
#include "renderer/Renderer.h"
#include "scene/Registry.h"
#include "scene/components/AnimationComponent.h"
#include "scene/components/SkinnedMeshComponent.h"
#include "scene/components/TransformComponent.h"

namespace Monolith {
    // Submits all visible skinned-mesh entities into the active explicit renderer
    // pass. Frame/pass orchestration stays with the scene owner.
    void SkinnedRenderSystem::OnUpdate(Registry &registry, float dt) {
        (void) dt;

        // A static empty palette is passed to SubmitSkinned when no
        // AnimationComponent is present (or boneMatrices is not yet populated).  The
        // skinning shader must handle this gracefully — typically by using vertex
        // bone weights of zero so all influence falls back to bone 0 identity, or by
        // an explicit fallback.
        static const std::vector<Mat4> kEmptyBones;

        for (Entity e: registry.GetEntities<SkinnedMeshComponent>()) {
            const auto &mc = registry.Get<SkinnedMeshComponent>(e);
            if (!mc.visible || !mc.mesh || !mc.material)
                continue;

            // Interpolate transform for sub-step smoothing (mirrors RenderSystem).
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

            // Use the pre-computed bone palette from AnimationSystem if available.
            const std::vector<Mat4> *bones = &kEmptyBones;
            if (registry.Has<AnimationComponent>(e)) {
                const auto &ac = registry.Get<AnimationComponent>(e);
                if (!ac.boneMatrices.empty())
                    bones = &ac.boneMatrices;
            }

            Renderer::SubmitSkinned(*mc.mesh, model, *mc.material, *bones);
        }
    }
} // namespace Monolith
