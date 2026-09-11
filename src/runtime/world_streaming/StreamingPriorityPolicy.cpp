#include "Horo/WorldStreaming/StreamingPriorityPolicy.h"

#include "Horo/WorldStreaming/WorldStreamingErrors.h"

#include <algorithm>
#include <cmath>
#include <tuple>

namespace Horo::WorldStreaming {
    namespace {
        constexpr double MinimumOverride = 0.5;
        constexpr double MaximumOverride = 2.0;
        constexpr double MaximumPolicyFactor = 1'024.0;
        constexpr std::uint64_t MaximumEpsilonMillimeters = 1'000'000'000;
        static_assert(std::tuple_size_v<decltype(StreamingPriorityPolicyRequest::intentMultipliers)> ==
                      static_cast<std::size_t>(StreamingSourceIntent::Count));

        template <typename T> [[nodiscard]] Result<T> Failure(const ErrorCodeDescriptor &descriptor) {
            return Result<T>::Failure(MakeError(descriptor));
        }

        [[nodiscard]] bool IsKnown(const StreamingPriorityPolicyState state) noexcept {
            using enum StreamingPriorityPolicyState;
            return state < Count;
        }

        [[nodiscard]] bool IsFinitePolicyFactor(const double value) noexcept {
            return std::isfinite(value) && value >= 0.0 && value <= MaximumPolicyFactor;
        }

        [[nodiscard]] bool IsValidPolicy(const StreamingPriorityPolicyRequest &request) noexcept {
            return request.id.IsValid() && request.revision.IsValid() && request.epsilonMillimeters > 0 &&
                   request.epsilonMillimeters <= MaximumEpsilonMillimeters && request.maximumCandidates > 0 &&
                   request.maximumCandidates <= StreamingPriorityPolicyRequest::MaximumCandidateCount &&
                   std::ranges::all_of(request.intentMultipliers,
                                       [](const double multiplier) {
                return IsFinitePolicyFactor(multiplier) && multiplier > 0.0;
            }) && IsFinitePolicyFactor(request.ageSlopePerSecond) &&
                   request.ageSlopePerSecond > 0.0 && IsFinitePolicyFactor(request.maximumAgeBoost) && request.maximumAgeBoost > 0.0;
        }

        [[nodiscard]] bool IsValidCandidate(const StreamingCellPriorityCandidate &candidate) {
            return candidate.cell.IsValid() && std::isfinite(candidate.priorityOverride) && candidate.priorityOverride >= MinimumOverride &&
                   candidate.priorityOverride <= MaximumOverride;
        }

        [[nodiscard]] std::size_t IntentIndex(const StreamingSourceIntent intent) noexcept {
            return static_cast<std::size_t>(intent);
        }

        [[nodiscard]] StreamingRankedCellPriority Evaluate(const StreamingPriorityPolicyRequest &policy,
                                                           const StreamingPriorityEvaluationContext &context,
                                                           const StreamingCellPriorityCandidate &candidate) noexcept {
            const std::uint64_t waitedMilliseconds = context.serviceTimeMilliseconds >= candidate.queuedAtServiceMilliseconds
                                                         ? context.serviceTimeMilliseconds - candidate.queuedAtServiceMilliseconds
                                                         : 0;
            const double waitSeconds = static_cast<double>(waitedMilliseconds) / 1'000.0;
            const double ageBoost = std::min(waitSeconds * policy.ageSlopePerSecond, policy.maximumAgeBoost);
            const double denominatorMillimeters =
                static_cast<double>(candidate.distanceMillimeters) + static_cast<double>(policy.epsilonMillimeters);
            const double distanceScore = static_cast<double>(candidate.source.priority.Value()) *
                                         policy.intentMultipliers[IntentIndex(candidate.source.intent)] * candidate.priorityOverride *
                                         1'000.0 / denominatorMillimeters;
            return {.candidate = candidate, .score = distanceScore + ageBoost, .ageBoost = ageBoost};
        }

