#pragma once

/**
 * @file EditorViewportGridGeometry.h
 * @brief Allocation-free adaptive world-grid geometry for the editor viewport.
 */

#include "Horo/Runtime/Render/RenderScene.h"

#include <array>
#include <cstdint>
#include <span>

namespace Horo::Editor {
    /** @brief Inputs controlling one adaptive XZ-plane viewport grid build. */
    struct ViewportGridGeometryRequest {
        Render::RenderCameraView camera{};
        float aspect{1.0F};
        float viewportHeightPixels{1.0F};
        float targetMinorSpacingPixels{48.0F};
        float targetLineWidthPixels{1.5F};
    };

    /** @brief Primitive topology used by one grid presentation batch. */
    enum class ViewportGridPrimitiveTopology : std::uint8_t {
        Lines,
        Triangles,
    };

    /** @brief One grid geometry batch sharing a topology and presentation color. */
    struct ViewportGridLineBatch {
        std::span<const Math::Vec3> positions{};
        Math::Vec3 color{};
        ViewportGridPrimitiveTopology topology{ViewportGridPrimitiveTopology::Lines};

        /** @brief Reports whether positions form finite primitives for the declared topology. */
        [[nodiscard]] bool IsValid() const noexcept;
    };

    /**
     * @brief Fixed-capacity world-space grid output suitable for frame-hot rendering.
     *
     * The storage is owned by the geometry value. Returned spans remain valid until
     * the next build into the same value.
     */
    struct ViewportGridGeometry {
        static constexpr std::size_t MaxRegularVertices = 8192;

        std::array<Math::Vec3, MaxRegularVertices> regularPositions{};
        std::array<Math::Vec3, 4> axisPositions{};
        std::size_t regularVertexCount{0};
        std::size_t axisVertexCount{0};
        float minorSpacing{1.0F};

        /** @brief Returns the uniform regular-grid stroke batch. */
        [[nodiscard]] ViewportGridLineBatch RegularLines() const noexcept;

        /** @brief Returns the world-axis batch. */
        [[nodiscard]] ViewportGridLineBatch Axes() const noexcept;

        /** @brief Reports whether counts, spacing, and populated geometry are valid. */
        [[nodiscard]] bool IsValid() const noexcept;
    };

    /**
     * @brief Rebuilds an adaptive XZ-plane grid around the camera through its far clip range.
     * @param request Valid camera, aspect, viewport height, and desired screen spacing.
     * @param output Caller-owned fixed-capacity output replaced on success.
     * @return True when a finite grid was produced; false leaves an empty output.
     */
    [[nodiscard]] bool BuildViewportGridGeometry(const ViewportGridGeometryRequest &request, ViewportGridGeometry &output) noexcept;
}  // namespace Horo::Editor
