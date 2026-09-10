#include "Horo/Destruction/DestructionStateMachine.h"

#include "Horo/Destruction/DestructionErrors.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace Horo::Destruction {
    namespace {
        template <typename T> [[nodiscard]] Result<T> StateFailure(const ErrorCodeDescriptor &descriptor) {
            return Result<T>::Failure(MakeError(descriptor));
        }

        [[nodiscard]] Result<void> ValidateDescriptorTarget(const DestructibleDescriptor &descriptor, const DestructionHandle target) {
            if (!target.IsValid())
                return StateFailure<void>(DestructionErrors::IdentityInvalid);
            if (descriptor.Data().destructible != target.destructible)
                return StateFailure<void>(DestructionErrors::IdentityUnknown);
            return Result<void>::Success();
        }

        [[nodiscard]] DestructionStatePhase PhaseForHealth(const float health, const DestructionHealthPolicy &policy) noexcept {
            using enum DestructionStatePhase;
            if (health <= policy.fractureHealthThreshold)
                return Destroyed;
            if (health <= policy.damagedHealthThreshold)
                return Damaged;
            return Intact;
        }

        [[nodiscard]] bool SameCommandIdentity(const DestructionStateCommand &left, const DestructionStateCommand &right) noexcept {
            return left.Id() == right.Id();
        }

        enum class CommandSequenceDisposition : std::uint8_t {
            Fresh,
            ExactDuplicate,
        };

        [[nodiscard]] Result<CommandSequenceDisposition> ClassifyCommandSequence(const std::optional<DestructionStateCommand> &lastCommand,
                                                                                 const DestructionStateCommand &command) {
            using enum CommandSequenceDisposition;
            if (!lastCommand.has_value())
                return Result<CommandSequenceDisposition>::Success(Fresh);
            if (SameCommandIdentity(*lastCommand, command)) {
                if (*lastCommand != command)
                    return StateFailure<CommandSequenceDisposition>(DestructionErrors::DuplicateCommand);
                return Result<CommandSequenceDisposition>::Success(ExactDuplicate);
            }
            if (command.Id().value.Value() <= lastCommand->Id().value.Value())
                return StateFailure<CommandSequenceDisposition>(DestructionErrors::DuplicateCommand);
            return Result<CommandSequenceDisposition>::Success(Fresh);
        }

        [[nodiscard]] Result<void> ValidatePreparedTransition(const DestructionStateSnapshot &active,
                                                              const DestructionStateTransition &transition) {
            if (transition.Status() == DestructionTransitionStatus::Cancelled)
                return StateFailure<void>(DestructionErrors::CancelledBeforeCommit);
            if (transition.Status() != DestructionTransitionStatus::Prepared)
                return StateFailure<void>(DestructionErrors::StateInvalid);
            if (transition.Source().revision != active.revision)
                return StateFailure<void>(DestructionErrors::StaleRevision);
            if (transition.Source() != active)
                return StateFailure<void>(DestructionErrors::StateInvalid);
            return Result<void>::Success();
        }
    }  // namespace

    DestructionStateCommand::DestructionStateCommand(DestructionCommandId id, const DestructionStateRevision expectedRevision,
                                                     const DestructionStateCommandKind kind, const float damage) noexcept
        : id_(std::move(id)), expectedRevision_(expectedRevision), kind_(kind), damage_(damage) {}

    /** @copydoc DestructionStateCommand::Damage */
    Result<DestructionStateCommand> DestructionStateCommand::Damage(DestructionCommandId id,
                                                                    const DestructionStateRevision expectedRevision, const float damage) {
        if (!id.IsValid() || !expectedRevision.IsValid())
            return StateFailure<DestructionStateCommand>(DestructionErrors::IdentityInvalid);
        if (!std::isfinite(damage) || damage <= 0.0F)
            return StateFailure<DestructionStateCommand>(DestructionErrors::InvalidDamage);
        return Result<DestructionStateCommand>::Success(
            DestructionStateCommand{std::move(id), expectedRevision, DestructionStateCommandKind::ApplyDamage, damage});
    }

    /** @copydoc DestructionStateCommand::Destroy */
    Result<DestructionStateCommand> DestructionStateCommand::Destroy(DestructionCommandId id,
                                                                     const DestructionStateRevision expectedRevision) {
        if (!id.IsValid() || !expectedRevision.IsValid())
            return StateFailure<DestructionStateCommand>(DestructionErrors::IdentityInvalid);
        return Result<DestructionStateCommand>::Success(
            DestructionStateCommand{std::move(id), expectedRevision, DestructionStateCommandKind::Destroy, 0.0F});
    }

    DestructionStateTransition::DestructionStateTransition(DestructionStateCommand command, DestructionStateSnapshot source,
                                                           DestructionStateSnapshot successor,
                                                           const DestructionTransitionStatus status) noexcept
        : command_(std::move(command)), source_(std::move(source)), successor_(std::move(successor)), status_(status) {}

    /** @copydoc DestructionStateTransition::Cancel */
    DestructionStateTransition DestructionStateTransition::Cancel() const noexcept {
        auto cancelled = *this;
        cancelled.status_ = DestructionTransitionStatus::Cancelled;
        return cancelled;
    }

    DestructionStateMachine::DestructionStateMachine(DestructionStateSnapshot snapshot, DestructionHealthPolicy healthPolicy,
                                                     std::optional<DestructionStateCommand> lastCommand, const bool admissionOpen) noexcept
        : snapshot_(std::move(snapshot)), healthPolicy_(healthPolicy), lastCommand_(std::move(lastCommand)), admissionOpen_(admissionOpen) {
    }

    /** @copydoc DestructionStateMachine::Create */
    Result<DestructionStateMachine> DestructionStateMachine::Create(const DestructibleDescriptor &descriptor,
                                                                    const DestructionHandle target,
                                                                    const DestructionStateRevision initialRevision) {
        if (const auto targetResult = ValidateDescriptorTarget(descriptor, target); targetResult.HasError())
            return Result<DestructionStateMachine>::Failure(targetResult.ErrorValue());
        if (!initialRevision.IsValid())
            return StateFailure<DestructionStateMachine>(DestructionErrors::IdentityInvalid);

        const auto &data = descriptor.Data();
        DestructionStateSnapshot snapshot{.target = target,
                                          .content = data.content,
                                          .configurationRevision = data.configurationRevision,
                                          .effectiveFeatures = descriptor.EffectiveFeatures(),
                                          .revision = initialRevision,
                                          .phase = DestructionStatePhase::Intact,
                                          .health = data.health.maximumHealth};
        return Result<DestructionStateMachine>::Success(DestructionStateMachine{std::move(snapshot), data.health, std::nullopt, true});
    }

    /** @copydoc DestructionStateMachine::Prepare */
    Result<DestructionStateTransition> DestructionStateMachine::Prepare(const DestructionStateCommand &command) const {
        if (const auto access = ValidateDestructionCommandAccess(command.Id(), snapshot_.target); access.HasError())
            return Result<DestructionStateTransition>::Failure(access.ErrorValue());
        const auto sequence = ClassifyCommandSequence(lastCommand_, command);
        if (sequence.HasError())
            return Result<DestructionStateTransition>::Failure(sequence.ErrorValue());
        if (sequence.Value() == CommandSequenceDisposition::ExactDuplicate) {
            return Result<DestructionStateTransition>::Success(
                DestructionStateTransition{command, snapshot_, snapshot_, DestructionTransitionStatus::Duplicate});
        }
        if (!admissionOpen_)
            return StateFailure<DestructionStateTransition>(DestructionErrors::ShutdownInProgress);
        if (command.ExpectedRevision() != snapshot_.revision)
            return StateFailure<DestructionStateTransition>(DestructionErrors::StaleRevision);
        if (snapshot_.phase == DestructionStatePhase::Destroyed)
            return StateFailure<DestructionStateTransition>(DestructionErrors::StateTerminal);

        auto nextRevision = AdvanceDestructionStateRevision(snapshot_.revision);
        if (nextRevision.HasError())
            return Result<DestructionStateTransition>::Failure(nextRevision.ErrorValue());

        auto successor = snapshot_;
        successor.revision = nextRevision.Value();
        if (command.Kind() == DestructionStateCommandKind::Destroy) {
            successor.health = 0.0F;
        } else {
            successor.health = command.DamageAmount() >= snapshot_.health ? 0.0F : snapshot_.health - command.DamageAmount();
        }
        successor.phase = PhaseForHealth(successor.health, healthPolicy_);
        return Result<DestructionStateTransition>::Success(
            DestructionStateTransition{command, snapshot_, std::move(successor), DestructionTransitionStatus::Prepared});
    }

    /** @copydoc DestructionStateMachine::Commit */
    Result<DestructionStateMachine> DestructionStateMachine::Commit(const DestructionStateTransition &transition) const {
        if (const auto access = ValidateDestructionCommandAccess(transition.Command().Id(), snapshot_.target); access.HasError())
            return Result<DestructionStateMachine>::Failure(access.ErrorValue());
        const auto sequence = ClassifyCommandSequence(lastCommand_, transition.Command());
        if (sequence.HasError())
            return Result<DestructionStateMachine>::Failure(sequence.ErrorValue());
        if (sequence.Value() == CommandSequenceDisposition::ExactDuplicate)
            return Result<DestructionStateMachine>::Success(*this);
        if (!admissionOpen_)
            return StateFailure<DestructionStateMachine>(DestructionErrors::ShutdownInProgress);
        if (const auto validation = ValidatePreparedTransition(snapshot_, transition); validation.HasError())
            return Result<DestructionStateMachine>::Failure(validation.ErrorValue());

        auto canonical = Prepare(transition.Command());
        if (canonical.HasError())
            return Result<DestructionStateMachine>::Failure(canonical.ErrorValue());
        if (canonical.Value().Successor() != transition.Successor())
            return StateFailure<DestructionStateMachine>(DestructionErrors::StateInvalid);
        return Result<DestructionStateMachine>::Success(
            DestructionStateMachine{transition.Successor(), healthPolicy_, transition.Command(), true});
    }

    /** @copydoc DestructionStateMachine::Replace */
    Result<DestructionStateMachine> DestructionStateMachine::Replace(const DestructionHandle replacement,
                                                                     const DestructibleDescriptor &descriptor) const {
        if (!admissionOpen_)
            return StateFailure<DestructionStateMachine>(DestructionErrors::ShutdownInProgress);
        if (const auto targetResult = ValidateDescriptorTarget(descriptor, replacement); targetResult.HasError())
            return Result<DestructionStateMachine>::Failure(targetResult.ErrorValue());
        if (replacement.world != snapshot_.target.world || replacement.destructible != snapshot_.target.destructible)
            return StateFailure<DestructionStateMachine>(DestructionErrors::IdentityUnknown);

        auto nextGeneration = AdvanceDestructionGeneration(snapshot_.target.generation);
        if (nextGeneration.HasError())
            return Result<DestructionStateMachine>::Failure(nextGeneration.ErrorValue());
        if (replacement.generation != nextGeneration.Value())
            return StateFailure<DestructionStateMachine>(DestructionErrors::StaleGeneration);

        auto initialRevision = DestructionStateRevision::Create(1);
        if (initialRevision.HasError())
            return Result<DestructionStateMachine>::Failure(initialRevision.ErrorValue());
        return Create(descriptor, replacement, initialRevision.Value());
    }

    /** @copydoc DestructionStateMachine::BeginShutdown */
    DestructionStateMachine DestructionStateMachine::BeginShutdown() const noexcept {
        auto closed = *this;
        closed.admissionOpen_ = false;
        return closed;
    }
}  // namespace Horo::Destruction
