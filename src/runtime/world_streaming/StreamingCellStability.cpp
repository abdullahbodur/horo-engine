#include "Horo/WorldStreaming/StreamingCellStability.h"

#include "Horo/WorldStreaming/WorldStreamingErrors.h"

namespace Horo::WorldStreaming {
    namespace {
        template <typename T> [[nodiscard]] Result<T> Failure(const ErrorCodeDescriptor &descriptor) {
            return Result<T>::Failure(MakeError(descriptor));
        }

        [[nodiscard]] bool IsKnown(const StreamingCellStabilityLifecycle lifecycle) noexcept {
            return lifecycle < StreamingCellStabilityLifecycle::Count;
        }

        [[nodiscard]] bool IsKnown(const StreamingCellStabilityPhase phase) noexcept {
            return phase < StreamingCellStabilityPhase::Count;
        }

        [[nodiscard]] bool IsKnown(const StreamingDesiredResidency residency) noexcept {
            return residency >= StreamingDesiredResidency::Unloaded && residency <= StreamingDesiredResidency::Activated;
        }

        [[nodiscard]] bool IsValidObservation(const StreamingCellStabilityObservation &observation) noexcept {
            if (!observation.cell.IsValid() || !IsKnown(observation.effectiveResidency))
                return false;
            if (!observation.pinnedResidencyFloor.has_value())
                return true;
            const auto floor = *observation.pinnedResidencyFloor;
            return IsKnown(floor) && floor != StreamingDesiredResidency::Unloaded && floor <= observation.effectiveResidency;
        }

        [[nodiscard]] bool MatchesContext(const StreamingCellStabilitySnapshot &snapshot, const StreamingCellStabilityContext &context,
                                          const StreamingCellStabilityObservation &observation) noexcept {
            return snapshot.policy == context.policy && snapshot.policyRevision == context.policyRevision &&
                   snapshot.partition == context.partition && snapshot.epoch == context.epoch && snapshot.cell == observation.cell;
        }

        [[nodiscard]] bool IsValidPrevious(const StreamingCellStabilitySnapshot &snapshot, const StreamingCellStabilityContext &context,
                                           const StreamingCellStabilityObservation &observation) noexcept {
            if (!MatchesContext(snapshot, context, observation) || !IsKnown(snapshot.phase) ||
                snapshot.phase == StreamingCellStabilityPhase::Unloaded || !IsKnown(snapshot.retainedResidency) ||
                snapshot.retainedResidency == StreamingDesiredResidency::Unloaded ||
                snapshot.observedAtServiceMilliseconds > context.serviceTimeMilliseconds)
                return false;
            if (snapshot.phase == StreamingCellStabilityPhase::Resident)
                return snapshot.lingerStartedAtServiceMilliseconds == 0;
            return snapshot.lingerStartedAtServiceMilliseconds <= snapshot.observedAtServiceMilliseconds;
        }

        [[nodiscard]] StreamingCellStabilitySnapshot Snapshot(const StreamingCellStabilityContext &context,
                                                              const StreamingCellStabilityObservation &observation,
                                                              const StreamingCellStabilityPhase phase,
                                                              const StreamingDesiredResidency residency,
                                                              const std::uint64_t lingerStartedAt) noexcept {
            return {context.policy, context.policyRevision,          context.partition, context.epoch, observation.cell, phase,
                    residency,      context.serviceTimeMilliseconds, lingerStartedAt};
        }
    }  // namespace

    /** @copydoc StreamingCellStabilityPolicy::Create */
    Result<StreamingCellStabilityPolicy> StreamingCellStabilityPolicy::Create(const StreamingCellStabilityPolicyRequest &request) {
        if (request.contractVersion != StreamingCellStabilityPolicyRequest::CurrentContractVersion)
            return Failure<StreamingCellStabilityPolicy>(WorldStreamingErrors::CellStabilityUnsupported);
        if (!request.id.IsValid() || !request.revision.IsValid() || request.enterMarginMillimeters < 0 ||
            request.enterMarginMillimeters > StreamingCellStabilityPolicyRequest::MaximumMarginMillimeters ||
            request.exitMarginMillimeters < 0 ||
            request.exitMarginMillimeters > StreamingCellStabilityPolicyRequest::MaximumMarginMillimeters ||
            request.lingerMilliseconds > StreamingCellStabilityPolicyRequest::MaximumLingerMilliseconds ||
            request.maximumTrackedCells == 0 || request.maximumTrackedCells > StreamingCellStabilityPolicyRequest::MaximumTrackedCellCount)
            return Failure<StreamingCellStabilityPolicy>(WorldStreamingErrors::CellStabilityInvalid);
        return Result<StreamingCellStabilityPolicy>::Success(StreamingCellStabilityPolicy{request});
    }

    StreamingCellStabilityPolicy::StreamingCellStabilityPolicy(const StreamingCellStabilityPolicyRequest &request) noexcept
        : request_(request) {}

    /** @copydoc StreamingCellStabilityPolicy::Id */
    StreamingCellStabilityPolicyId StreamingCellStabilityPolicy::Id() const noexcept {
        return request_.id;
    }

