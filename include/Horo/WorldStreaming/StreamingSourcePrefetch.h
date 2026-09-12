#pragma once

/**
 * @file StreamingSourcePrefetch.h
 * @brief Deterministic velocity prediction for bounded world-streaming source paths.
 */

#include "Horo/WorldStreaming/StreamingSourceRange.h"

#include <array>
#include <compare>
#include <cstdint>
#include <optional>

namespace Horo::WorldStreaming {
    namespace Detail {
        struct StreamingPrefetchPolicyIdTag;
        struct StreamingPrefetchPolicyRevisionTag;
    }  // namespace Detail

    /** @brief Stable identity of one host-owned velocity-prefetch policy. */
    using StreamingPrefetchPolicyId =
        Foundation::Detail::NonZeroId64<Detail::StreamingPrefetchPolicyIdTag, WorldStreamingErrors::IdentityInvalid>;
    /** @brief Exact immutable publication revision of one velocity-prefetch policy. */
    using StreamingPrefetchPolicyRevision =
        Foundation::Detail::NonZeroId64<Detail::StreamingPrefetchPolicyRevisionTag, WorldStreamingErrors::IdentityInvalid>;

    /** @brief Lifecycle gate for one immutable prefetch evaluation. */
    enum class StreamingPrefetchLifecycle : std::uint8_t {
        Active,
        Cancelling,
        Closed,
        Count,
    };

    /** @brief Versioned limits for deterministic linear velocity prediction. */
    struct StreamingPrefetchPolicyRequest final {
        static constexpr std::uint32_t CurrentContractVersion = 1;
        static constexpr std::uint64_t MaximumLookaheadMilliseconds = 60'000;
        static constexpr std::uint64_t MaximumSampleAgeMilliseconds = 10'000;
        static constexpr std::uint64_t MaximumSpeedMillimetersPerSecond = 10'000'000;

