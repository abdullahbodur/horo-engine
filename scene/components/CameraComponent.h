#pragma once
#include "renderer/Camera.h"

namespace Monolith {
    struct CameraComponent {
        Camera camera;
        bool isActive = false;
    };
} // namespace Monolith
