#pragma once

#include "scene/System.h"

namespace Horo {
    class Camera;

    class SkinnedRenderSystem : public System {
    public:
        explicit SkinnedRenderSystem(Camera &camera, float &alpha)
            : m_camera(camera), m_alpha(alpha) {
        }

        void OnUpdate(Registry &registry, float dt) override;

    private:
        Camera &m_camera;
        float &m_alpha;
    };
} // namespace Horo