        std::uint32_t contractVersion{CurrentContractVersion};   /**< Exact supported in-memory contract version. */
        StreamingPrefetchPolicyId id{};                          /**< Stable policy authority identity. */
        StreamingPrefetchPolicyRevision revision{};              /**< Exact immutable policy publication. */
        std::uint64_t lookaheadMilliseconds{1'500};              /**< Positive prediction horizon. */
        std::uint64_t maximumSampleAgeMilliseconds{250};         /**< Maximum admitted unscaled sample age. */
        std::uint64_t minimumSpeedMillimetersPerSecond{1'000};   /**< Inclusive speed threshold for emitting a path. */
        std::uint64_t maximumSpeedMillimetersPerSecond{500'000}; /**< Inclusive per-axis velocity magnitude ceiling. */
        std::int64_t pathHalfExtentMillimeters{2'000};           /**< Positive swept corridor half-extent. */
    };

    /** @brief Immutable validated velocity-prefetch policy with no registration side effects. */
    class StreamingPrefetchPolicy final {
    public:
        /**
         * @brief Validates and captures one complete velocity-prefetch policy.
         * @param request Versioned bounded policy request.
         * @return Immutable policy or PrefetchInvalid/PrefetchUnsupported.
         */
        [[nodiscard]] static Result<StreamingPrefetchPolicy> Create(const StreamingPrefetchPolicyRequest &request);

        /** @brief Returns the stable policy authority. @return Non-zero policy identity. */
        [[nodiscard]] StreamingPrefetchPolicyId Id() const noexcept;
        /** @brief Returns the exact policy publication. @return Non-zero immutable revision. */
        [[nodiscard]] StreamingPrefetchPolicyRevision Revision() const noexcept;

    private:
        explicit StreamingPrefetchPolicy(const StreamingPrefetchPolicyRequest &request) noexcept;

        friend Result<struct StreamingPrefetchResult> EvaluateStreamingVelocityPrefetch(
            const StreamingPrefetchPolicy &, const struct StreamingPrefetchEvaluationContext &,
            const struct StreamingVelocityPrefetchObservation &);

        StreamingPrefetchPolicyRequest request_{};
    };

    /** @brief Exact signed canonical velocity in millimeters per second. */
    struct StreamingVelocity64 final {
        std::array<std::int64_t, 3> millimetersPerSecond{}; /**< X/Y/Z canonical velocity components. */

        [[nodiscard]] constexpr auto operator<=>(const StreamingVelocity64 &) const noexcept = default;
    };

    /** @brief Immutable source and kinematic sample captured by the source owner. */
    struct StreamingVelocityPrefetchObservation final {
        StreamingSourceDescriptor source{};           /**< Camera/gameplay source and exact owner/revision. */
        Math::WorldCoordinate64 position{};           /**< Exact canonical position at sample time. */
        StreamingVelocity64 velocity{};               /**< Exact canonical velocity at sample time. */
        std::uint64_t sampledAtServiceMilliseconds{}; /**< Unscaled monotonic sample timestamp. */

        [[nodiscard]] constexpr auto operator<=>(const StreamingVelocityPrefetchObservation &) const noexcept = default;
    };

    /** @brief Immutable policy, partition, source-admission and lifecycle evidence for one evaluation. */
    struct StreamingPrefetchEvaluationContext final {
        StreamingPrefetchPolicyId policy{};                /**< Expected stable policy authority. */
        StreamingPrefetchPolicyRevision policyRevision{};  /**< Expected exact policy publication. */
        WorldPartitionId partition{};                      /**< Stable partition identity. */
        PartitionEpoch epoch{};                            /**< Exact mounted partition incarnation. */
        std::uint64_t serviceTimeMilliseconds{};           /**< Current unscaled monotonic service time. */
        StreamingSourceAdmissionContext sourceAdmission{}; /**< Current immutable source admission snapshot. */
        StreamingSourceShapeSupport shapeSupport{};        /**< Explicit host shape capabilities; path volume is required. */
        StreamingPrefetchLifecycle lifecycle{StreamingPrefetchLifecycle::Closed}; /**< Current evaluator lifecycle gate. */
    };

    /** @brief Whether an accepted sample emits forward-looking demand. */
    enum class StreamingPrefetchDisposition : std::uint8_t {
        Inactive,
        Path,
    };

    /** @brief Value-owned deterministic output for one accepted velocity sample. */
    struct StreamingPrefetchResult final {
        StreamingSourceDescriptor source{};              /**< Exact source revision admitted by the evaluation. */
        StreamingSourceAdmissionKind admission{};        /**< Insert or replacement authorized by the source snapshot. */
        StreamingPrefetchDisposition disposition{};      /**< Whether a path was emitted. */
        std::optional<StreamingSourcePathVolume> path{}; /**< Bounded two-point corridor when disposition is Path. */

        [[nodiscard]] constexpr auto operator<=>(const StreamingPrefetchResult &) const noexcept = default;
    };

    /**
     * @brief Produces deterministic bounded forward-looking demand from one camera/gameplay velocity sample.
     * @param policy Immutable velocity-prefetch policy publication.
     * @param context Exact policy, partition, source-admission, service-time and lifecycle evidence.
     * @param observation Value-owned source and canonical kinematic sample.
     * @return Inactive or bounded path output, or a typed invalid, unsupported, stale, capacity, range or lifecycle failure.
     * @details Evaluation is allocation-free, non-blocking and side-effect free. It never registers a source, mutates admission state,
     *          grants gameplay authority, bypasses capacity, or silently substitutes another shape or policy.
     */
    [[nodiscard]] Result<StreamingPrefetchResult> EvaluateStreamingVelocityPrefetch(
        const StreamingPrefetchPolicy &policy, const StreamingPrefetchEvaluationContext &context,
        const StreamingVelocityPrefetchObservation &observation);
}  // namespace Horo::WorldStreaming
