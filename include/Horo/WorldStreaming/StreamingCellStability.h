#pragma once

/**
 * @file StreamingCellStability.h
 * @brief Deterministic per-cell hysteresis, linger, and boundary-thrash policy.
 */

#include "Horo/WorldStreaming/StreamingDesiredState.h"

#include <compare>
#include <cstdint>
#include <optional>

namespace Horo::WorldStreaming {
    namespace Detail {
        struct StreamingCellStabilityPolicyIdTag;
        struct StreamingCellStabilityPolicyRevisionTag;
    }  // namespace Detail

    /** @brief Stable identity of one host-owned cell-stability policy. */
    using StreamingCellStabilityPolicyId =
        Foundation::Detail::NonZeroId64<Detail::StreamingCellStabilityPolicyIdTag, WorldStreamingErrors::IdentityInvalid>;
    /** @brief Exact immutable publication revision of one cell-stability policy. */
    using StreamingCellStabilityPolicyRevision =
        Foundation::Detail::NonZeroId64<Detail::StreamingCellStabilityPolicyRevisionTag, WorldStreamingErrors::IdentityInvalid>;

    /** @brief Lifecycle admission state supplied by the owning streaming authority. */
    enum class StreamingCellStabilityLifecycle : std::uint8_t {
        Active,
        Cancelling,
        Closed,
        Count,
    };

    /** @brief Versioned bounded hysteresis and linger policy. */
    struct StreamingCellStabilityPolicyRequest final {
        static constexpr std::uint32_t CurrentContractVersion = 1;
        static constexpr std::int64_t MaximumMarginMillimeters = 1'000'000'000;
        static constexpr std::uint64_t MaximumLingerMilliseconds = 600'000;
        static constexpr std::uint32_t MaximumTrackedCellCount = 1'048'576;

