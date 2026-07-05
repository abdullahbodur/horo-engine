#pragma once
#include "scene/System.h"

namespace Horo {
    class Camera;

    class RenderSystem : public System {
    public:
        explicit RenderSystem(float &alpha)
            : m_alpha(alpha) {
        }

        void OnUpdate(Registry &registry, float dt) override;

    private:
        float &m_alpha;
    };
} // namespace Horo
