#pragma once
#include "math/Vec3.h"

namespace Horo {
    struct Light {
        enum class Type {
            Directional = 0,
            Point = 1,
            Spot = 2,
            Rect = 3,
            Sky = 4,
        };

        Type type = Type::Point;
        Vec3 position = {}; // world-space for local lights
        Vec3 direction = {0, -1, 0}; // world-space, normalised for directional/cone lights
        Vec3 color = {1, 1, 1};
        float intensity = 1.0f;
        float radius = 10.0f; // effective range in metres for local lights
    };
} // namespace Horo
