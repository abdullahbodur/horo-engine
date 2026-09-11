#pragma once

/**
 * @file DestructionRegistry.h
 * @brief Explicit destruction registration, bounded queries, and immutable capability projections.
 */

#include "Horo/Destruction/DestructionCommand.h"

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <span>

namespace Horo::Destruction {
    /** @brief Version of the portable destruction registry contract. */
    inline constexpr std::uint32_t CurrentDestructionRegistryContractVersion = 1;

    struct DestructionRegistryRevisionTag;
    /** @brief Non-wrapping revision of an explicitly owned registry publication. */
    using DestructionRegistryRevision = DestructionStableIdentity<DestructionRegistryRevisionTag>;

    /** @brief Compile-time storage ceilings for registry composition and one query response. */
    struct DestructionRegistryHardLimits final {
        static constexpr std::size_t Entries = 256;     /**< Maximum indexed destructibles. */
        static constexpr std::size_t QueryResults = 64; /**< Maximum records returned by one query. */
    };

    /** @brief Product-selected finite limits that may only narrow hard storage ceilings. */
    struct DestructionRegistryLimits final {
        std::size_t maximumEntries{DestructionRegistryHardLimits::Entries};           /**< Maximum live index records. */
        std::size_t maximumQueryResults{DestructionRegistryHardLimits::QueryResults}; /**< Maximum records in one response. */

        [[nodiscard]] constexpr auto operator<=>(const DestructionRegistryLimits &) const noexcept = default;
    };

    /**
     * @brief Immutable registry record projected from state and capability owners.
     * @details This value neither owns nor extends the lifetime of a destructible, artifact, backend, or authority grant.
     */
    struct DestructionRegistryRecord final {
        DestructionStateSnapshot state{};                      /**< Exact published semantic state. */
        DestructionCapabilityRevision capabilityRevision{};    /**< Exact capability publication. */
        DestructionCommandCapabilitySet commandCapabilities{}; /**< Explicit supported command families. */
        DestructionLimits limits{};                            /**< Exact bounded operation ceilings. */

        [[nodiscard]] constexpr auto operator<=>(const DestructionRegistryRecord &) const noexcept = default;
    };

    /** @brief Optional phase selection for one bounded registry query. */
    enum class DestructionPhaseFilter : std::uint8_t {
        Any,
        Intact,
        Damaged,
        Destroyed,
        Count,
    };

    /** @brief Fixed-cost query input; no field grants mutation or feature ownership. */
    struct DestructionQuery final {
        DestructionWorldId world{};                                /**< Exact world to search. */
        DestructionPhaseFilter phase{DestructionPhaseFilter::Any}; /**< Optional exact semantic phase. */
        DestructionFeatureSet requiredFeatures{};                  /**< Every requested feature must be present. */
        std::size_t maximumResults{1};                             /**< Non-zero response bound within registry limits. */

        [[nodiscard]] constexpr auto operator<=>(const DestructionQuery &) const noexcept = default;
    };

    /** @brief Fixed-capacity owned query result in stable handle order. */
    class DestructionQueryResult final {
    public:
        /** @brief Returns the registry revision queried. @return Exact immutable publication revision. */
        [[nodiscard]] constexpr DestructionRegistryRevision Revision() const noexcept {
            return revision_;
        }

        /** @brief Returns copied immutable records. @return Span valid for this result's lifetime. */
        [[nodiscard]] std::span<const DestructionRegistryRecord> Records() const noexcept;

        /** @brief Reports omitted matching records. @return True when the caller's bound truncated the response. */
        [[nodiscard]] constexpr bool HasMore() const noexcept {
            return hasMore_;
        }

    private:
        friend class DestructionRegistrySnapshot;
        DestructionRegistryRevision revision_{};
        std::array<DestructionRegistryRecord, DestructionRegistryHardLimits::QueryResults> records_{};
        std::size_t count_{};
        bool hasMore_{};
    };

    /** @brief Immutable exact-target capability evidence copied from one registry publication. */
    class DestructionCapabilitySnapshot final {
    public:
        /** @brief Returns the exact target. @return Generation-fenced non-owning handle. */
        [[nodiscard]] constexpr DestructionHandle Target() const noexcept {
            return target_;
        }

        /** @brief Returns the observed semantic revision. @return Exact state revision. */
        [[nodiscard]] constexpr DestructionStateRevision StateRevision() const noexcept {
            return stateRevision_;
        }

        /** @brief Returns the registry publication. @return Exact registry revision. */
        [[nodiscard]] constexpr DestructionRegistryRevision RegistryRevision() const noexcept {
            return registryRevision_;
        }

        /** @brief Returns the capability publication. @return Exact capability revision. */
        [[nodiscard]] constexpr DestructionCapabilityRevision CapabilityRevision() const noexcept {
            return capabilityRevision_;
        }

        /** @brief Returns effective features without fallback. @return Fixed feature set. */
        [[nodiscard]] constexpr DestructionFeatureSet Features() const noexcept {
            return features_;
        }

        /** @brief Returns explicitly supported command families. @return Fixed capability set. */
        [[nodiscard]] constexpr DestructionCommandCapabilitySet Commands() const noexcept {
            return commands_;
        }

