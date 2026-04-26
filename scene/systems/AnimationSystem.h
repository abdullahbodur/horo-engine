#pragma once

#include "scene/System.h"

namespace Horo {
    class AnimationSystem : public System {
    public:
        void OnUpdate(Registry &registry, float dt) override;
    };
} // namespace Horo
