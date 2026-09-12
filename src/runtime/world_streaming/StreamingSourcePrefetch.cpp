#include "Horo/WorldStreaming/StreamingSourcePrefetch.h"

#include "Horo/WorldStreaming/WorldStreamingErrors.h"
#include "WorldStreamingInternal.h"

#include <limits>

namespace Horo::WorldStreaming {
    namespace {
        [[nodiscard]] constexpr bool IsKnown(const StreamingPrefetchLifecycle lifecycle) noexcept {
            return lifecycle < StreamingPrefetchLifecycle::Count;
        }

        [[nodiscard]] constexpr bool IsSupportedSource(const StreamingSourceIntent intent) noexcept {
            return intent == StreamingSourceIntent::Camera || intent == StreamingSourceIntent::Gameplay;
        }

        [[nodiscard]] bool PredictAxis(const std::int64_t position, const std::int64_t velocity, const std::uint64_t horizonMilliseconds,
                                       std::int64_t &predicted) noexcept {
            constexpr std::int64_t MillisecondsPerSecond = 1'000;
            const auto horizon = static_cast<std::int64_t>(horizonMilliseconds);
            const std::int64_t wholeSeconds = velocity / MillisecondsPerSecond;
            const std::int64_t remainder = velocity % MillisecondsPerSecond;
            const std::int64_t delta = wholeSeconds * horizon + (remainder * horizon) / MillisecondsPerSecond;
            return Internal::CheckedAdd(position, delta, predicted);
        }

        [[nodiscard]] std::uint64_t SpeedSquared(const StreamingVelocity64 &velocity) noexcept {
            std::uint64_t squared{};
            for (const std::int64_t component : velocity.millimetersPerSecond) {
                const std::uint64_t magnitude = Internal::UnsignedMagnitude(component);
                squared += magnitude * magnitude;
            }
            return squared;
        }

        [[nodiscard]] Result<void> ValidateObservation(const StreamingPrefetchPolicyRequest &request,
                                                       const StreamingPrefetchEvaluationContext &context,
                                                       const StreamingVelocityPrefetchObservation &observation) {
            if (observation.sampledAtServiceMilliseconds > context.serviceTimeMilliseconds)
                return Internal::Failure<void>(WorldStreamingErrors::PrefetchInvalid);
            if (context.serviceTimeMilliseconds - observation.sampledAtServiceMilliseconds > request.maximumSampleAgeMilliseconds)
                return Internal::Failure<void>(WorldStreamingErrors::PrefetchStale);
            for (const std::int64_t component : observation.velocity.millimetersPerSecond) {
                if (Internal::UnsignedMagnitude(component) > request.maximumSpeedMillimetersPerSecond)
                    return Internal::Failure<void>(WorldStreamingErrors::PrefetchInvalid);
            }
            return Result<void>::Success();
        }
    }  // namespace

    /** @copydoc StreamingPrefetchPolicy::Create */
    Result<StreamingPrefetchPolicy> StreamingPrefetchPolicy::Create(const StreamingPrefetchPolicyRequest &request) {
        if (request.contractVersion != StreamingPrefetchPolicyRequest::CurrentContractVersion)
            return Internal::Failure<StreamingPrefetchPolicy>(WorldStreamingErrors::PrefetchUnsupported);
        if (!request.id.IsValid() || !request.revision.IsValid() || request.lookaheadMilliseconds == 0 ||
            request.lookaheadMilliseconds > StreamingPrefetchPolicyRequest::MaximumLookaheadMilliseconds ||
            request.maximumSampleAgeMilliseconds > StreamingPrefetchPolicyRequest::MaximumSampleAgeMilliseconds ||
            request.maximumSpeedMillimetersPerSecond == 0 ||
            request.maximumSpeedMillimetersPerSecond > StreamingPrefetchPolicyRequest::MaximumSpeedMillimetersPerSecond ||
            request.minimumSpeedMillimetersPerSecond > request.maximumSpeedMillimetersPerSecond || request.pathHalfExtentMillimeters <= 0 ||
            request.pathHalfExtentMillimeters > StreamingSourceRangeLimits::MaximumExtentMillimeters)
            return Internal::Failure<StreamingPrefetchPolicy>(WorldStreamingErrors::PrefetchInvalid);
        const std::uint64_t maximumPredictedDistance = (request.maximumSpeedMillimetersPerSecond * request.lookaheadMilliseconds) / 1'000U;
        if (maximumPredictedDistance + static_cast<std::uint64_t>(request.pathHalfExtentMillimeters) >
            static_cast<std::uint64_t>(StreamingSourceRangeLimits::MaximumExtentMillimeters))
            return Internal::Failure<StreamingPrefetchPolicy>(WorldStreamingErrors::PrefetchInvalid);
        return Result<StreamingPrefetchPolicy>::Success(StreamingPrefetchPolicy{request});
    }

