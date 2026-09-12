#include "Horo/WorldStreaming/WorldLayerState.h"

#include "Horo/WorldStreaming/WorldStreamingErrors.h"
#include "WorldStreamingInternal.h"

#include <limits>

namespace Horo::WorldStreaming {
    namespace {
        template <typename T> [[nodiscard]] Result<T> Failure(const ErrorCodeDescriptor &descriptor) {
            return Internal::Failure<T>(descriptor);
        }

        [[nodiscard]] bool IsKnown(const WorldLayerState value) noexcept {
            return value >= WorldLayerState::Unloaded && value <= WorldLayerState::Failed;
        }

        [[nodiscard]] bool IsKnown(const WorldLayerStateTransition value) noexcept {
            return value >= WorldLayerStateTransition::BeginLoad && value <= WorldLayerStateTransition::Fail;
        }

        [[nodiscard]] bool IsKnown(const WorldLayerStateAuthorityState value) noexcept {
            return value >= WorldLayerStateAuthorityState::Active && value <= WorldLayerStateAuthorityState::Closed;
        }

        [[nodiscard]] bool IsDrainTransition(const WorldLayerStateTransition transition) noexcept {
            using enum WorldLayerStateTransition;
            return transition == BeginDeactivation || transition == CompleteDeactivation || transition == BeginUnload ||
                   transition == CompleteUnload || transition == Cancel || transition == Fail;
        }

        /** @brief Resolve cancellation to the deterministic rollback state for in-flight work. */
        [[nodiscard]] Result<WorldLayerState> ApplyCancellation(const WorldLayerState state) {
            using enum WorldLayerState;
            if (state == Loading)
                return Result<WorldLayerState>::Success(Unloading);
            if (state == Activating)
                return Result<WorldLayerState>::Success(Deactivating);
            if (state == Deactivating || state == Unloading)
                return Result<WorldLayerState>::Success(state);
            return Failure<WorldLayerState>(WorldStreamingErrors::LayerStateTransitionInvalid);
        }

        [[nodiscard]] Result<WorldLayerState> ApplyTransition(const WorldLayerState state, const WorldLayerStateTransition transition) {
            using enum WorldLayerState;
            using enum WorldLayerStateTransition;
            switch (transition) {
                case BeginLoad:
                    if (state == Unloaded || state == Failed)
                        return Result<WorldLayerState>::Success(Loading);
                    break;
                case CompleteLoad:
                    if (state == Loading)
                        return Result<WorldLayerState>::Success(Loaded);
                    break;
                case BeginActivation:
                    if (state == Loaded)
                        return Result<WorldLayerState>::Success(Activating);
                    break;
                case CompleteActivation:
                    if (state == Activating)
                        return Result<WorldLayerState>::Success(Activated);
                    break;
                case BeginDeactivation:
                    if (state == Activated)
                        return Result<WorldLayerState>::Success(Deactivating);
                    break;
                case CompleteDeactivation:
                    if (state == Deactivating)
                        return Result<WorldLayerState>::Success(Loaded);
                    break;
                case BeginUnload:
                    if (state == Loaded)
                        return Result<WorldLayerState>::Success(Unloading);
                    break;
                case CompleteUnload:
                    if (state == Unloading)
                        return Result<WorldLayerState>::Success(Unloaded);
                    break;
                case Cancel:
                    return ApplyCancellation(state);
                case Fail:
                    if (state == Loading || state == Activating || state == Deactivating || state == Unloading)
                        return Result<WorldLayerState>::Success(Failed);
                    break;
            }
            return Failure<WorldLayerState>(WorldStreamingErrors::LayerStateTransitionInvalid);
        }

        [[nodiscard]] Result<void> ValidateCurrent(const WorldLayerStateRecord &current, const WorldLayerStateFence &expected,
                                                   const WorldLayerStateAuthorityState authorityState) {
            if (!current.IsValid() || !expected.IsValid() || !IsKnown(authorityState))
                return Failure<void>(WorldStreamingErrors::LayerStateInvalid);
            if (current.Fence() != expected)
                return Failure<void>(WorldStreamingErrors::LayerStateStale);
            if (authorityState == WorldLayerStateAuthorityState::Closed)
                return Failure<void>(WorldStreamingErrors::LayerStateLifecycleUnavailable);
            return Result<void>::Success();
        }

        /** @brief Publish the next immutable layer-state record after a successful mutation. */
        [[nodiscard]] Result<WorldLayerStateRecord> SuccessorRecord(const WorldLayerStateRecord &current,
                                                                    const WorldLayerOwnershipDescriptor &ownership,
                                                                    const WorldLayerState state) {
            const auto nextRevision = NextWorldLayerStateRevision(current.revision);
            if (nextRevision.HasError())
                return Result<WorldLayerStateRecord>::Failure(nextRevision.ErrorValue());
            return Result<WorldLayerStateRecord>::Success({.ownership = ownership, .revision = nextRevision.Value(), .state = state});
        }
    }  // namespace

    /** @copydoc WorldLayerStateFence::IsValid */
    bool WorldLayerStateFence::IsValid() const noexcept {
        return world.IsValid() && layer.IsValid() && ownershipRevision.IsValid() && stateRevision.IsValid();
    }

