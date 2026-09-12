#include "Horo/WorldStreaming/RuntimeEntityCellExitOperation.h"

#include "Horo/WorldStreaming/WorldStreamingErrors.h"
#include "WorldStreamingInternal.h"

#include <algorithm>
#include <array>
#include <utility>

namespace Horo::WorldStreaming {
    namespace {
        [[nodiscard]] bool IsKnown(const RuntimeEntityCellExitTransition transition) noexcept {
            return transition >= RuntimeEntityCellExitTransition::Admit &&
                   transition <= RuntimeEntityCellExitTransition::AcknowledgeDestinationRollback;
        }

        [[nodiscard]] bool IsKnown(const WorldObjectOwnershipOwnerState state) noexcept {
            return state >= WorldObjectOwnershipOwnerState::Active && state <= WorldObjectOwnershipOwnerState::Closed;
        }

        [[nodiscard]] bool IsRuntimeCellOwnership(const WorldObjectOwnershipDescriptor &descriptor) noexcept {
            return descriptor.objectClass == WorldObjectOwnershipClass::RuntimeSpawned &&
                   descriptor.owner.kind == WorldObjectOwnerKind::Cell;
        }

        [[nodiscard]] Result<void> ValidateStructure(const RuntimeEntityCellExitRequest &request,
                                                     const RuntimeEntityCellExitAdmissionContext &context) {
            if (!request.handle.IsValid() || !context.expectedWorld.IsValid() || !context.retiringCell.IsValid() ||
                !IsKnown(context.state) || context.operationCapacity == 0 || context.inFlightOperations > context.operationCapacity)
                return Internal::Failure<void>(WorldStreamingErrors::RuntimeEntityCellExitInvalid);

            const auto sourceValid = ValidateWorldObjectOwnershipDescriptor(request.sourceOwnership);
            if (const auto currentValid = ValidateWorldObjectOwnershipDescriptor(context.currentOwnership);
                sourceValid.HasError() || currentValid.HasError())
                return Internal::Failure<void>(WorldStreamingErrors::RuntimeEntityCellExitInvalid);
            return Result<void>::Success();
        }

        [[nodiscard]] Result<void> ValidateCurrentAuthority(const RuntimeEntityCellExitRequest &request,
                                                            const RuntimeEntityCellExitAdmissionContext &context) {
            if (!IsRuntimeCellOwnership(request.sourceOwnership))
                return Internal::Failure<void>(WorldStreamingErrors::RuntimeEntityCellExitUnsupported);

            if (request.handle.entity != request.sourceOwnership.runtimeSpawned ||
                request.handle.source != request.sourceOwnership.owner.cell || context.retiringCell != request.handle.source ||
                request.sourceOwnership != context.currentOwnership || request.sourceOwnership.owner.world != context.expectedWorld)
                return Internal::Failure<void>(WorldStreamingErrors::RuntimeEntityCellExitStale);
            return Result<void>::Success();
        }

        [[nodiscard]] Result<void> ValidateAdmissionGate(const RuntimeEntityCellExitAdmissionContext &context) {
            if (context.state != WorldObjectOwnershipOwnerState::Active)
                return Internal::Failure<void>(WorldStreamingErrors::RuntimeEntityCellExitLifecycleUnavailable);
            if (context.inFlightOperations == context.operationCapacity)
                return Internal::Failure<void>(WorldStreamingErrors::RuntimeEntityCellExitCapacityExceeded);
            return Result<void>::Success();
        }