    /** @copydoc StreamingCellStabilityPolicy::Revision */
    StreamingCellStabilityPolicyRevision StreamingCellStabilityPolicy::Revision() const noexcept {
        return request_.revision;
    }

    /** @copydoc StreamingCellStabilityPolicy::MaximumTrackedCells */
    std::uint32_t StreamingCellStabilityPolicy::MaximumTrackedCells() const noexcept {
        return request_.maximumTrackedCells;
    }

    /** @copydoc StreamingCellStabilityPolicy::EnterMarginMillimeters */
    std::int64_t StreamingCellStabilityPolicy::EnterMarginMillimeters() const noexcept {
        return request_.enterMarginMillimeters;
    }

    /** @copydoc StreamingCellStabilityPolicy::ExitMarginMillimeters */
    std::int64_t StreamingCellStabilityPolicy::ExitMarginMillimeters() const noexcept {
        return request_.exitMarginMillimeters;
    }

    /** @copydoc StreamingCellStabilityPolicy::LingerMilliseconds */
    std::uint64_t StreamingCellStabilityPolicy::LingerMilliseconds() const noexcept {
        return request_.lingerMilliseconds;
    }

    /** @copydoc EvaluateStreamingCellStability */
    Result<StreamingCellStabilityDecision> EvaluateStreamingCellStability(const StreamingCellStabilityPolicy &policy,
                                                                          const StreamingCellStabilityContext &context,
                                                                          const StreamingCellStabilityObservation &observation,
                                                                          const std::optional<StreamingCellStabilitySnapshot> &previous) {
        if (!IsKnown(context.lifecycle))
            return Failure<StreamingCellStabilityDecision>(WorldStreamingErrors::CellStabilityUnsupported);
        if (!IsKnown(observation.effectiveResidency) ||
            (observation.pinnedResidencyFloor.has_value() && !IsKnown(*observation.pinnedResidencyFloor)))
            return Failure<StreamingCellStabilityDecision>(WorldStreamingErrors::CellStabilityUnsupported);
        if (!context.policy.IsValid() || !context.policyRevision.IsValid() || !context.partition.IsValid() || !context.epoch.IsValid() ||
            !IsValidObservation(observation))
            return Failure<StreamingCellStabilityDecision>(WorldStreamingErrors::CellStabilityInvalid);
        if (context.policy != policy.Id() || context.policyRevision != policy.Revision())
            return Failure<StreamingCellStabilityDecision>(WorldStreamingErrors::CellStabilityStale);
        if (context.lifecycle != StreamingCellStabilityLifecycle::Active)
            return Failure<StreamingCellStabilityDecision>(WorldStreamingErrors::CellStabilityLifecycleUnavailable);
        if (previous.has_value() && !IsValidPrevious(*previous, context, observation))
            return Failure<StreamingCellStabilityDecision>(WorldStreamingErrors::CellStabilityStale);

        const bool hasDemand = observation.effectiveResidency != StreamingDesiredResidency::Unloaded;
        const bool pinned = observation.pinnedResidencyFloor.has_value();
        if (!previous.has_value()) {
            if (!hasDemand || (!pinned && observation.signedBoundaryDistanceMillimeters < policy.EnterMarginMillimeters()))
                return Result<StreamingCellStabilityDecision>::Success(
                    {Snapshot(context, observation, StreamingCellStabilityPhase::Unloaded, StreamingDesiredResidency::Unloaded, 0), false,
                     false});
            if (context.trackedCells >= policy.MaximumTrackedCells())
                return Failure<StreamingCellStabilityDecision>(WorldStreamingErrors::CellStabilityCapacityExceeded);
            return Result<StreamingCellStabilityDecision>::Success(
                {Snapshot(context, observation, StreamingCellStabilityPhase::Resident, observation.effectiveResidency, 0), false, false});
        }

        const bool withinExitBoundary = observation.signedBoundaryDistanceMillimeters >= -policy.ExitMarginMillimeters();
        if (hasDemand && (pinned || withinExitBoundary)) {
            const bool boundaryHeld = !pinned && observation.signedBoundaryDistanceMillimeters < policy.EnterMarginMillimeters();
            return Result<StreamingCellStabilityDecision>::Success(
                {Snapshot(context, observation, StreamingCellStabilityPhase::Resident, observation.effectiveResidency, 0), boundaryHeld,
                 false});
        }

        const auto &prior = *previous;
        const std::uint64_t lingerStarted = prior.phase == StreamingCellStabilityPhase::Lingering ? prior.lingerStartedAtServiceMilliseconds
                                                                                                  : context.serviceTimeMilliseconds;
        const std::uint64_t elapsed = context.serviceTimeMilliseconds - lingerStarted;
        if (elapsed >= policy.LingerMilliseconds())
            return Result<StreamingCellStabilityDecision>::Success(
                {Snapshot(context, observation, StreamingCellStabilityPhase::Unloaded, StreamingDesiredResidency::Unloaded, 0), false,
                 true});
        return Result<StreamingCellStabilityDecision>::Success(
            {Snapshot(context, observation, StreamingCellStabilityPhase::Lingering, prior.retainedResidency, lingerStarted), false, false});
    }
}  // namespace Horo::WorldStreaming