        [[nodiscard]] bool RankedBefore(const StreamingRankedCellPriority &left, const StreamingRankedCellPriority &right) noexcept {
            if (left.score != right.score)
                return left.score > right.score;
            const StreamingCellCanonicalLess cellLess;
            if (left.candidate.cell != right.candidate.cell)
                return cellLess(left.candidate.cell, right.candidate.cell);
            return std::tuple{left.candidate.source.id.Value(),
                              left.candidate.source.revision.Value(),
                              static_cast<std::uint8_t>(left.candidate.source.intent),
                              left.candidate.source.priority.Value(),
                              left.candidate.priorityOverride,
                              left.candidate.distanceMillimeters,
                              left.candidate.queuedAtServiceMilliseconds} <
                   std::tuple{right.candidate.source.id.Value(),
                              right.candidate.source.revision.Value(),
                              static_cast<std::uint8_t>(right.candidate.source.intent),
                              right.candidate.source.priority.Value(),
                              right.candidate.priorityOverride,
                              right.candidate.distanceMillimeters,
                              right.candidate.queuedAtServiceMilliseconds};
        }
    }  // namespace

    /** @copydoc StreamingPriorityPolicy::Create */
    Result<StreamingPriorityPolicy> StreamingPriorityPolicy::Create(const StreamingPriorityPolicyRequest &request) {
        if (request.contractVersion != StreamingPriorityPolicyRequest::CurrentContractVersion)
            return Failure<StreamingPriorityPolicy>(WorldStreamingErrors::PriorityPolicyUnsupported);
        if (!IsValidPolicy(request))
            return Failure<StreamingPriorityPolicy>(WorldStreamingErrors::PriorityPolicyInvalid);
        return Result<StreamingPriorityPolicy>::Success(StreamingPriorityPolicy{request});
    }

    StreamingPriorityPolicy::StreamingPriorityPolicy(const StreamingPriorityPolicyRequest &request) noexcept : request_(request) {}

    /** @copydoc StreamingPriorityPolicy::Id */
    StreamingPriorityPolicyId StreamingPriorityPolicy::Id() const noexcept {
        return request_.id;
    }

    /** @copydoc StreamingPriorityPolicy::Revision */
    StreamingPriorityPolicyRevision StreamingPriorityPolicy::Revision() const noexcept {
        return request_.revision;
    }

    /** @copydoc StreamingPriorityPolicy::MaximumCandidates */
    std::uint32_t StreamingPriorityPolicy::MaximumCandidates() const noexcept {
        return request_.maximumCandidates;
    }

    /** @copydoc RankStreamingCellPriorities */
    Result<std::size_t> RankStreamingCellPriorities(const StreamingPriorityPolicy &policy,
                                                    const StreamingPriorityEvaluationContext &context,
                                                    const std::span<const StreamingCellPriorityCandidate> candidates,
                                                    const std::span<StreamingRankedCellPriority> output) {
        using enum StreamingPriorityPolicyState;
        if (!context.policy.IsValid() || !context.policyRevision.IsValid() || !context.partition.IsValid() || !context.epoch.IsValid() ||
            !IsKnown(context.state))
            return Failure<std::size_t>(WorldStreamingErrors::PriorityPolicyInvalid);
        if (context.policy != policy.Id() || context.policyRevision != policy.Revision())
            return Failure<std::size_t>(WorldStreamingErrors::PriorityPolicyStale);
        if (context.state == Cancelling || context.state == Closed)
            return Failure<std::size_t>(WorldStreamingErrors::PriorityPolicyLifecycleUnavailable);
        if (candidates.size() > policy.MaximumCandidates() || candidates.size() > output.size())
            return Failure<std::size_t>(WorldStreamingErrors::PriorityPolicyCapacityExceeded);
        for (const auto &candidate : candidates) {
            if (const auto validSource = ValidateStreamingSourceDescriptor(candidate.source); validSource.HasError())
                return Result<std::size_t>::Failure(validSource.ErrorValue());
            if (!IsValidCandidate(candidate))
                return Failure<std::size_t>(WorldStreamingErrors::PriorityPolicyInvalid);
            if (candidate.source.owner.partition != context.partition || candidate.source.owner.epoch != context.epoch)
                return Failure<std::size_t>(WorldStreamingErrors::PriorityPolicyStale);
        }

        const auto ranked = output.first(candidates.size());
        for (std::size_t index = 0; index < candidates.size(); ++index)
            ranked[index] = Evaluate(policy.request_, context, candidates[index]);
        std::ranges::sort(ranked, RankedBefore);
        return Result<std::size_t>::Success(ranked.size());
    }
}  // namespace Horo::WorldStreaming