        [[nodiscard]] Result<RuntimeEntityCellExitDisposition> ValidateDisposition(const RuntimeEntityCellExitRequest &request,
                                                                                   const RuntimeEntityCellExitAdmissionContext &context) {
            if (request.sourceOwnership.cellExitPolicy == WorldObjectCellExitPolicy::Retire) {
                if (request.destination.has_value())
                    return Internal::Failure<RuntimeEntityCellExitDisposition>(WorldStreamingErrors::RuntimeEntityCellExitUnsupported);
                return Result<RuntimeEntityCellExitDisposition>::Success(RuntimeEntityCellExitDisposition::Retire);
            }
            if (request.sourceOwnership.cellExitPolicy != WorldObjectCellExitPolicy::RequireHandoff || !request.destination.has_value())
                return Internal::Failure<RuntimeEntityCellExitDisposition>(WorldStreamingErrors::RuntimeEntityCellExitUnsupported);

            const WorldObjectOwnershipRequest ownershipRequest{
                .candidate = *request.destination,
                .expectedRevision = request.sourceOwnership.revision,
            };
            const WorldObjectOwnershipAdmissionContext ownershipContext{
                .expectedWorld = context.expectedWorld,
                .current = context.currentOwnership,
                .objectCount = 1,
                .objectCapacity = 1,
                .state = context.state,
            };
            const auto admission = ValidateWorldObjectOwnershipAdmission(ownershipRequest, ownershipContext);
            if (admission.HasError())
                return Result<RuntimeEntityCellExitDisposition>::Failure(admission.ErrorValue());
            if (admission.Value() != WorldObjectOwnershipAdmissionKind::Handoff)
                return Internal::Failure<RuntimeEntityCellExitDisposition>(WorldStreamingErrors::RuntimeEntityCellExitUnsupported);
            return Result<RuntimeEntityCellExitDisposition>::Success(RuntimeEntityCellExitDisposition::Handoff);
        }

        [[nodiscard]] Result<RuntimeEntityCellExitDisposition> ValidateRequest(const RuntimeEntityCellExitRequest &request,
                                                                               const RuntimeEntityCellExitAdmissionContext &context) {
            if (const auto valid = ValidateStructure(request, context); valid.HasError())
                return Result<RuntimeEntityCellExitDisposition>::Failure(valid.ErrorValue());
            if (const auto valid = ValidateCurrentAuthority(request, context); valid.HasError())
                return Result<RuntimeEntityCellExitDisposition>::Failure(valid.ErrorValue());
            if (const auto valid = ValidateAdmissionGate(context); valid.HasError())
                return Result<RuntimeEntityCellExitDisposition>::Failure(valid.ErrorValue());
            return ValidateDisposition(request, context);
        }

        struct TransitionRule final {
            RuntimeEntityCellExitState source;
            RuntimeEntityCellExitTransition transition;
            RuntimeEntityCellExitState destination;
            RuntimeEntityCellExitOutcome outcome;
            RuntimeEntityCellExitDisposition requiredDisposition{RuntimeEntityCellExitDisposition::Retire};
            bool anyDisposition{true};
            bool retainOutcome{};
        };

        constexpr auto TransitionRules = std::to_array<TransitionRule>({
            {RuntimeEntityCellExitState::Queued, RuntimeEntityCellExitTransition::Admit, RuntimeEntityCellExitState::Admitted,
             RuntimeEntityCellExitOutcome::None},
            {RuntimeEntityCellExitState::Admitted, RuntimeEntityCellExitTransition::BeginDestinationPreparation,
             RuntimeEntityCellExitState::PreparingDestination, RuntimeEntityCellExitOutcome::None,
             RuntimeEntityCellExitDisposition::Handoff, false},
            {RuntimeEntityCellExitState::PreparingDestination, RuntimeEntityCellExitTransition::AcceptDestination,
             RuntimeEntityCellExitState::DestinationAccepted, RuntimeEntityCellExitOutcome::None, RuntimeEntityCellExitDisposition::Handoff,
             false},
            {RuntimeEntityCellExitState::Admitted, RuntimeEntityCellExitTransition::BeginSourceRetirement,
             RuntimeEntityCellExitState::RetiringSource, RuntimeEntityCellExitOutcome::Retired, RuntimeEntityCellExitDisposition::Retire,
             false},
            {RuntimeEntityCellExitState::DestinationAccepted, RuntimeEntityCellExitTransition::BeginSourceRetirement,
             RuntimeEntityCellExitState::RetiringSource, RuntimeEntityCellExitOutcome::HandedOff, RuntimeEntityCellExitDisposition::Handoff,
             false},
            {RuntimeEntityCellExitState::RetiringSource, RuntimeEntityCellExitTransition::AcknowledgeSourceRetirement,
             RuntimeEntityCellExitState::Terminal, RuntimeEntityCellExitOutcome::None, RuntimeEntityCellExitDisposition::Retire, true,
             true},
            {RuntimeEntityCellExitState::RollingBackDestination, RuntimeEntityCellExitTransition::AcknowledgeDestinationRollback,
             RuntimeEntityCellExitState::Terminal, RuntimeEntityCellExitOutcome::None, RuntimeEntityCellExitDisposition::Retire, true,
             true},
            {RuntimeEntityCellExitState::RollingBackDestination, RuntimeEntityCellExitTransition::Shutdown,
             RuntimeEntityCellExitState::RollingBackDestination, RuntimeEntityCellExitOutcome::None,
             RuntimeEntityCellExitDisposition::Retire, true, true},
            {RuntimeEntityCellExitState::RetiringSource, RuntimeEntityCellExitTransition::Shutdown,
             RuntimeEntityCellExitState::RetiringSource, RuntimeEntityCellExitOutcome::None, RuntimeEntityCellExitDisposition::Retire, true,
             true},
        });

