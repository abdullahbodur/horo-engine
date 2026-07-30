#pragma once

/**
 * @file LightVisualizerGeometry.h
 * @brief Fixed-capacity backend-neutral debug geometry for selected editor lights.
 */

#include "Horo/Runtime/Render/RenderScene.h"

#include <array>
#include <span>

namespace Horo::Editor {
    /** @brief Inputs required to build one selected-light influence visualizer. */
    struct LightVisualizerGeometryRequest {
        Render::RenderCameraView camera{};
        Render::RenderLight light{};
    };

    /** @brief Fixed-capacity line-list geometry rendered with scene depth testing. */
    struct LightVisualizerGeometry {
        static constexpr std::size_t MaximumVertexCount = 512;

        std::array<Math::Vec3, MaximumVertexCount> positions{};
        std::size_t vertexCount{0};
        Math::Vec3 color{1.0F, 0.85F, 0.35F};

        /** @brief Returns the populated line endpoint pairs. */
        [[nodiscard]] std::span<const Math::Vec3> Lines() const noexcept;

        /** @brief Reports whether all populated line endpoints and presentation values are valid. */
        [[nodiscard]] bool IsValid() const noexcept;
    };

    /**
     * @brief Builds Directional, Point, or Spot influence geometry without allocation.
     * @param request Valid render camera and selected render light.
     * @param output Caller-owned output replaced on success.
     * @return True when finite line geometry was produced.
     */
    [[nodiscard]] bool BuildLightVisualizerGeometry(const LightVisualizerGeometryRequest &request,
                                                    LightVisualizerGeometry &output) noexcept;
}  // namespace Horo::Editor
