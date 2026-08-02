#pragma once

/**
 * @file EditorViewportGridGeometry.h
 * @brief Allocation-free adaptive world-grid geometry for the editor viewport.
 */

#include "Horo/Runtime/Render/RenderScene.h"

#include <array>
#include <span>

namespace Horo::Editor {
    /** @brief Inputs controlling one adaptive XZ-plane viewport grid build. */
    struct ViewportGridGeometryRequest {
        Render::RenderCameraView camera{};
        float aspect{1.0F};
        float viewportHeightPixels{1.0F};
        float targetMinorSpacingPixels{48.0F};
    };

    /** @brief One line-list batch sharing a presentation color. */
    struct ViewportGridLineBatch {
        std::span<const Math::Vec3> positions{};
        Math::Vec3 color{};

        /** @brief Reports whether positions form finite line endpoint pairs. */
        [[nodiscard]] bool IsValid() const noexcept;
    };

    /**
     * @brief Fixed-capacity world-space grid output suitable for frame-hot rendering.
     *
     * The storage is owned by the geometry value. Returned spans remain valid until
     * the next build into the same value.
     */
    struct ViewportGridGeometry {
        static constexpr std::size_t MaxVerticesPerBatch = 512;

        std::array<Math::Vec3, MaxVerticesPerBatch> minorPositions{};
        std::array<Math::Vec3, MaxVerticesPerBatch> majorPositions{};
        std::array<Math::Vec3, 4> axisPositions{};
        std::size_t minorVertexCount{0};
        std::size_t majorVertexCount{0};
        std::size_t axisVertexCount{0};
        float minorSpacing{1.0F};
        float majorSpacing{10.0F};

        /** @brief Returns the minor-line batch. */
        [[nodiscard]] ViewportGridLineBatch MinorLines() const noexcept;

        /** @brief Returns the major-line batch. */
        [[nodiscard]] ViewportGridLineBatch MajorLines() const noexcept;

        /** @brief Returns the world-axis batch. */
        [[nodiscard]] ViewportGridLineBatch Axes() const noexcept;

        /** @brief Reports whether counts, spacing, and all populated positions are valid. */
        [[nodiscard]] bool IsValid() const noexcept;
    };

    /**
     * @brief Rebuilds an adaptive XZ-plane grid around the camera target.
     * @param request Valid camera, aspect, viewport height, and desired screen spacing.
     * @param output Caller-owned fixed-capacity output replaced on success.
     * @return True when a finite grid was produced; false leaves an empty output.
     */
    [[nodiscard]] bool BuildViewportGridGeometry(const ViewportGridGeometryRequest &request, ViewportGridGeometry &output) noexcept;
}  // namespace Horo::Editor