        [[nodiscard]] RuntimeEntityCellExitOutcome InterruptionOutcome(const RuntimeEntityCellExitTransition transition) noexcept {
            switch (transition) {
                case RuntimeEntityCellExitTransition::Cancel:
                    return RuntimeEntityCellExitOutcome::Cancelled;
                case RuntimeEntityCellExitTransition::Fail:
                    return RuntimeEntityCellExitOutcome::Failed;
                case RuntimeEntityCellExitTransition::Replace:
                    return RuntimeEntityCellExitOutcome::Replaced;
                case RuntimeEntityCellExitTransition::Shutdown:
                    return RuntimeEntityCellExitOutcome::Shutdown;
                default:
                    return RuntimeEntityCellExitOutcome::None;
            }
        }

        [[nodiscard]] bool IsInterruption(const RuntimeEntityCellExitTransition transition) noexcept {
            return transition >= RuntimeEntityCellExitTransition::Cancel && transition <= RuntimeEntityCellExitTransition::Shutdown;
        }
    }  // namespace

    /** @copydoc RuntimeEntityCellExitHandle::IsValid */
    bool RuntimeEntityCellExitHandle::IsValid() const noexcept {
        return operation.IsValid() && entity.IsValid() && source.IsValid();
    }

    /** @copydoc RuntimeEntityCellExitOperation::Create */
    Result<RuntimeEntityCellExitOperation> RuntimeEntityCellExitOperation::Create(const RuntimeEntityCellExitRequest &request,
                                                                                  const RuntimeEntityCellExitAdmissionContext &context) {
        const auto disposition = ValidateRequest(request, context);
        if (disposition.HasError())
            return Result<RuntimeEntityCellExitOperation>::Failure(disposition.ErrorValue());
        return Result<RuntimeEntityCellExitOperation>::Success(
            RuntimeEntityCellExitOperation{request.handle, disposition.Value(), request.sourceOwnership, request.destination,
                                           RuntimeEntityCellExitState::Queued, RuntimeEntityCellExitOutcome::None});
    }

    /** @copydoc RuntimeEntityCellExitOperation::Handle */
    const RuntimeEntityCellExitHandle &RuntimeEntityCellExitOperation::Handle() const noexcept {
        return handle_;
    }

    /** @copydoc RuntimeEntityCellExitOperation::Disposition */
    RuntimeEntityCellExitDisposition RuntimeEntityCellExitOperation::Disposition() const noexcept {
        return disposition_;
    }

    /** @copydoc RuntimeEntityCellExitOperation::State */
    RuntimeEntityCellExitState RuntimeEntityCellExitOperation::State() const noexcept {
        return state_;
    }

    /** @copydoc RuntimeEntityCellExitOperation::Outcome */
    RuntimeEntityCellExitOutcome RuntimeEntityCellExitOperation::Outcome() const noexcept {
        return outcome_;
    }

