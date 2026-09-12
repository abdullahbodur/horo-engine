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

        [[nodiscard]] bool IsKnown(const WorldLayerStateRollbackDisposition value) noexcept {
            return value >= WorldLayerStateRollbackDisposition::None && value <= WorldLayerStateRollbackDisposition::FailurePending;
        }

        [[nodiscard]] bool IsKnown(const WorldLayerStateTransition value) noexcept {
            return value >= WorldLayerStateTransition::BeginLoad && value <= WorldLayerStateTransition::Fail;
        }

        [[nodiscard]] bool IsKnown(const WorldLayerStateAuthorityState value) noexcept {
            return value >= WorldLayerStateAuthorityState::Active && value <= WorldLayerStateAuthorityState::Closed;
        }

        /** @brief Whether an accepted rule publishes a successor or returns the exact current fact. */
        enum class RevisionEffect : std::uint8_t {
            Advance,
            Preserve,
        };

        /** @brief One closed state, disposition, command and lifecycle transition rule. */
        struct TransitionRule final {
            WorldLayerState from{};
            WorldLayerStateRollbackDisposition disposition{};
            WorldLayerStateTransition transition{};
            WorldLayerState to{};
            WorldLayerStateRollbackDisposition nextDisposition{};
            bool allowedDuringCancellation{};
            RevisionEffect revisionEffect{};
        };

        /** @brief Complete legal transition relation for valid layer-state records. */
        constexpr TransitionRule kTransitionRules[]{
            {WorldLayerState::Unloaded, WorldLayerStateRollbackDisposition::None, WorldLayerStateTransition::BeginLoad,
             WorldLayerState::Loading, WorldLayerStateRollbackDisposition::None, false, RevisionEffect::Advance},
            {WorldLayerState::Failed, WorldLayerStateRollbackDisposition::None, WorldLayerStateTransition::BeginLoad,
             WorldLayerState::Loading, WorldLayerStateRollbackDisposition::None, false, RevisionEffect::Advance},
            {WorldLayerState::Loading, WorldLayerStateRollbackDisposition::None, WorldLayerStateTransition::CompleteLoad,
             WorldLayerState::Loaded, WorldLayerStateRollbackDisposition::None, false, RevisionEffect::Advance},
            {WorldLayerState::Loaded, WorldLayerStateRollbackDisposition::None, WorldLayerStateTransition::BeginActivation,
             WorldLayerState::Activating, WorldLayerStateRollbackDisposition::None, false, RevisionEffect::Advance},
            {WorldLayerState::Activating, WorldLayerStateRollbackDisposition::None, WorldLayerStateTransition::CompleteActivation,
             WorldLayerState::Activated, WorldLayerStateRollbackDisposition::None, false, RevisionEffect::Advance},
            {WorldLayerState::Activated, WorldLayerStateRollbackDisposition::None, WorldLayerStateTransition::BeginDeactivation,
             WorldLayerState::Deactivating, WorldLayerStateRollbackDisposition::None, true, RevisionEffect::Advance},
            {WorldLayerState::Deactivating, WorldLayerStateRollbackDisposition::None, WorldLayerStateTransition::CompleteDeactivation,
             WorldLayerState::Loaded, WorldLayerStateRollbackDisposition::None, true, RevisionEffect::Advance},
            {WorldLayerState::Loaded, WorldLayerStateRollbackDisposition::None, WorldLayerStateTransition::BeginUnload,
             WorldLayerState::Unloading, WorldLayerStateRollbackDisposition::None, true, RevisionEffect::Advance},
            {WorldLayerState::Unloading, WorldLayerStateRollbackDisposition::None, WorldLayerStateTransition::CompleteUnload,
             WorldLayerState::Unloaded, WorldLayerStateRollbackDisposition::None, true, RevisionEffect::Advance},
            {WorldLayerState::Loading, WorldLayerStateRollbackDisposition::None, WorldLayerStateTransition::Cancel,
             WorldLayerState::Unloading, WorldLayerStateRollbackDisposition::CancellationPending, true, RevisionEffect::Advance},
            {WorldLayerState::Activating, WorldLayerStateRollbackDisposition::None, WorldLayerStateTransition::Cancel,
             WorldLayerState::Deactivating, WorldLayerStateRollbackDisposition::CancellationPending, true, RevisionEffect::Advance},
            {WorldLayerState::Deactivating, WorldLayerStateRollbackDisposition::CancellationPending,
             WorldLayerStateTransition::CompleteDeactivation, WorldLayerState::Loaded, WorldLayerStateRollbackDisposition::None, true,
             RevisionEffect::Advance},
            {WorldLayerState::Unloading, WorldLayerStateRollbackDisposition::CancellationPending, WorldLayerStateTransition::CompleteUnload,
             WorldLayerState::Unloaded, WorldLayerStateRollbackDisposition::None, true, RevisionEffect::Advance},
            {WorldLayerState::Deactivating, WorldLayerStateRollbackDisposition::FailurePending,
             WorldLayerStateTransition::CompleteDeactivation, WorldLayerState::Unloading,
             WorldLayerStateRollbackDisposition::FailurePending, true, RevisionEffect::Advance},
            {WorldLayerState::Unloading, WorldLayerStateRollbackDisposition::FailurePending, WorldLayerStateTransition::CompleteUnload,
             WorldLayerState::Failed, WorldLayerStateRollbackDisposition::None, true, RevisionEffect::Advance},
            {WorldLayerState::Loading, WorldLayerStateRollbackDisposition::None, WorldLayerStateTransition::Fail,
             WorldLayerState::Unloading, WorldLayerStateRollbackDisposition::FailurePending, true, RevisionEffect::Advance},
            {WorldLayerState::Activating, WorldLayerStateRollbackDisposition::None, WorldLayerStateTransition::Fail,
             WorldLayerState::Deactivating, WorldLayerStateRollbackDisposition::FailurePending, true, RevisionEffect::Advance},
            {WorldLayerState::Deactivating, WorldLayerStateRollbackDisposition::None, WorldLayerStateTransition::Fail,
             WorldLayerState::Deactivating, WorldLayerStateRollbackDisposition::FailurePending, true, RevisionEffect::Advance},
            {WorldLayerState::Deactivating, WorldLayerStateRollbackDisposition::CancellationPending, WorldLayerStateTransition::Fail,
             WorldLayerState::Deactivating, WorldLayerStateRollbackDisposition::FailurePending, true, RevisionEffect::Advance},
            {WorldLayerState::Unloading, WorldLayerStateRollbackDisposition::None, WorldLayerStateTransition::Fail,
             WorldLayerState::Unloading, WorldLayerStateRollbackDisposition::FailurePending, true, RevisionEffect::Advance},
            {WorldLayerState::Unloading, WorldLayerStateRollbackDisposition::CancellationPending, WorldLayerStateTransition::Fail,
             WorldLayerState::Unloading, WorldLayerStateRollbackDisposition::FailurePending, true, RevisionEffect::Advance},
            {WorldLayerState::Deactivating, WorldLayerStateRollbackDisposition::FailurePending, WorldLayerStateTransition::Fail,
             WorldLayerState::Deactivating, WorldLayerStateRollbackDisposition::FailurePending, true, RevisionEffect::Preserve},
            {WorldLayerState::Unloading, WorldLayerStateRollbackDisposition::FailurePending, WorldLayerStateTransition::Fail,
             WorldLayerState::Unloading, WorldLayerStateRollbackDisposition::FailurePending, true, RevisionEffect::Preserve},
            {WorldLayerState::Deactivating, WorldLayerStateRollbackDisposition::None, WorldLayerStateTransition::Cancel,
             WorldLayerState::Deactivating, WorldLayerStateRollbackDisposition::None, true, RevisionEffect::Preserve},
            {WorldLayerState::Deactivating, WorldLayerStateRollbackDisposition::CancellationPending, WorldLayerStateTransition::Cancel,
             WorldLayerState::Deactivating, WorldLayerStateRollbackDisposition::CancellationPending, true, RevisionEffect::Preserve},
            {WorldLayerState::Deactivating, WorldLayerStateRollbackDisposition::FailurePending, WorldLayerStateTransition::Cancel,
             WorldLayerState::Deactivating, WorldLayerStateRollbackDisposition::FailurePending, true, RevisionEffect::Preserve},
            {WorldLayerState::Unloading, WorldLayerStateRollbackDisposition::None, WorldLayerStateTransition::Cancel,
             WorldLayerState::Unloading, WorldLayerStateRollbackDisposition::None, true, RevisionEffect::Preserve},
            {WorldLayerState::Unloading, WorldLayerStateRollbackDisposition::CancellationPending, WorldLayerStateTransition::Cancel,
             WorldLayerState::Unloading, WorldLayerStateRollbackDisposition::CancellationPending, true, RevisionEffect::Preserve},
            {WorldLayerState::Unloading, WorldLayerStateRollbackDisposition::FailurePending, WorldLayerStateTransition::Cancel,
             WorldLayerState::Unloading, WorldLayerStateRollbackDisposition::FailurePending, true, RevisionEffect::Preserve},
        };

        /** @brief Check command-level permission while authority drains cancellation. */
        [[nodiscard]] bool AllowsDuringCancellation(const WorldLayerStateTransition transition) noexcept {
            for (const auto &rule : kTransitionRules) {
                if (rule.transition == transition && rule.allowedDuringCancellation)
                    return true;
            }
            return false;
        }

        /** @brief Resolve the unique rule for an exact valid state fact and command. */
        [[nodiscard]] const TransitionRule *FindTransition(const WorldLayerStateRecord &current,
                                                           const WorldLayerStateTransition transition) noexcept {
            for (const auto &rule : kTransitionRules) {
                if (rule.from == current.state && rule.disposition == current.rollbackDisposition && rule.transition == transition)
                    return &rule;
            }
            return nullptr;
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
                                                                    const WorldLayerState state,
                                                                    const WorldLayerStateRollbackDisposition disposition,
                                                                    const RevisionEffect revisionEffect = RevisionEffect::Advance) {
            if (revisionEffect == RevisionEffect::Preserve)
                return Result<WorldLayerStateRecord>::Success(current);
            const auto nextRevision = NextWorldLayerStateRevision(current.revision);
            if (nextRevision.HasError())
                return Result<WorldLayerStateRecord>::Failure(nextRevision.ErrorValue());
            return Result<WorldLayerStateRecord>::Success(
                {.ownership = ownership, .revision = nextRevision.Value(), .state = state, .rollbackDisposition = disposition});
        }
    }  // namespace

    /** @copydoc WorldLayerStateFence::IsValid */
    bool WorldLayerStateFence::IsValid() const noexcept {
        return world.IsValid() && layer.IsValid() && ownershipRevision.IsValid() && stateRevision.IsValid();
    }

    /** @copydoc WorldLayerStateRecord::IsValid */
    bool WorldLayerStateRecord::IsValid() const noexcept {
        if (!ownership.IsValid() || !revision.IsValid() || !IsKnown(state) || !IsKnown(rollbackDisposition))
            return false;
        return rollbackDisposition == WorldLayerStateRollbackDisposition::None || state == WorldLayerState::Deactivating ||
               state == WorldLayerState::Unloading;
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
        return Result<WorldLayerStateRecord>::Success({.ownership = ownership,
                                                       .revision = WorldLayerStateRevision::Create(1).Value(),
                                                       .state = WorldLayerState::Unloaded,
                                                       .rollbackDisposition = WorldLayerStateRollbackDisposition::None});
    }

    /** @copydoc AdvanceWorldLayerState */
    Result<WorldLayerStateRecord> AdvanceWorldLayerState(const WorldLayerStateRecord &current,
                                                         const WorldLayerStateTransitionRequest &request,
                                                         const WorldLayerStateAuthorityState authorityState) {
        if (!IsKnown(request.transition))
            return Failure<WorldLayerStateRecord>(WorldStreamingErrors::LayerStateUnsupported);
        if (const auto valid = ValidateCurrent(current, request.expected, authorityState); valid.HasError())
            return Result<WorldLayerStateRecord>::Failure(valid.ErrorValue());
        if (authorityState == WorldLayerStateAuthorityState::Cancelling && !AllowsDuringCancellation(request.transition))
            return Failure<WorldLayerStateRecord>(WorldStreamingErrors::LayerStateLifecycleUnavailable);

        const auto *rule = FindTransition(current, request.transition);
        if (rule == nullptr)
            return Failure<WorldLayerStateRecord>(WorldStreamingErrors::LayerStateTransitionInvalid);
        return SuccessorRecord(current, current.ownership, rule->to, rule->nextDisposition, rule->revisionEffect);
    }

    /** @copydoc ReplaceWorldLayerStateOwnership */
    Result<WorldLayerStateRecord> ReplaceWorldLayerStateOwnership(const WorldLayerStateRecord &current,
                                                                  const WorldLayerOwnershipDescriptor &replacement,
                                                                  const WorldLayerStateFence &expected,
                                                                  const WorldLayerStateOwnershipReplacementContext &context) {
        if (const auto valid = ValidateCurrent(current, expected, context.authorityState); valid.HasError())
            return Result<WorldLayerStateRecord>::Failure(valid.ErrorValue());
        if (context.authorityState != WorldLayerStateAuthorityState::Active)
            return Failure<WorldLayerStateRecord>(WorldStreamingErrors::LayerStateLifecycleUnavailable);
        if (current.state != WorldLayerState::Unloaded && current.state != WorldLayerState::Failed)
            return Failure<WorldLayerStateRecord>(WorldStreamingErrors::LayerStateTransitionInvalid);

        const WorldLayerOwnershipAdmissionContext ownershipContext{.expectedWorld = expected.world,
                                                                   .current = current.ownership,
                                                                   .authorizedHandoff = context.authorizedHandoff,
                                                                   .validatedHandoffTarget = context.validatedHandoffTarget,
                                                                   .layerCount = 1,
                                                                   .layerCapacity = 1,
                                                                   .state = WorldLayerOwnershipAuthorityState::Active};
        const WorldLayerOwnershipRequest ownershipRequest{.candidate = replacement,
                                                          .expectedRevision = current.ownership.revision,
                                                          .handoff = context.authorizedHandoff};
        if (const auto admitted = ValidateWorldLayerOwnershipAdmission(ownershipRequest, ownershipContext); admitted.HasError())
            return Result<WorldLayerStateRecord>::Failure(admitted.ErrorValue());

        return SuccessorRecord(current, replacement, WorldLayerState::Unloaded, WorldLayerStateRollbackDisposition::None);
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