    /** @copydoc WorldLayerStateRecord::IsValid */
    bool WorldLayerStateRecord::IsValid() const noexcept {
        return ownership.IsValid() && revision.IsValid() && IsKnown(state);
    }

    /** @copydoc WorldLayerStateRecord::Fence */
    WorldLayerStateFence WorldLayerStateRecord::Fence() const noexcept {
        return {.world = ownership.owner.world,
                .layer = ownership.layer,
                .ownershipRevision = ownership.revision,
                .stateRevision = revision};
    }

    /** @copydoc CreateWorldLayerStateRecord */
    Result<WorldLayerStateRecord> CreateWorldLayerStateRecord(const WorldLayerOwnershipDescriptor &ownership,
                                                              const WorldLayerStateAdmissionContext &context) {
        if (const auto valid = ValidateWorldLayerOwnershipDescriptor(ownership); valid.HasError())
            return Result<WorldLayerStateRecord>::Failure(valid.ErrorValue());
        if (!context.expectedWorld.IsValid() || !IsKnown(context.authorityState) || context.layerCapacity == 0 ||
            context.layerCount > context.layerCapacity) {
            return Failure<WorldLayerStateRecord>(WorldStreamingErrors::LayerStateInvalid);
        }
        if (ownership.owner.world != context.expectedWorld)
            return Failure<WorldLayerStateRecord>(WorldStreamingErrors::LayerStateStale);
        if (context.authorityState != WorldLayerStateAuthorityState::Active)
            return Failure<WorldLayerStateRecord>(WorldStreamingErrors::LayerStateLifecycleUnavailable);
        if (context.layerCount == context.layerCapacity)
            return Failure<WorldLayerStateRecord>(WorldStreamingErrors::LayerStateCapacityExceeded);
        return Result<WorldLayerStateRecord>::Success(
            {.ownership = ownership, .revision = WorldLayerStateRevision::Create(1).Value(), .state = WorldLayerState::Unloaded});
    }

    /** @copydoc AdvanceWorldLayerState */
    Result<WorldLayerStateRecord> AdvanceWorldLayerState(const WorldLayerStateRecord &current,
                                                         const WorldLayerStateTransitionRequest &request,
                                                         const WorldLayerStateAuthorityState authorityState) {
        if (!IsKnown(request.transition))
            return Failure<WorldLayerStateRecord>(WorldStreamingErrors::LayerStateUnsupported);
        if (const auto valid = ValidateCurrent(current, request.expected, authorityState); valid.HasError())
            return Result<WorldLayerStateRecord>::Failure(valid.ErrorValue());
        if (authorityState == WorldLayerStateAuthorityState::Cancelling && !IsDrainTransition(request.transition))
            return Failure<WorldLayerStateRecord>(WorldStreamingErrors::LayerStateLifecycleUnavailable);

        const auto nextState = ApplyTransition(current.state, request.transition);
        if (nextState.HasError())
            return Result<WorldLayerStateRecord>::Failure(nextState.ErrorValue());
        return SuccessorRecord(current, current.ownership, nextState.Value());
    }

    /** @copydoc ReplaceWorldLayerStateOwnership */
    Result<WorldLayerStateRecord> ReplaceWorldLayerStateOwnership(const WorldLayerStateRecord &current,
                                                                  const WorldLayerOwnershipDescriptor &replacement,
                                                                  const WorldLayerStateFence &expected,
                                                                  const WorldLayerStateAuthorityState authorityState) {
        if (const auto valid = ValidateCurrent(current, expected, authorityState); valid.HasError())
            return Result<WorldLayerStateRecord>::Failure(valid.ErrorValue());
        if (authorityState != WorldLayerStateAuthorityState::Active)
            return Failure<WorldLayerStateRecord>(WorldStreamingErrors::LayerStateLifecycleUnavailable);
        if (current.state != WorldLayerState::Unloaded && current.state != WorldLayerState::Failed)
            return Failure<WorldLayerStateRecord>(WorldStreamingErrors::LayerStateTransitionInvalid);

        const WorldLayerOwnershipAdmissionContext ownershipContext{.expectedWorld = expected.world,
                                                                   .current = current.ownership,
                                                                   .layerCount = 1,
                                                                   .layerCapacity = 1,
                                                                   .state = WorldLayerOwnershipAuthorityState::Active};
        const WorldLayerOwnershipRequest ownershipRequest{.candidate = replacement, .expectedRevision = current.ownership.revision};
        if (const auto admitted = ValidateWorldLayerOwnershipAdmission(ownershipRequest, ownershipContext); admitted.HasError())
            return Result<WorldLayerStateRecord>::Failure(admitted.ErrorValue());

        return SuccessorRecord(current, replacement, WorldLayerState::Unloaded);
    }

    /** @copydoc NextWorldLayerStateRevision */
    Result<WorldLayerStateRevision> NextWorldLayerStateRevision(const WorldLayerStateRevision current) {
        if (!current.IsValid())
            return Failure<WorldLayerStateRevision>(WorldStreamingErrors::IdentityInvalid);
        if (current.Value() == std::numeric_limits<std::uint64_t>::max())
            return Failure<WorldLayerStateRevision>(WorldStreamingErrors::GenerationExhausted);
        return WorldLayerStateRevision::Create(current.Value() + 1);
    }
}  // namespace Horo::WorldStreaming
