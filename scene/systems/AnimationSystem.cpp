#include "scene/systems/AnimationSystem.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "math/Mat4.h"
#include "scene/Registry.h"
#include "scene/components/AnimationComponent.h"

namespace Monolith {
    void AnimationSystem::OnUpdate(Registry &registry, float dt) {
        for (Entity e: registry.GetEntities<AnimationComponent>()) {
            auto &ac = registry.Get<AnimationComponent>(e);

            if (!ac.playing || !ac.skeleton || !ac.currentClip)
                continue;

            // Advance playback time.
            ac.time += dt * ac.speed;

            // Wrap or clamp to clip bounds.
            const float duration = ac.currentClip->duration;
            if (ac.loop) {
                ac.time = std::fmod(ac.time, duration);
                // fmod can return negative values when ac.time is negative (speed < 0
                // edge case).
                if (ac.time < 0.0f)
                    ac.time += duration;
            } else {
                ac.time = std::clamp(ac.time, 0.0f, duration);
            }

            const int boneCount = ac.skeleton->BoneCount();

            // Ensure the output palette is sized correctly.
            if (static_cast<int>(ac.boneMatrices.size()) != boneCount)
                ac.boneMatrices.resize(boneCount);

            // Sample the clip into per-bone local TRS matrices.
            // AnimationClip::Sample requires the vector to be pre-sized to boneCount.
            std::vector<Mat4> localTransforms(boneCount, Mat4::Identity());
            ac.currentClip->Sample(ac.time, localTransforms);

            // Propagate global transforms top-down.
            // The skeleton invariant guarantees parentIndex < child index, so a single
            // forward pass is sufficient — no recursion or stack needed.
            std::vector<Mat4> globalTransforms(boneCount);
            for (int i = 0; i < boneCount; ++i) {
                const int parent = ac.skeleton->GetBone(i).parentIndex;
                if (parent < 0)
                    globalTransforms[i] = localTransforms[i];
                else
                    globalTransforms[i] = globalTransforms[parent] * localTransforms[i];
            }

            // Compute final skinning matrices: global * inverseBindMatrix.
            for (int i = 0; i < boneCount; ++i)
                ac.boneMatrices[i] =
                        globalTransforms[i] * ac.skeleton->GetBone(i).inverseBindMatrix;
        }
    }
} // namespace Monolith
