#pragma once

/**
 * @file StreamingBudgetModel.h
 * @brief Immutable multidimensional world-streaming budget policy and sampling contract.
 */

#include "Horo/Foundation/StrongId.h"
#include "Horo/WorldStreaming/WorldStreamingErrors.h"

#include <array>
#include <chrono>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace Horo::WorldStreaming {
    namespace Detail {
        /** @brief Tag that keeps budget-policy revisions distinct from other world-streaming revisions. */
        struct StreamingBudgetPolicyRevisionTag;
        /** @brief Tag that keeps budget-sample revisions distinct from other world-streaming revisions. */
        struct StreamingBudgetSampleRevisionTag;
    }  // namespace Detail

    /** @brief Monotonic identity for one immutable budget policy revision. */
    using StreamingBudgetPolicyRevision =
        Foundation::Detail::NonZeroId64<Detail::StreamingBudgetPolicyRevisionTag, WorldStreamingErrors::IdentityInvalid>;

    /** @brief Monotonic identity for one immutable usage sample revision. */
    using StreamingBudgetSampleRevision =
        Foundation::Detail::NonZeroId64<Detail::StreamingBudgetSampleRevisionTag, WorldStreamingErrors::IdentityInvalid>;

    /** @brief Independent resource axes that must never be collapsed into one additive scalar. */
    enum class StreamingBudgetDimension : std::uint8_t {
        CpuResidentBytes,
        GpuResidentBytes,
        StagingBytes,
        IoBytesInFlight,
        QueueScratchBytes,
        RetiredBytes,
        OwnerWorkNanoseconds,
        Count,
    };

    /** @brief Number of supported resource dimensions, excluding the Count sentinel. */
    inline constexpr std::size_t StreamingBudgetDimensionCount = static_cast<std::size_t>(StreamingBudgetDimension::Count);

    class StreamingBudgetAmounts;
    class StreamingBudgetPolicy;
    class StreamingBudgetSample;
    class StreamingBudgetEvaluation;
    struct StreamingBudgetEvaluationContext;

    [[nodiscard]] Result<StreamingBudgetEvaluation> EvaluateStreamingBudget(const StreamingBudgetPolicy &policy,
                                                                            const StreamingBudgetSample &sample,
                                                                            const StreamingBudgetAmounts &request,
                                                                            const StreamingBudgetEvaluationContext &context);

    /** @brief One explicitly known amount on a typed resource axis. */
    struct StreamingBudgetAmount final {
        StreamingBudgetDimension dimension{}; /**< Axis whose unit is declared by StreamingBudgetDimension. */
        std::uint64_t value{};                /**< Bytes for byte axes; nanoseconds for OwnerWorkNanoseconds. */

        [[nodiscard]] constexpr auto operator<=>(const StreamingBudgetAmount &) const noexcept = default;
    };

    /** @brief Complete immutable amount vector with one explicit entry for every supported dimension. */
    class StreamingBudgetAmounts final {
    public:
        /**
         * @brief Canonicalizes one complete resource vector.
         * @param amounts Exactly one entry for every supported dimension, in any order.
         * @return Owned vector or a typed invalid/unsupported error; omitted dimensions are never inferred as zero.
         */
        [[nodiscard]] static Result<StreamingBudgetAmounts> Create(std::span<const StreamingBudgetAmount> amounts);

        /** @brief Reads one supported axis. @param dimension Resource axis. @return Exact value or unsupported-dimension error. */
        [[nodiscard]] Result<std::uint64_t> Value(StreamingBudgetDimension dimension) const;
        /** @brief Returns entries in canonical enum order. @return Owned fixed-size projection. */
        [[nodiscard]] std::array<StreamingBudgetAmount, StreamingBudgetDimensionCount> Entries() const noexcept;
        /** @brief Reports whether every explicitly known amount is zero. @return True only for an all-zero vector. */
        [[nodiscard]] bool IsZero() const noexcept;

        [[nodiscard]] constexpr auto operator<=>(const StreamingBudgetAmounts &) const noexcept = default;

    private:
        explicit constexpr StreamingBudgetAmounts(const std::array<std::uint64_t, StreamingBudgetDimensionCount> values) noexcept
            : values_(values) {}

        std::array<std::uint64_t, StreamingBudgetDimensionCount> values_{};

        friend Result<StreamingBudgetEvaluation> EvaluateStreamingBudget(const StreamingBudgetPolicy &, const StreamingBudgetSample &,
                                                                         const StreamingBudgetAmounts &,
                                                                         const StreamingBudgetEvaluationContext &);
    };

    /** @brief Soft target and hard admission limit for one independent resource axis. */
    struct StreamingBudgetLimit final {
        StreamingBudgetDimension dimension{}; /**< Axis governed by this limit. */
        std::uint64_t softTarget{};           /**< Pressure threshold; equality remains within target. */
        std::uint64_t hardLimit{};            /**< Absolute admission ceiling; must be positive and at least softTarget. */

        [[nodiscard]] constexpr auto operator<=>(const StreamingBudgetLimit &) const noexcept = default;
    };

    /** @brief Validated immutable policy revision for all independent resource axes and one sampling window. */
    class StreamingBudgetPolicy final {
    public:
        /**
         * @brief Creates a complete budget policy.
         * @param revision Non-zero monotonic policy revision.
         * @param limits Exactly one valid limit for every supported dimension, in any order.
         * @param samplingWindow Positive deterministic service-time window.
         * @return Owned policy or a typed invalid/unsupported error.
         */
        [[nodiscard]] static Result<StreamingBudgetPolicy> Create(StreamingBudgetPolicyRevision revision,
                                                                  std::span<const StreamingBudgetLimit> limits,
                                                                  std::chrono::nanoseconds samplingWindow);

        /** @brief Returns the exact immutable policy revision. @return Non-zero revision. */
        [[nodiscard]] StreamingBudgetPolicyRevision Revision() const noexcept;
        /** @brief Returns the deterministic sampling-window duration. @return Positive duration. */
        [[nodiscard]] std::chrono::nanoseconds SamplingWindow() const noexcept;
        /** @brief Reads one supported axis limit. @param dimension Resource axis. @return Limit or unsupported-dimension error. */
        [[nodiscard]] Result<StreamingBudgetLimit> Limit(StreamingBudgetDimension dimension) const;
        /** @brief Returns limits in canonical enum order. @return Owned fixed-size projection. */
        [[nodiscard]] std::array<StreamingBudgetLimit, StreamingBudgetDimensionCount> Limits() const noexcept;

    private:
        StreamingBudgetPolicy(StreamingBudgetPolicyRevision revision,
                              std::array<StreamingBudgetLimit, StreamingBudgetDimensionCount> limits,
                              std::chrono::nanoseconds samplingWindow) noexcept;

        StreamingBudgetPolicyRevision revision_{};
        std::array<StreamingBudgetLimit, StreamingBudgetDimensionCount> limits_{};
        std::chrono::nanoseconds samplingWindow_{};

        friend Result<StreamingBudgetEvaluation> EvaluateStreamingBudget(const StreamingBudgetPolicy &, const StreamingBudgetSample &,
                                                                         const StreamingBudgetAmounts &,
                                                                         const StreamingBudgetEvaluationContext &);
    };

    /** @brief Immutable measured usage bound to one policy revision and deterministic service-time window. */
    class StreamingBudgetSample final {
    public:
        /**
         * @brief Creates a usage observation inside one policy window.
         * @param policy Policy revision and window definition used for sampling.
         * @param revision Non-zero monotonic sample revision.
         * @param windowStart Non-negative monotonic service time at the inclusive window start.
         * @param observedAt Monotonic service time inside the half-open policy window.
         * @param usage Complete explicitly known measured usage.
         * @return Owned sample or a typed invalid error.
         */
        [[nodiscard]] static Result<StreamingBudgetSample> Create(const StreamingBudgetPolicy &policy,
                                                                  StreamingBudgetSampleRevision revision,
                                                                  std::chrono::nanoseconds windowStart, std::chrono::nanoseconds observedAt,
                                                                  StreamingBudgetAmounts usage);

        /** @brief Returns the policy revision used by this sample. @return Non-zero revision. */
        [[nodiscard]] StreamingBudgetPolicyRevision PolicyRevision() const noexcept;
        /** @brief Returns this sample's monotonic revision. @return Non-zero revision. */
        [[nodiscard]] StreamingBudgetSampleRevision Revision() const noexcept;
        /** @brief Returns the inclusive service-time window start. @return Non-negative duration. */
        [[nodiscard]] std::chrono::nanoseconds WindowStart() const noexcept;
        /** @brief Returns the observation service time. @return Time inside the sample window. */
        [[nodiscard]] std::chrono::nanoseconds ObservedAt() const noexcept;
        /** @brief Returns complete measured usage. @return Immutable owned amount vector. */
        [[nodiscard]] const StreamingBudgetAmounts &Usage() const noexcept;

    private:
        StreamingBudgetSample(StreamingBudgetPolicyRevision policyRevision, StreamingBudgetSampleRevision revision,
                              std::chrono::nanoseconds windowStart, std::chrono::nanoseconds observedAt,
                              StreamingBudgetAmounts usage) noexcept;

        StreamingBudgetPolicyRevision policyRevision_{};
        StreamingBudgetSampleRevision revision_{};
        std::chrono::nanoseconds windowStart_{};
        std::chrono::nanoseconds observedAt_{};
        StreamingBudgetAmounts usage_;
    };

    /** @brief Caller-owned freshness fence for one deterministic budget evaluation. */
    struct StreamingBudgetEvaluationContext final {
        StreamingBudgetPolicyRevision expectedPolicyRevision; /**< Authority's current policy revision. */
        StreamingBudgetSampleRevision expectedSampleRevision; /**< Authority's current sample revision. */
        std::chrono::nanoseconds evaluationTime{};            /**< Non-negative monotonic service time. */
    };

    /** @brief Non-failing pressure decision below hard limits. */
    enum class StreamingBudgetDecision : std::uint8_t {
        Admit,
        DeferAboveSoftTarget,
    };

    /** @brief Successful immutable projection of one budget request. */
    class StreamingBudgetEvaluation final {
    public:
        /** @brief Returns whether work may start now or should defer under soft pressure. @return Typed decision. */
        [[nodiscard]] StreamingBudgetDecision Decision() const noexcept;
        /** @brief Returns usage after the request without mutating the source sample. @return Checked projected vector. */
        [[nodiscard]] const StreamingBudgetAmounts &ProjectedUsage() const noexcept;
        /** @brief Returns the first canonical soft-pressure axis when deferred. @return Empty for Admit. */
        [[nodiscard]] std::optional<StreamingBudgetDimension> PressureDimension() const noexcept;

    private:
        StreamingBudgetEvaluation(StreamingBudgetDecision decision, StreamingBudgetAmounts projectedUsage,
                                  std::optional<StreamingBudgetDimension> pressureDimension) noexcept;

        StreamingBudgetDecision decision_{StreamingBudgetDecision::Admit};
        StreamingBudgetAmounts projectedUsage_;
        std::optional<StreamingBudgetDimension> pressureDimension_;

        friend Result<StreamingBudgetEvaluation> EvaluateStreamingBudget(const StreamingBudgetPolicy &, const StreamingBudgetSample &,
                                                                         const StreamingBudgetAmounts &,
                                                                         const StreamingBudgetEvaluationContext &);
    };

    /**
     * @brief Evaluates a positive request against one exact current policy/sample snapshot without mutation.
     * @param policy Current immutable policy.
     * @param sample Current immutable measured usage.
     * @param request Complete resource requirement; at least one dimension must be positive.
     * @param context Expected revisions and current monotonic service time.
     * @return Admit/defer projection, or a typed invalid, stale, arithmetic, or hard-cap failure.
     * @details Dimensions are checked independently and never summed. This inert model owns no reservation or lifecycle;
     *          the authority composes a successful decision with scheduler admission and later retirement accounting.
     */
    [[nodiscard]] Result<StreamingBudgetEvaluation> EvaluateStreamingBudget(const StreamingBudgetPolicy &policy,
                                                                            const StreamingBudgetSample &sample,
                                                                            const StreamingBudgetAmounts &request,
                                                                            const StreamingBudgetEvaluationContext &context);
}  // namespace Horo::WorldStreaming