    StreamingPrefetchPolicy::StreamingPrefetchPolicy(const StreamingPrefetchPolicyRequest &request) noexcept : request_(request) {}

    /** @copydoc StreamingPrefetchPolicy::Id */
    StreamingPrefetchPolicyId StreamingPrefetchPolicy::Id() const noexcept {
        return request_.id;
    }

    /** @copydoc StreamingPrefetchPolicy::Revision */
    StreamingPrefetchPolicyRevision StreamingPrefetchPolicy::Revision() const noexcept {
        return request_.revision;
    }

    /** @copydoc EvaluateStreamingVelocityPrefetch */
    Result<StreamingPrefetchResult> EvaluateStreamingVelocityPrefetch(const StreamingPrefetchPolicy &policy,
                                                                      const StreamingPrefetchEvaluationContext &context,
                                                                      const StreamingVelocityPrefetchObservation &observation) {
        if (!IsKnown(context.lifecycle))
            return Internal::Failure<StreamingPrefetchResult>(WorldStreamingErrors::PrefetchInvalid);
        if (!context.policy.IsValid() || !context.policyRevision.IsValid() || !context.partition.IsValid() || !context.epoch.IsValid())
            return Internal::Failure<StreamingPrefetchResult>(WorldStreamingErrors::PrefetchInvalid);
        if (const auto validSource = ValidateStreamingSourceDescriptor(observation.source); validSource.HasError())
            return Result<StreamingPrefetchResult>::Failure(validSource.ErrorValue());
        if (context.policy != policy.Id() || context.policyRevision != policy.Revision() ||
            observation.source.owner.partition != context.partition || observation.source.owner.epoch != context.epoch)
            return Internal::Failure<StreamingPrefetchResult>(WorldStreamingErrors::PrefetchStale);
        if (context.lifecycle != StreamingPrefetchLifecycle::Active)
            return Internal::Failure<StreamingPrefetchResult>(WorldStreamingErrors::PrefetchLifecycleUnavailable);
        if (!IsSupportedSource(observation.source.intent))
            return Internal::Failure<StreamingPrefetchResult>(WorldStreamingErrors::PrefetchUnsupported);
        const auto validObservation = ValidateObservation(policy.request_, context, observation);
        if (validObservation.HasError())
            return Result<StreamingPrefetchResult>::Failure(validObservation.ErrorValue());

        const auto admission = ValidateStreamingSourceAdmission(observation.source, context.sourceAdmission);
        if (admission.HasError())
            return Result<StreamingPrefetchResult>::Failure(admission.ErrorValue());

        const std::uint64_t minimumSpeedSquared =
            policy.request_.minimumSpeedMillimetersPerSecond * policy.request_.minimumSpeedMillimetersPerSecond;
        if (SpeedSquared(observation.velocity) < minimumSpeedSquared)
            return Result<StreamingPrefetchResult>::Success(
                {observation.source, admission.Value(), StreamingPrefetchDisposition::Inactive, std::nullopt});

        const std::array position = observation.position.Millimeters();
        std::array<std::int64_t, 3> predicted{};
        for (std::size_t axis = 0; axis < predicted.size(); ++axis) {
            if (!PredictAxis(position[axis], observation.velocity.millimetersPerSecond[axis], policy.request_.lookaheadMilliseconds,
                             predicted[axis]))
                return Internal::Failure<StreamingPrefetchResult>(WorldStreamingErrors::CoordinateOutOfRange);
        }
        if (position == predicted)
            return Result<StreamingPrefetchResult>::Success(
                {observation.source, admission.Value(), StreamingPrefetchDisposition::Inactive, std::nullopt});
        if (!context.shapeSupport.pathVolume)
            return Internal::Failure<StreamingPrefetchResult>(WorldStreamingErrors::SourceShapeUnsupported);

        StreamingSourcePathVolume path{};
        path.points[0] = observation.position;
        path.points[1] = Math::WorldCoordinate64::FromMillimeters(predicted[0], predicted[1], predicted[2]);
        path.pointCount = 2;
        path.halfExtentMillimeters = policy.request_.pathHalfExtentMillimeters;
        if (const auto validPath = ValidateStreamingSourceShape(StreamingSourceShape{path}); validPath.HasError())
            return Result<StreamingPrefetchResult>::Failure(validPath.ErrorValue());
        return Result<StreamingPrefetchResult>::Success({observation.source, admission.Value(), StreamingPrefetchDisposition::Path, path});
    }
}  // namespace Horo::WorldStreaming
