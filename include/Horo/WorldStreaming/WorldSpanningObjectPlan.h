#pragma once

/**
 * @file WorldSpanningObjectPlan.h
 * @brief Deterministic policy projection for authored objects spanning many world cells.
 */

#include "Horo/WorldStreaming/WorldSpatialAssignment.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace Horo::WorldStreaming {
    /** @brief Explicit cook policy for an object whose source assignment exceeds the direct-cell threshold. */
    enum class WorldSpanningObjectPolicy : std::uint8_t {
        SingleCellOwner = 1,
        SplitPerCell,
        NonSpatial,
    };

    /** @brief Placement selected by the spanning-object cook stage. */
    enum class WorldSpanningObjectPlacement : std::uint8_t {
        Direct,
        SingleCellOwner,
        SplitPerCell,
        NonSpatial,
    };

    /** @brief One exact policy decision for an oversized authored-object revision. */
    struct WorldSpanningObjectDirective final {
        WorldAuthoringObjectAddress address{}; /**< Stable page-scoped object address. */
        WorldAuthoringRevision revision{};     /**< Exact immutable revision from spatial assignment. */
        WorldSpanningObjectPolicy policy{};    /**< Explicit oversized-object handling policy. */
        StreamingCellId ownerCell{};           /**< Covered owner cell; used only by SingleCellOwner. */

        [[nodiscard]] constexpr auto operator<=>(const WorldSpanningObjectDirective &) const noexcept = default;
    };

    /** @brief Mandatory host ceilings for spanning-object plan publication. */
    struct WorldSpanningObjectPlanLimits final {
        std::uint32_t maximumObjects{};              /**< Maximum admitted assignment entries. */
        std::uint32_t maximumDirectCellsPerObject{}; /**< Inclusive threshold before an explicit directive is required. */
        std::uint32_t maximumPlacementCells{};       /**< Maximum aggregate result-owned placement cells. */
    };

    /** @brief Canonical placement metadata for one authored-object revision. */
    struct WorldSpanningObjectEntry final {
        WorldAuthoringObjectAddress address{};    /**< Stable page-scoped object address. */
        WorldAuthoringRevision revision{};        /**< Exact immutable authoring revision. */
        WorldSpanningObjectPlacement placement{}; /**< Selected direct, owner, split, or non-spatial behavior. */
        std::uint32_t cellOffset{};               /**< Offset into the result-owned placement-cell table. */
        std::uint32_t cellCount{};                /**< Zero only for NonSpatial; one for SingleCellOwner. */

        [[nodiscard]] constexpr auto operator<=>(const WorldSpanningObjectEntry &) const noexcept = default;
    };

    /** @brief Owned immutable output of a pure spanning-object policy cook pass. */
    class WorldSpanningObjectPlan final {
    public:
        WorldSpanningObjectPlan(const WorldSpanningObjectPlan &) = delete;
        WorldSpanningObjectPlan &operator=(const WorldSpanningObjectPlan &) = delete;
        WorldSpanningObjectPlan(WorldSpanningObjectPlan &&) noexcept = default;
        WorldSpanningObjectPlan &operator=(WorldSpanningObjectPlan &&) = delete;

        /**
         * @brief Applies explicit policy only to source objects exceeding the direct-cell threshold.
         * @param assignments Immutable canonical spatial-assignment output; never retained or modified.
         * @param directives Exact oversized-object decisions; order does not affect the result.
         * @param limits Mandatory non-zero object, direct-cell, and aggregate placement ceilings.
         * @return Canonical owned placement plan, or a stable typed error with no partial result.
         */
        [[nodiscard]] static Result<WorldSpanningObjectPlan> Create(const WorldSpatialAssignment &assignments,
                                                                    std::span<const WorldSpanningObjectDirective> directives,
                                                                    WorldSpanningObjectPlanLimits limits);

        /** @brief Returns the source partition identity. @return Stable immutable partition identity. */
        [[nodiscard]] constexpr const WorldPartitionId &Partition() const noexcept {
            return partition_;
        }

        /** @brief Returns placement entries in canonical authored-object order. @return Immutable result-owned view. */
        [[nodiscard]] std::span<const WorldSpanningObjectEntry> Objects() const noexcept {
            return objects_;
        }

        /**
         * @brief Returns placement cells for one object.
         * @param objectIndex Index into Objects().
         * @return Direct/split cells, the exact owner cell, or an empty view for non-spatial/out-of-range entries.
         */
        [[nodiscard]] std::span<const StreamingCellId> CellsForObject(std::size_t objectIndex) const noexcept;

    private:
        /** @brief Stores already validated and canonically ordered owned plan state. */
        WorldSpanningObjectPlan(WorldPartitionId partition, std::vector<WorldSpanningObjectEntry> objects,
                                std::vector<StreamingCellId> cells) noexcept;

        WorldPartitionId partition_{};
        std::vector<WorldSpanningObjectEntry> objects_;
        std::vector<StreamingCellId> cells_;
    };
}  // namespace Horo::WorldStreaming
