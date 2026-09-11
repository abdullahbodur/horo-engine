#pragma once

/**
 * @file WorldDependencyPlan.h
 * @brief Deterministic in-memory co-load bundles and deferred-reference cook metadata.
 */

#include "Horo/WorldStreaming/WorldSpatialAssignment.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace Horo::WorldStreaming {
    /** @brief Cook behavior requested by one authored object reference. */
    enum class WorldDependencyKind : std::uint8_t {
        Hard,
        Soft,
    };

    /** @brief Exact immutable authored-object endpoint carried by a dependency edge. */
    struct WorldDependencyEndpoint final {
        WorldAuthoringObjectAddress address{}; /**< Stable page-scoped object address. */
        WorldAuthoringRevision revision{};     /**< Exact referenced immutable revision. */

        /** @brief Checks the persistent endpoint representation. @return True when address and revision are usable. */
        [[nodiscard]] bool IsValid() const noexcept {
            return address.IsValid() && revision.IsValid();
        }

        [[nodiscard]] constexpr auto operator<=>(const WorldDependencyEndpoint &) const noexcept = default;
    };

    /** @brief One directed edge from an authored dependency-graph snapshot. */
    struct WorldDependencyCandidate final {
        WorldDependencyEndpoint source{}; /**< Assigned object that owns the reference. */
        WorldDependencyEndpoint target{}; /**< Exact hard or potentially unresolved soft target. */
        WorldDependencyKind kind{};       /**< Co-load or deferred-resolution behavior. */

        [[nodiscard]] constexpr auto operator<=>(const WorldDependencyCandidate &) const noexcept = default;
    };

    /** @brief Mandatory host ceilings checked before dependency-plan storage is published. */
    struct WorldDependencyPlanLimits final {
        std::uint32_t maximumEdges{};                     /**< Maximum aggregate authored edges. */
        std::uint32_t maximumHardDependenciesPerObject{}; /**< Maximum outgoing hard edges per source object. */
        std::uint32_t maximumBundleMembers{};             /**< Maximum members in one transitive co-load bundle. */
        std::uint32_t maximumSoftReferences{};            /**< Maximum deferred soft-reference records. */
    };

    /** @brief Flat-table range for one canonically ordered co-load bundle. */
    struct WorldDependencyBundleEntry final {
        std::uint32_t memberOffset{}; /**< Offset into the result-owned member table. */
        std::uint32_t memberCount{};  /**< At least two transitively connected hard-dependency objects. */

        [[nodiscard]] constexpr auto operator<=>(const WorldDependencyBundleEntry &) const noexcept = default;
    };

    /** @brief Owned immutable dependency cook plan with no persistent wire-format authority. */
    class WorldDependencyPlan final {
    public:
        WorldDependencyPlan(const WorldDependencyPlan &) = delete;
        WorldDependencyPlan &operator=(const WorldDependencyPlan &) = delete;
        WorldDependencyPlan(WorldDependencyPlan &&) noexcept = default;
        WorldDependencyPlan &operator=(WorldDependencyPlan &&) = delete;

        /**
         * @brief Builds transitive hard co-load bundles and canonical deferred soft references transactionally.
         * @param assignments Immutable spatial cook output defining the admitted object revisions.
         * @param dependencies Directed authored edges; input storage is never retained or modified.
         * @param limits Mandatory non-zero count ceilings applied before result publication.
         * @return Owned in-memory plan, or a stable typed error for invalid, stale, over-capacity, missing-hard-target, or
         *         mixed hard/soft policy input with no partial result.
         */
        [[nodiscard]] static Result<WorldDependencyPlan> Create(const WorldSpatialAssignment &assignments,
                                                                std::span<const WorldDependencyCandidate> dependencies,
                                                                WorldDependencyPlanLimits limits);

        /** @brief Returns the partition bound to the source assignments. @return Stable immutable partition identity. */
        [[nodiscard]] constexpr const WorldPartitionId &Partition() const noexcept {
            return partition_;
        }

        /** @brief Returns co-load bundle ranges in canonical first-member order. @return Immutable result-owned view. */
        [[nodiscard]] std::span<const WorldDependencyBundleEntry> Bundles() const noexcept {
            return bundles_;
        }

        /**
         * @brief Returns exact object revisions in one co-load bundle.
         * @param bundleIndex Index into Bundles().
         * @return Immutable result-owned view, or an empty view for an out-of-range index.
         */
        [[nodiscard]] std::span<const WorldDependencyEndpoint> MembersForBundle(std::size_t bundleIndex) const noexcept;

        /**
         * @brief Returns canonical directed soft references, including unresolved targets.
         * @return Immutable result-owned view ordered by source then target endpoint.
         */
        [[nodiscard]] std::span<const WorldDependencyCandidate> SoftReferences() const noexcept {
            return softReferences_;
        }

    private:
        /** @brief Stores already validated and canonically ordered owned dependency state. */
        WorldDependencyPlan(WorldPartitionId partition, std::vector<WorldDependencyBundleEntry> bundles,
                            std::vector<WorldDependencyEndpoint> members, std::vector<WorldDependencyCandidate> softReferences) noexcept;

        WorldPartitionId partition_{};
        std::vector<WorldDependencyBundleEntry> bundles_;
        std::vector<WorldDependencyEndpoint> members_;
        std::vector<WorldDependencyCandidate> softReferences_;
    };
}  // namespace Horo::WorldStreaming
