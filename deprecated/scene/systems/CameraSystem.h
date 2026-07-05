#pragma once
#include "scene/System.h"

namespace Horo {
    class CameraSystem : public System {
    public:
        void OnUpdate(Registry &registry, float dt) override;
    };
} // namespace Horo