    /** @copydoc RuntimeEntityCellExitOperation::SourceOwnership */
    const WorldObjectOwnershipDescriptor &RuntimeEntityCellExitOperation::SourceOwnership() const noexcept {
        return source_;
    }

    /** @copydoc RuntimeEntityCellExitOperation::DestinationOwnership */
    const std::optional<WorldObjectOwnershipDescriptor> &RuntimeEntityCellExitOperation::DestinationOwnership() const noexcept {
        return destination_;
    }

    /** @copydoc RuntimeEntityCellExitOperation::IsTerminal */
    bool RuntimeEntityCellExitOperation::IsTerminal() const noexcept {
        return state_ == RuntimeEntityCellExitState::Terminal;
    }

    /** @copydoc RuntimeEntityCellExitOperation::IsCommitted */
    bool RuntimeEntityCellExitOperation::IsCommitted() const noexcept {
        return state_ == RuntimeEntityCellExitState::RetiringSource ||
               (state_ == RuntimeEntityCellExitState::Terminal &&
                (outcome_ == RuntimeEntityCellExitOutcome::Retired || outcome_ == RuntimeEntityCellExitOutcome::HandedOff));
    }

    /** @copydoc RuntimeEntityCellExitOperation::Advance */
    Result<RuntimeEntityCellExitOperation> RuntimeEntityCellExitOperation::Advance(const RuntimeEntityCellExitHandle &expected,
                                                                                   const RuntimeEntityCellExitTransition transition) const {
        using enum RuntimeEntityCellExitState;
        if (!expected.IsValid())
            return Internal::Failure<RuntimeEntityCellExitOperation>(WorldStreamingErrors::RuntimeEntityCellExitInvalid);
        if (expected != handle_)
            return Internal::Failure<RuntimeEntityCellExitOperation>(WorldStreamingErrors::RuntimeEntityCellExitStale);
        if (!IsKnown(transition))
            return Internal::Failure<RuntimeEntityCellExitOperation>(WorldStreamingErrors::RuntimeEntityCellExitUnsupported);

        if (IsInterruption(transition) && state_ != RetiringSource) {
            const auto outcome = InterruptionOutcome(transition);
            if (state_ == Queued || state_ == Admitted)
                return Result<RuntimeEntityCellExitOperation>::Success(
                    RuntimeEntityCellExitOperation{handle_, disposition_, source_, destination_, Terminal, outcome});
            if (state_ == PreparingDestination || state_ == DestinationAccepted)
                return Result<RuntimeEntityCellExitOperation>::Success(
                    RuntimeEntityCellExitOperation{handle_, disposition_, source_, destination_, RollingBackDestination, outcome});
        }

        const auto rule = std::ranges::find_if(TransitionRules, [this, transition](const TransitionRule &candidate) {
            return candidate.source == state_ && candidate.transition == transition &&
                   (candidate.anyDisposition || candidate.requiredDisposition == disposition_);
        });
        if (rule == TransitionRules.end())
            return Internal::Failure<RuntimeEntityCellExitOperation>(WorldStreamingErrors::RuntimeEntityCellExitTransitionInvalid);
        const auto successorOutcome = rule->retainOutcome ? outcome_ : rule->outcome;
        return Result<RuntimeEntityCellExitOperation>::Success(
            RuntimeEntityCellExitOperation{handle_, disposition_, source_, destination_, rule->destination, successorOutcome});
    }

    RuntimeEntityCellExitOperation::RuntimeEntityCellExitOperation(RuntimeEntityCellExitHandle handle,
                                                                   const RuntimeEntityCellExitDisposition disposition,
                                                                   WorldObjectOwnershipDescriptor source,
                                                                   std::optional<WorldObjectOwnershipDescriptor> destination,
                                                                   const RuntimeEntityCellExitState state,
                                                                   const RuntimeEntityCellExitOutcome outcome) noexcept
        : handle_(std::move(handle)), disposition_(disposition), source_(std::move(source)), destination_(std::move(destination)),
          state_(state), outcome_(outcome) {}
}  // namespace Horo::WorldStreaming
