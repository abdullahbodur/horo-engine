#include "Horo/WorldStreaming/StreamingBudgetModel.h"

#include "WorldStreamingInternal.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace Horo::WorldStreaming {
    namespace {
        [[nodiscard]] constexpr bool IsKnownDimension(const StreamingBudgetDimension dimension) noexcept {
            return dimension >= StreamingBudgetDimension::CpuResidentBytes && dimension < StreamingBudgetDimension::Count;
        }

        [[nodiscard]] constexpr std::size_t Index(const StreamingBudgetDimension dimension) noexcept {
            return static_cast<std::size_t>(dimension);
        }

        [[nodiscard]] bool IsValidTime(const std::chrono::nanoseconds time) noexcept {
            return time.count() >= 0;
        }

        [[nodiscard]] bool TryWindowEnd(const std::chrono::nanoseconds start, const std::chrono::nanoseconds duration,
                                        std::chrono::nanoseconds &end) noexcept {
            if (!IsValidTime(start) || duration.count() <= 0 || start.count() > std::chrono::nanoseconds::max().count() - duration.count())
                return false;
            end = start + duration;
            return true;
        }

        [[nodiscard]] bool TryAdd(const std::uint64_t left, const std::uint64_t right, std::uint64_t &sum) noexcept {
            if (left > std::numeric_limits<std::uint64_t>::max() - right)
                return false;
            sum = left + right;
            return true;
        }

        [[nodiscard]] bool IsMalformedEvaluation(const StreamingBudgetSample &sample, const StreamingBudgetAmounts &request,
                                                 const StreamingBudgetEvaluationContext &context) noexcept {
            return !context.expectedPolicyRevision.IsValid() || !context.expectedSampleRevision.IsValid() ||
                   !IsValidTime(context.evaluationTime) || context.evaluationTime < sample.ObservedAt() || request.IsZero();
        }

        [[nodiscard]] bool HasStaleRevision(const StreamingBudgetPolicy &policy, const StreamingBudgetSample &sample,
                                            const StreamingBudgetEvaluationContext &context) noexcept {
            return context.expectedPolicyRevision != policy.Revision() || sample.PolicyRevision() != policy.Revision() ||
                   context.expectedSampleRevision != sample.Revision();
        }

        [[nodiscard]] Result<void> ValidateEvaluationInputs(const StreamingBudgetPolicy &policy, const StreamingBudgetSample &sample,
                                                            const StreamingBudgetAmounts &request,
                                                            const StreamingBudgetEvaluationContext &context) {
            if (IsMalformedEvaluation(sample, request, context))
                return Internal::Failure<void>(WorldStreamingErrors::BudgetModelInvalid);
            if (HasStaleRevision(policy, sample, context))
                return Internal::Failure<void>(WorldStreamingErrors::BudgetRevisionStale);

            std::chrono::nanoseconds windowEnd{};
            if (!TryWindowEnd(sample.WindowStart(), policy.SamplingWindow(), windowEnd))
                return Internal::Failure<void>(WorldStreamingErrors::BudgetSampleInvalid);
            if (context.evaluationTime >= windowEnd)
                return Internal::Failure<void>(WorldStreamingErrors::BudgetSampleStale);
            return Result<void>::Success();
        }
    }  // namespace

    /** @copydoc StreamingBudgetAmounts::Create */
    Result<StreamingBudgetAmounts> StreamingBudgetAmounts::Create(const std::span<const StreamingBudgetAmount> amounts) {
        if (amounts.size() != StreamingBudgetDimensionCount)
            return Internal::Failure<StreamingBudgetAmounts>(WorldStreamingErrors::BudgetModelInvalid);

        std::array<std::uint64_t, StreamingBudgetDimensionCount> values{};
        std::array<bool, StreamingBudgetDimensionCount> present{};
        for (const auto &amount : amounts) {
            if (!IsKnownDimension(amount.dimension))
                return Internal::Failure<StreamingBudgetAmounts>(WorldStreamingErrors::BudgetDimensionUnsupported);
            const auto index = Index(amount.dimension);
            if (present[index])
                return Internal::Failure<StreamingBudgetAmounts>(WorldStreamingErrors::BudgetModelInvalid);
            present[index] = true;
            values[index] = amount.value;
        }
        if (std::ranges::find(present, false) != present.end())
            return Internal::Failure<StreamingBudgetAmounts>(WorldStreamingErrors::BudgetModelInvalid);
        return Result<StreamingBudgetAmounts>::Success(StreamingBudgetAmounts{values});
    }

    /** @copydoc StreamingBudgetAmounts::Value */
    Result<std::uint64_t> StreamingBudgetAmounts::Value(const StreamingBudgetDimension dimension) const {
        if (!IsKnownDimension(dimension))
            return Internal::Failure<std::uint64_t>(WorldStreamingErrors::BudgetDimensionUnsupported);
        return Result<std::uint64_t>::Success(values_[Index(dimension)]);
    }

    /** @copydoc StreamingBudgetAmounts::Entries */
    std::array<StreamingBudgetAmount, StreamingBudgetDimensionCount> StreamingBudgetAmounts::Entries() const noexcept {
        std::array<StreamingBudgetAmount, StreamingBudgetDimensionCount> entries{};
        for (std::size_t index = 0; index < entries.size(); ++index)
            entries[index] = {.dimension = static_cast<StreamingBudgetDimension>(index), .value = values_[index]};
        return entries;
    }

    /** @copydoc StreamingBudgetAmounts::IsZero */
    bool StreamingBudgetAmounts::IsZero() const noexcept {
        return std::ranges::all_of(values_, [](const std::uint64_t value) {
            return value == 0;
        });
    }

    /** @copydoc StreamingBudgetPolicy::Create */
    Result<StreamingBudgetPolicy> StreamingBudgetPolicy::Create(const StreamingBudgetPolicyRevision revision,
                                                                const std::span<const StreamingBudgetLimit> limits,
                                                                const std::chrono::nanoseconds samplingWindow) {
        if (!revision.IsValid() || samplingWindow.count() <= 0 || limits.size() != StreamingBudgetDimensionCount)
            return Internal::Failure<StreamingBudgetPolicy>(WorldStreamingErrors::BudgetModelInvalid);

        std::array<StreamingBudgetLimit, StreamingBudgetDimensionCount> canonical{};
        std::array<bool, StreamingBudgetDimensionCount> present{};
        for (const auto &limit : limits) {
            if (!IsKnownDimension(limit.dimension))
                return Internal::Failure<StreamingBudgetPolicy>(WorldStreamingErrors::BudgetDimensionUnsupported);
            if (limit.hardLimit == 0 || limit.softTarget > limit.hardLimit)
                return Internal::Failure<StreamingBudgetPolicy>(WorldStreamingErrors::BudgetModelInvalid);
            const auto index = Index(limit.dimension);
            if (present[index])
                return Internal::Failure<StreamingBudgetPolicy>(WorldStreamingErrors::BudgetModelInvalid);
            present[index] = true;
            canonical[index] = limit;
        }
        if (std::ranges::find(present, false) != present.end())
            return Internal::Failure<StreamingBudgetPolicy>(WorldStreamingErrors::BudgetModelInvalid);
        return Result<StreamingBudgetPolicy>::Success(StreamingBudgetPolicy{revision, canonical, samplingWindow});
    }

    /** @copydoc StreamingBudgetPolicy::Revision */
    StreamingBudgetPolicyRevision StreamingBudgetPolicy::Revision() const noexcept {
        return revision_;
    }

    /** @copydoc StreamingBudgetPolicy::SamplingWindow */
    std::chrono::nanoseconds StreamingBudgetPolicy::SamplingWindow() const noexcept {
        return samplingWindow_;
    }

    /** @copydoc StreamingBudgetPolicy::Limit */
    Result<StreamingBudgetLimit> StreamingBudgetPolicy::Limit(const StreamingBudgetDimension dimension) const {
        if (!IsKnownDimension(dimension))
            return Internal::Failure<StreamingBudgetLimit>(WorldStreamingErrors::BudgetDimensionUnsupported);
        return Result<StreamingBudgetLimit>::Success(limits_[Index(dimension)]);
    }

    /** @copydoc StreamingBudgetPolicy::Limits */
    std::array<StreamingBudgetLimit, StreamingBudgetDimensionCount> StreamingBudgetPolicy::Limits() const noexcept {
        return limits_;
    }

    StreamingBudgetPolicy::StreamingBudgetPolicy(const StreamingBudgetPolicyRevision revision,
                                                 std::array<StreamingBudgetLimit, StreamingBudgetDimensionCount> limits,
                                                 const std::chrono::nanoseconds samplingWindow) noexcept
        : revision_(revision), limits_(std::move(limits)), samplingWindow_(samplingWindow) {}

    /** @copydoc StreamingBudgetSample::Create */
    Result<StreamingBudgetSample> StreamingBudgetSample::Create(const StreamingBudgetPolicy &policy,
                                                                const StreamingBudgetSampleRevision revision,
                                                                const std::chrono::nanoseconds windowStart,
                                                                const std::chrono::nanoseconds observedAt, StreamingBudgetAmounts usage) {
        if (std::chrono::nanoseconds windowEnd{}; !revision.IsValid() || !TryWindowEnd(windowStart, policy.SamplingWindow(), windowEnd) ||
                                                  observedAt < windowStart || observedAt >= windowEnd)
            return Internal::Failure<StreamingBudgetSample>(WorldStreamingErrors::BudgetSampleInvalid);
        return Result<StreamingBudgetSample>::Success(
            StreamingBudgetSample{policy.Revision(), revision, windowStart, observedAt, std::move(usage)});
    }

    /** @copydoc StreamingBudgetSample::PolicyRevision */
    StreamingBudgetPolicyRevision StreamingBudgetSample::PolicyRevision() const noexcept {
        return policyRevision_;
    }

    /** @copydoc StreamingBudgetSample::Revision */
    StreamingBudgetSampleRevision StreamingBudgetSample::Revision() const noexcept {
        return revision_;
    }

    /** @copydoc StreamingBudgetSample::WindowStart */
    std::chrono::nanoseconds StreamingBudgetSample::WindowStart() const noexcept {
        return windowStart_;
    }

    /** @copydoc StreamingBudgetSample::ObservedAt */
    std::chrono::nanoseconds StreamingBudgetSample::ObservedAt() const noexcept {
        return observedAt_;
    }

    /** @copydoc StreamingBudgetSample::Usage */
    const StreamingBudgetAmounts &StreamingBudgetSample::Usage() const noexcept {
        return usage_;
    }

    StreamingBudgetSample::StreamingBudgetSample(const StreamingBudgetPolicyRevision policyRevision,
                                                 const StreamingBudgetSampleRevision revision, const std::chrono::nanoseconds windowStart,
                                                 const std::chrono::nanoseconds observedAt, StreamingBudgetAmounts usage) noexcept
        : policyRevision_(policyRevision), revision_(revision), windowStart_(windowStart), observedAt_(observedAt),
          usage_(std::move(usage)) {}

    /** @copydoc StreamingBudgetEvaluation::Decision */
    StreamingBudgetDecision StreamingBudgetEvaluation::Decision() const noexcept {
        return decision_;
    }

    /** @copydoc StreamingBudgetEvaluation::ProjectedUsage */
    const StreamingBudgetAmounts &StreamingBudgetEvaluation::ProjectedUsage() const noexcept {
        return projectedUsage_;
    }

    /** @copydoc StreamingBudgetEvaluation::PressureDimension */
    std::optional<StreamingBudgetDimension> StreamingBudgetEvaluation::PressureDimension() const noexcept {
        return pressureDimension_;
    }

    StreamingBudgetEvaluation::StreamingBudgetEvaluation(const StreamingBudgetDecision decision, StreamingBudgetAmounts projectedUsage,
                                                         const std::optional<StreamingBudgetDimension> pressureDimension) noexcept
        : decision_(decision), projectedUsage_(std::move(projectedUsage)), pressureDimension_(pressureDimension) {}

    /** @copydoc EvaluateStreamingBudget */
    Result<StreamingBudgetEvaluation> EvaluateStreamingBudget(const StreamingBudgetPolicy &policy, const StreamingBudgetSample &sample,
                                                              const StreamingBudgetAmounts &request,
                                                              const StreamingBudgetEvaluationContext &context) {
        if (const auto validation = ValidateEvaluationInputs(policy, sample, request, context); validation.HasError())
            return Result<StreamingBudgetEvaluation>::Failure(validation.ErrorValue());

        std::array<std::uint64_t, StreamingBudgetDimensionCount> projected{};
        std::optional<StreamingBudgetDimension> pressureDimension;
        for (std::size_t index = 0; index < projected.size(); ++index) {
            const auto dimension = static_cast<StreamingBudgetDimension>(index);
            const auto limit = policy.limits_[index];
            if (!TryAdd(sample.Usage().values_[index], request.values_[index], projected[index]) || projected[index] > limit.hardLimit)
                return Internal::Failure<StreamingBudgetEvaluation>(WorldStreamingErrors::BudgetCapacityExceeded);
            if (!pressureDimension.has_value() && projected[index] > limit.softTarget)
                pressureDimension = dimension;
        }

        const auto decision =
            pressureDimension.has_value() ? StreamingBudgetDecision::DeferAboveSoftTarget : StreamingBudgetDecision::Admit;
        return Result<StreamingBudgetEvaluation>::Success(
            StreamingBudgetEvaluation{decision, StreamingBudgetAmounts{projected}, pressureDimension});
    }
}  // namespace Horo::WorldStreaming