        std::uint32_t contractVersion{CurrentContractVersion}; /**< Exact supported in-memory version. */
        StreamingCellStabilityPolicyId id{};                   /**< Stable policy authority identity. */
        StreamingCellStabilityPolicyRevision revision{};       /**< Exact immutable policy publication. */
        std::int64_t enterMarginMillimeters{};                 /**< Required depth inside a source boundary before admission. */
        std::int64_t exitMarginMillimeters{};                  /**< Allowed distance outside a boundary before linger begins. */
        std::uint64_t lingerMilliseconds{5'000};               /**< Unscaled no-demand retention interval. */
        std::uint32_t maximumTrackedCells{1'024};              /**< Authority-owned record ceiling. */
    };

    /** @brief Immutable validated cell-stability policy with no ambient registration effects. */
    class StreamingCellStabilityPolicy final {
    public:
        /** @brief Validates and captures one complete policy. @param request Policy facts. @return Policy or a typed failure. */
        [[nodiscard]] static Result<StreamingCellStabilityPolicy> Create(const StreamingCellStabilityPolicyRequest &request);

        /** @brief Returns the stable policy authority. @return Non-zero identity. */
        [[nodiscard]] StreamingCellStabilityPolicyId Id() const noexcept;
        /** @brief Returns the exact immutable policy publication. @return Non-zero revision. */
        [[nodiscard]] StreamingCellStabilityPolicyRevision Revision() const noexcept;
        /** @brief Returns the admitted tracked-cell ceiling. @return Positive bounded count. */
        [[nodiscard]] std::uint32_t MaximumTrackedCells() const noexcept;
        /** @brief Returns the inclusive admission margin. @return Non-negative millimeters inside the boundary. */
        [[nodiscard]] std::int64_t EnterMarginMillimeters() const noexcept;
        /** @brief Returns the inclusive retention margin. @return Non-negative millimeters outside the boundary. */
        [[nodiscard]] std::int64_t ExitMarginMillimeters() const noexcept;
        /** @brief Returns the no-demand retention interval. @return Bounded unscaled milliseconds. */
        [[nodiscard]] std::uint64_t LingerMilliseconds() const noexcept;

    private:
        explicit StreamingCellStabilityPolicy(const StreamingCellStabilityPolicyRequest &request) noexcept;

        StreamingCellStabilityPolicyRequest request_{};
    };

    /** @brief Stable phase of one authority-owned per-cell anti-thrash record. */
    enum class StreamingCellStabilityPhase : std::uint8_t {
        Unloaded,
        Resident,
        Lingering,
        Count,
    };

    /** @brief Exact policy, partition, time, capacity, and lifecycle evidence for one transition. */
    struct StreamingCellStabilityContext final {
        StreamingCellStabilityPolicyId policy{};               /**< Expected stable policy identity. */
        StreamingCellStabilityPolicyRevision policyRevision{}; /**< Expected exact publication. */
        WorldPartitionId partition{};                          /**< Stable partition authority. */
        PartitionEpoch epoch{};                                /**< Exact mounted partition incarnation. */
        std::uint64_t serviceTimeMilliseconds{};               /**< Unscaled monotonic observation time. */
        std::uint32_t trackedCells{};                          /**< Records currently owned by the authority. */
        StreamingCellStabilityLifecycle lifecycle{StreamingCellStabilityLifecycle::Closed}; /**< Admission gate. */
    };

    /** @brief Reduced demand and signed boundary evidence for one canonical cell. */
    struct StreamingCellStabilityObservation final {
        StreamingCellId cell{};                                                            /**< Stable cell tuple. */
        StreamingDesiredResidency effectiveResidency{StreamingDesiredResidency::Unloaded}; /**< Reduced desired residency. */
        std::optional<StreamingDesiredResidency> pinnedResidencyFloor{};                   /**< Explicit pin floor, if any. */
        std::int64_t signedBoundaryDistanceMillimeters{}; /**< Positive inside, negative outside the strongest source boundary. */
    };

    /** @brief Immutable previous or next anti-thrash state for one exact policy and partition generation. */
    struct StreamingCellStabilitySnapshot final {
        StreamingCellStabilityPolicyId policy{};                                          /**< Stable policy authority. */
        StreamingCellStabilityPolicyRevision policyRevision{};                            /**< Exact policy publication. */
        WorldPartitionId partition{};                                                     /**< Stable partition authority. */
        PartitionEpoch epoch{};                                                           /**< Exact mounted partition incarnation. */
        StreamingCellId cell{};                                                           /**< Canonical cell tuple. */
        StreamingCellStabilityPhase phase{StreamingCellStabilityPhase::Unloaded};         /**< Current anti-thrash phase. */
        StreamingDesiredResidency retainedResidency{StreamingDesiredResidency::Unloaded}; /**< Held desired residency. */
        std::uint64_t observedAtServiceMilliseconds{};                                    /**< Last unscaled monotonic observation time. */
        std::uint64_t lingerStartedAtServiceMilliseconds{};                               /**< Linger origin, or zero outside Lingering. */

        [[nodiscard]] constexpr auto operator<=>(const StreamingCellStabilitySnapshot &) const noexcept = default;
    };

    /** @brief Typed next-state decision; Unloaded tells the authority to release its bounded record. */
    struct StreamingCellStabilityDecision final {
        StreamingCellStabilitySnapshot snapshot{}; /**< Complete immutable next state. */
        bool boundaryHeld{};                       /**< True when hysteresis retained demand outside the enter threshold. */
        bool lingerExpired{};                      /**< True only on the transition that releases an expired record. */
    };

    /**
     * @brief Evaluates one pure per-cell hysteresis and linger transition without mutating authority storage.
     * @param policy Immutable policy publication.
     * @param context Exact authority, time, record-count, and lifecycle facts.
     * @param observation Reduced demand and signed source-boundary evidence.
     * @param previous Optional prior state owned by the calling authority.
     * @return Complete next-state decision or a typed invalid, unsupported, stale, capacity, or lifecycle failure.
     */
    [[nodiscard]] Result<StreamingCellStabilityDecision> EvaluateStreamingCellStability(
        const StreamingCellStabilityPolicy &policy, const StreamingCellStabilityContext &context,
        const StreamingCellStabilityObservation &observation, const std::optional<StreamingCellStabilitySnapshot> &previous);
}  // namespace Horo::WorldStreaming
