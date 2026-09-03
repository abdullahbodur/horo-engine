#pragma once

#include "Horo/Math/SceneMath.h"
#include "Horo/Runtime/Render/RenderScene.h"

#include <array>
#include <cstdint>

namespace Horo::Editor {
    struct MetalSelectionStyle {
        Math::Vec3 color;
        float strength;
    };

    static_assert(sizeof(MetalSelectionStyle) == sizeof(float) * 4);

    struct MetalLight {
        Math::Vec4 positionKind;
        Math::Vec4 directionRange;
        Math::Vec4 colorIntensity;
        Math::Vec4 cone;
    };

    static_assert(sizeof(MetalLight) == sizeof(float) * 16);

    struct MetalSceneLighting {
        std::array<MetalLight, Render::MaximumForwardLights> lights{};
        Math::Vec3 cameraPosition{};
        std::uint32_t lightCount{0};
        std::uint32_t shadowEnabled{0};
        std::uint32_t shadowLightIndex{0};
        Math::Vec2 padding{};
    };

    static_assert(sizeof(MetalSceneLighting) == sizeof(MetalLight) * Render::MaximumForwardLights + sizeof(float) * 8);

    struct MetalObjectTransforms {
        Math::Mat4 mvp;
        Math::Mat4 model;
        Math::Mat4 shadowMvp;
    };

    struct MetalGridStyle {
        Math::Vec3 color;
        float padding{0.0F};
    };

    static_assert(sizeof(MetalGridStyle) == sizeof(float) * 4);
}  // namespace Horo::Editor
