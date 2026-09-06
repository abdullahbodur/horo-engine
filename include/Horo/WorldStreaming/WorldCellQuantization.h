#pragma once

/**
 * @file WorldCellQuantization.h
 * @brief Deterministic checked world-coordinate to streaming-cell policy.
 */

#include "Horo/Math/WorldCoordinate64.h"
#include "Horo/WorldStreaming/WorldStreamingIdentity.h"

#include <cstdint>

namespace Horo::WorldStreaming {
    /** @brief Inclusive cell-coordinate bounds for one validated partition grid. */
    struct WorldCellBounds final {
        std::int32_t minimumX{}; /**< Inclusive minimum X cell. */
        std::int32_t maximumX{}; /**< Inclusive maximum X cell. */
        std::int32_t minimumY{}; /**< Inclusive minimum Y cell. */
        std::int32_t maximumY{}; /**< Inclusive maximum Y cell. */
        std::int32_t minimumZ{}; /**< Inclusive minimum Z cell. */
        std::int32_t maximumZ{}; /**< Inclusive maximum Z cell. */

        [[nodiscard]] constexpr auto operator<=>(const WorldCellBounds &) const noexcept = default;
    };

    /** @brief Immutable quantization inputs copied from one validated world.index grid descriptor. */
    class WorldCellQuantizationPolicy final {
    public:
        /**
         * @brief Validates a partition grid without publishing partial state.
         * @param origin Exact global position of cell (0,0,0)'s minimum boundary.
         * @param baseCellSizeMillimeters Positive level-zero cell edge length.
         * @param bounds Inclusive valid cell coordinates at every declared LOD.
         * @param lodLevels Number of supported LOD levels in [1, 32].
         * @return Immutable policy or WorldStreamingErrors::QuantizationPolicyInvalid.
         */
        [[nodiscard]] static Result<WorldCellQuantizationPolicy> Create(Math::WorldCoordinate64 origin,
                                                                        std::int64_t baseCellSizeMillimeters, WorldCellBounds bounds,
                                                                        std::uint8_t lodLevels);

        /** @brief Returns the exact grid origin. @return Immutable global origin. */
        [[nodiscard]] constexpr const Math::WorldCoordinate64 &Origin() const noexcept {
            return origin_;
        }

        /** @brief Returns the level-zero cell edge. @return Positive millimeter length. */
        [[nodiscard]] constexpr std::int64_t BaseCellSizeMillimeters() const noexcept {
            return baseCellSizeMillimeters_;
        }

        /** @brief Returns inclusive manifest bounds. @return Immutable cell-coordinate bounds. */
        [[nodiscard]] constexpr WorldCellBounds Bounds() const noexcept {
            return bounds_;
        }

        /** @brief Returns supported LOD count. @return Value in [1, 32]. */
        [[nodiscard]] constexpr std::uint8_t LodLevels() const noexcept {
            return lodLevels_;
        }

    private:
        constexpr WorldCellQuantizationPolicy(Math::WorldCoordinate64 origin, const std::int64_t baseCellSizeMillimeters,
                                              const WorldCellBounds bounds, const std::uint8_t lodLevels) noexcept
            : origin_(origin), baseCellSizeMillimeters_(baseCellSizeMillimeters), bounds_(bounds), lodLevels_(lodLevels) {}

        Math::WorldCoordinate64 origin_{};
        std::int64_t baseCellSizeMillimeters_{};
        WorldCellBounds bounds_{};
        std::uint8_t lodLevels_{};
    };

    /**
     * @brief Quantizes an exact global coordinate using floor division and half-open cell ownership.
     * @param coordinate Canonical ADR-026 global coordinate.
     * @param policy Validated immutable partition grid.
     * @param lod Requested manifest LOD.
     * @param layer Valid manifest layer identity; membership remains a descriptor responsibility.
     * @return Exact cell tuple, or a typed invalid/range/bounds/unsupported result with no state change.
     */
    [[nodiscard]] Result<StreamingCellId> QuantizeWorldToCell(const Math::WorldCoordinate64 &coordinate,
                                                              const WorldCellQuantizationPolicy &policy, std::uint8_t lod,
                                                              StreamingLayerId layer);
}  // namespace Horo::WorldStreaming
