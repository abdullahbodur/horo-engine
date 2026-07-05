#pragma once

#include "scene/System.h"

namespace Horo {
    class Camera;

    class SkinnedRenderSystem : public System {
    public:
        explicit SkinnedRenderSystem(float &alpha)
            : m_alpha(alpha) {
        }

        void OnUpdate(Registry &registry, float dt) override;

    private:
        float &m_alpha;
    };
} // namespace Horo