        /** @brief Returns exact finite operation ceilings. @return Immutable bounded limits. */
        [[nodiscard]] constexpr const DestructionLimits &Limits() const noexcept {
            return limits_;
        }

    private:
        friend class DestructionRegistrySnapshot;
        DestructionHandle target_{};
        DestructionStateRevision stateRevision_{};
        DestructionRegistryRevision registryRevision_{};
        DestructionCapabilityRevision capabilityRevision_{};
        DestructionFeatureSet features_{};
        DestructionCommandCapabilitySet commands_{};
        DestructionLimits limits_{};
    };

    /**
     * @brief Immutable fixed-capacity registry publication safe to retain across replacement and shutdown.
     * @details The snapshot contains value copies only. It exposes no mutation, native handles, callbacks, or ownership leases.
     */
    class DestructionRegistrySnapshot final {
    public:
        /** @brief Returns the exact registry publication. @return Non-zero revision. */
        [[nodiscard]] constexpr DestructionRegistryRevision Revision() const noexcept {
            return revision_;
        }

        /** @brief Returns copied records in stable handle order. @return Immutable span owned by this snapshot. */
        [[nodiscard]] std::span<const DestructionRegistryRecord> Records() const noexcept;
        /** @brief Resolves one exact handle. @param target Generation-fenced handle.
         * @return Copied record, IdentityUnknown, or StaleGeneration.
         */
        [[nodiscard]] Result<DestructionRegistryRecord> Find(DestructionHandle target) const;
        /** @brief Executes a bounded fixed-work query. @param query Validated world, phase, feature and count filter.
         * @return Owned bounded result or a typed invalid/capacity failure.
         */
        [[nodiscard]] Result<DestructionQueryResult> Query(const DestructionQuery &query) const;
        /** @brief Projects immutable capability evidence for one exact generation. @param target Generation-fenced handle.
         * @return Value snapshot, IdentityUnknown, or StaleGeneration.
         */
        [[nodiscard]] Result<DestructionCapabilitySnapshot> Capabilities(DestructionHandle target) const;

    private:
        friend class DestructionRegistry;
        DestructionRegistryRevision revision_{};
        DestructionRegistryLimits limits_{};
        std::array<DestructionRegistryRecord, DestructionRegistryHardLimits::Entries> records_{};
        std::size_t count_{};
    };

    /**
     * @brief Explicitly owned fixed-capacity index for destruction snapshots and capabilities.
     * @details Registry membership is discovery metadata only. Registering, replacing, removing, or closing an index record
     * never creates, destroys, mutates, retains, or transfers ownership of the represented destructible.
     */
    class DestructionRegistry final {
    public:
        /** @brief Creates an empty explicit registry. @param limits Finite product-selected bounds.
         * @return Registry or a typed invalid-limit failure.
         */
        [[nodiscard]] static Result<DestructionRegistry> Create(const DestructionRegistryLimits &limits = {});
        /** @brief Copies one current owner publication into the index. @param record Immutable projected record.
         * @return Success or typed invalid, duplicate, capacity, or shutdown failure.
         * @pre Called by the single composition owner; concurrent mutation is forbidden.
         */
        [[nodiscard]] Result<void> Register(const DestructionRegistryRecord &record);
        /** @brief Atomically replaces one indexed generation. @param current Exact currently indexed handle.
         * @param replacement Next-generation projected record.
         * @return Success or typed unknown, stale, invalid-generation, or shutdown failure.
         * @pre Called by the single composition owner; concurrent mutation is forbidden.
         */
        [[nodiscard]] Result<void> Replace(DestructionHandle current, const DestructionRegistryRecord &replacement);
        /** @brief Removes discovery metadata for one exact handle. @param target Exact indexed generation.
         * @return True when removed, or typed unknown/stale/shutdown failure.
         * @pre Called by the single composition owner; concurrent mutation is forbidden.
         */
        [[nodiscard]] Result<bool> Remove(DestructionHandle target);
        /** @brief Captures an immutable value snapshot. @return Snapshot or ShutdownInProgress.
         * @pre Called without concurrent registry mutation.
         */
        [[nodiscard]] Result<DestructionRegistrySnapshot> Snapshot() const;
        /** @brief Idempotently closes mutation and new snapshot admission without touching represented destructibles. */
        void BeginShutdown() noexcept;

        /** @brief Reports whether admission is closed. @return True after BeginShutdown. */
        [[nodiscard]] constexpr bool IsShutdown() const noexcept {
            return shutdown_;
        }

        /** @brief Returns the current registry revision. @return Exact non-zero revision. */
        [[nodiscard]] constexpr DestructionRegistryRevision Revision() const noexcept {
            return revision_;
        }

    private:
        explicit DestructionRegistry(DestructionRegistryLimits limits, DestructionRegistryRevision revision) noexcept;
        [[nodiscard]] Result<void> AdvanceRevision();

        DestructionRegistryLimits limits_{};
        std::array<DestructionRegistryRecord, DestructionRegistryHardLimits::Entries> records_{};
        std::size_t count_{};
        DestructionRegistryRevision revision_{};
        bool shutdown_{};
    };
}  // namespace Horo::Destruction
