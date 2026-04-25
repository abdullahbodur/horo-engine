#pragma once
#include "math/Transform.h"

namespace Monolith {
    struct TransformComponent {
        Transform current;
        Transform previous; // for interpolated rendering
    };
} // namespace Monolith
