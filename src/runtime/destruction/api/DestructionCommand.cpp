#include "Horo/Destruction/DestructionCommand.h"

#include "Horo/Destruction/DestructionErrors.h"

#include <cmath>
#include <utility>

namespace Horo::Destruction {
    namespace {
        constexpr std::uint32_t KnownCapabilityBits =
            (std::uint32_t{1} << static_cast<std::uint8_t>(DestructionCommandCapability::Count)) - 1U;

        template <typename T> [[nodiscard]] Result<T> Failure(const ErrorCodeDescriptor &descriptor) {
            return Result<T>::Failure(MakeError(descriptor));
        }

        [[nodiscard]] bool PositiveFinite(const float value) noexcept {
            return std::isfinite(value) && value > 0.0F;
        }

        [[nodiscard]] bool ExceedsHardLimit(const float value, const float hardLimit) noexcept {
            return value > hardLimit;
        }

        [[nodiscard]] Result<void> ValidateBoundedScalar(const float value, const float hardLimit) {
            if (!PositiveFinite(value))
                return Failure<void>(DestructionErrors::CommandInvalid);
            if (ExceedsHardLimit(value, hardLimit))
                return Failure<void>(DestructionErrors::CommandLimitExceeded);
            return Result<void>::Success();
        }

        [[nodiscard]] Result<void> ValidateBoundedPair(const float first, const float firstLimit, const float second,
                                                       const float secondLimit) {
            if (const auto validation = ValidateBoundedScalar(first, firstLimit); validation.HasError())
                return validation;
            return ValidateBoundedScalar(second, secondLimit);
        }

        [[nodiscard]] bool NonZeroFinite(const Math::Vec3 value) noexcept {
            if (!Math::IsFinite(value))
                return false;
            const float magnitudeSquared = value.x * value.x + value.y * value.y + value.z * value.z;
            return std::isfinite(magnitudeSquared) && magnitudeSquared > 0.0F;
        }

        [[nodiscard]] bool IsKnown(const DestructionExplosionFalloff falloff) noexcept {
            return falloff == DestructionExplosionFalloff::Constant || falloff == DestructionExplosionFalloff::Linear;
        }

        [[nodiscard]] bool IsKnown(const DestructionScriptIntent intent) noexcept {
            return intent == DestructionScriptIntent::ApplyDamage || intent == DestructionScriptIntent::Fracture;
        }

        [[nodiscard]] Result<void> ValidatePayload(const DestructionCommandKind kind, const DestructionCommandPayload &payload) {
            switch (kind) {
                case DestructionCommandKind::Impact:
                case DestructionCommandKind::Collision:
                    if (!Math::IsFinite(payload.position) || !NonZeroFinite(payload.direction))
                        return Failure<void>(DestructionErrors::CommandInvalid);
                    return ValidateBoundedPair(payload.impulse, DestructionCommandHardLimits::Impulse, payload.damage,
                                               DestructionCommandHardLimits::Damage);
                case DestructionCommandKind::Explosion:
                    if (!Math::IsFinite(payload.position) || !IsKnown(payload.falloff))
                        return Failure<void>(DestructionErrors::CommandInvalid);
                    return ValidateBoundedPair(payload.radius, DestructionCommandHardLimits::ExplosionRadius, payload.damage,
                                               DestructionCommandHardLimits::Damage);
                case DestructionCommandKind::Damage:
                    return ValidateBoundedScalar(payload.damage, DestructionCommandHardLimits::Damage);
                case DestructionCommandKind::Script:
                    if (!IsKnown(payload.scriptIntent) ||
                        (payload.scriptIntent == DestructionScriptIntent::Fracture && payload.damage != 0.0F))
                        return Failure<void>(DestructionErrors::CommandInvalid);
                    if (payload.scriptIntent == DestructionScriptIntent::Fracture)
                        return Result<void>::Success();
                    return ValidateBoundedScalar(payload.damage, DestructionCommandHardLimits::Damage);
            }
            return Failure<void>(DestructionErrors::CommandInvalid);
        }

        [[nodiscard]] Result<void> ValidateHeader(const DestructionCommandHeader &header) {
            if (header.contractVersion != CurrentDestructionCommandContractVersion || !header.id.IsValid() ||
                !header.expectedRevision.IsValid() || !header.capabilityRevision.IsValid() || header.eligibleSimulationTick == 0U ||
                !header.authority.IsValid())
                return Failure<void>(DestructionErrors::CommandInvalid);
            return Result<void>::Success();
        }

        [[nodiscard]] bool IsFractureIntent(const DestructionCommand &command) noexcept {
            return command.Kind() == DestructionCommandKind::Script && command.Payload().scriptIntent == DestructionScriptIntent::Fracture;
        }

        [[nodiscard]] bool TriggerAllows(const DestructionTriggerPolicy policy, const DestructionCommand &command) noexcept {
            using enum DestructionTriggerPolicy;
            if (policy == ExplicitOnly)
                return IsFractureIntent(command);
            if (policy == AccumulatedDamage)
                return command.Kind() != DestructionCommandKind::Collision;
            return policy == DamageAndContact;
        }

        [[nodiscard]] bool OutcomeReasonCompatible(const DestructionCommandOutcome outcome,
                                                   const DestructionCommandTerminalReason reason) noexcept {
            using enum DestructionCommandOutcome;
            using enum DestructionCommandTerminalReason;
            switch (outcome) {
                case Succeeded:
                    return reason == None;
                case Rejected:
                    return reason == InvalidState || reason == AuthorityDenied || reason == LimitExceeded || reason == StaleGeneration ||
                           reason == StaleRevision || reason == Replaced || reason == Shutdown;
                case Cancelled:
                    return reason == Rollback || reason == Replaced || reason == Shutdown;
                case Unsupported:
                    return reason == CapabilityUnavailable;
                case Failed:
                    return reason == ExecutionFailure || reason == Rollback;
            }
            return false;
        }
    }  // namespace

    /** @copydoc DestructionAuthorityGrant::IsValid */
    bool DestructionAuthorityGrant::IsValid() const noexcept {
        return authority.IsValid() && revision.IsValid() && capabilities.bits != 0U && (capabilities.bits & ~KnownCapabilityBits) == 0U;
    }

    /** @copydoc DestructionCommandLimits::IsValid */
    bool DestructionCommandLimits::IsValid() const noexcept {
        return PositiveFinite(maximumDamage) && maximumDamage <= DestructionCommandHardLimits::Damage && PositiveFinite(maximumImpulse) &&
               maximumImpulse <= DestructionCommandHardLimits::Impulse && PositiveFinite(maximumExplosionRadius) &&
               maximumExplosionRadius <= DestructionCommandHardLimits::ExplosionRadius;
    }

    DestructionCommand::DestructionCommand(DestructionCommandHeader header, const DestructionCommandKind kind,
                                           DestructionCommandPayload payload) noexcept
        : header_(std::move(header)), kind_(kind), payload_(std::move(payload)) {}

    /** @copydoc DestructionCommand::Create */
    Result<DestructionCommand> DestructionCommand::Create(DestructionCommandHeader header, const DestructionCommandKind kind,
                                                          DestructionCommandPayload payload) {
        if (const auto validation = ValidateHeader(header); validation.HasError())
            return Result<DestructionCommand>::Failure(validation.ErrorValue());
        if (const auto validation = ValidatePayload(kind, payload); validation.HasError())
            return Result<DestructionCommand>::Failure(validation.ErrorValue());
        return Result<DestructionCommand>::Success(DestructionCommand{std::move(header), kind, std::move(payload)});
    }

    /** @copydoc DestructionCommand::Impact */
    Result<DestructionCommand> DestructionCommand::Impact(DestructionCommandHeader header, const Math::Vec3 position,
                                                          const Math::Vec3 direction, const float impulse, const float damage) {
        return Create(std::move(header), DestructionCommandKind::Impact,
                      {.position = position, .direction = direction, .damage = damage, .impulse = impulse});
    }

    /** @copydoc DestructionCommand::Explosion */
    Result<DestructionCommand> DestructionCommand::Explosion(DestructionCommandHeader header, const Math::Vec3 center, const float radius,
                                                             const float damage, const DestructionExplosionFalloff falloff) {
        return Create(std::move(header), DestructionCommandKind::Explosion,
                      {.position = center, .damage = damage, .radius = radius, .falloff = falloff});
    }

    /** @copydoc DestructionCommand::Collision */
    Result<DestructionCommand> DestructionCommand::Collision(DestructionCommandHeader header, const Math::Vec3 position,
                                                             const Math::Vec3 normal, const float impulse, const float damage) {
        return Create(std::move(header), DestructionCommandKind::Collision,
                      {.position = position, .direction = normal, .damage = damage, .impulse = impulse});
    }

    /** @copydoc DestructionCommand::Damage */
    Result<DestructionCommand> DestructionCommand::Damage(DestructionCommandHeader header, const float damage) {
        return Create(std::move(header), DestructionCommandKind::Damage, {.damage = damage});
    }

    /** @copydoc DestructionCommand::Script */
    Result<DestructionCommand> DestructionCommand::Script(DestructionCommandHeader header, const DestructionScriptIntent intent,
                                                          const float damage) {
        return Create(std::move(header), DestructionCommandKind::Script, {.damage = damage, .scriptIntent = intent});
    }

    /** @copydoc DestructionCommand::Header */
    const DestructionCommandHeader &DestructionCommand::Header() const noexcept {
        return header_;
    }

    /** @copydoc DestructionCommand::Kind */
    DestructionCommandKind DestructionCommand::Kind() const noexcept {
        return kind_;
    }

    /** @copydoc DestructionCommand::Payload */
    const DestructionCommandPayload &DestructionCommand::Payload() const noexcept {
        return payload_;
    }

    /** @copydoc DestructionCommand::RequiredCapability */
    DestructionCommandCapability DestructionCommand::RequiredCapability() const noexcept {
        switch (kind_) {
            case DestructionCommandKind::Impact:
                return DestructionCommandCapability::Impact;
            case DestructionCommandKind::Explosion:
                return DestructionCommandCapability::Explosion;
            case DestructionCommandKind::Collision:
                return DestructionCommandCapability::Collision;
            case DestructionCommandKind::Script:
                return payload_.scriptIntent == DestructionScriptIntent::Fracture ? DestructionCommandCapability::ExplicitFracture
                                                                                  : DestructionCommandCapability::Damage;
            case DestructionCommandKind::Damage:
                return DestructionCommandCapability::Damage;
        }
        return DestructionCommandCapability::Count;
    }

    DestructionCommandResult::DestructionCommandResult(DestructionCommandId command, const DestructionStateRevision sourceRevision,
                                                       const DestructionStateRevision terminalRevision,
                                                       const DestructionCommandOutcome outcome,
                                                       const DestructionCommandTerminalReason reason) noexcept
        : command_(std::move(command)), sourceRevision_(sourceRevision), terminalRevision_(terminalRevision), outcome_(outcome),
          reason_(reason) {}

    /** @copydoc DestructionCommandResult::Succeeded */
    Result<DestructionCommandResult> DestructionCommandResult::Succeeded(DestructionCommandId command,
                                                                         const DestructionStateRevision sourceRevision,
                                                                         const DestructionStateRevision committedRevision) {
        if (!command.IsValid() || !sourceRevision.IsValid() || !committedRevision.IsValid())
            return Failure<DestructionCommandResult>(DestructionErrors::CommandResultInvalid);
        if (const auto next = AdvanceDestructionStateRevision(sourceRevision); next.HasError() || next.Value() != committedRevision)
            return Failure<DestructionCommandResult>(DestructionErrors::CommandResultInvalid);
        return Result<DestructionCommandResult>::Success(DestructionCommandResult{std::move(command), sourceRevision, committedRevision,
                                                                                  DestructionCommandOutcome::Succeeded,
                                                                                  DestructionCommandTerminalReason::None});
    }

    /** @copydoc DestructionCommandResult::Terminated */
    Result<DestructionCommandResult> DestructionCommandResult::Terminated(DestructionCommandId command,
                                                                          const DestructionStateRevision sourceRevision,
                                                                          const DestructionStateRevision observedRevision,
                                                                          const DestructionCommandOutcome outcome,
                                                                          const DestructionCommandTerminalReason reason) {
        if (!command.IsValid() || !sourceRevision.IsValid() || !observedRevision.IsValid() ||
            outcome == DestructionCommandOutcome::Succeeded || !OutcomeReasonCompatible(outcome, reason))
            return Failure<DestructionCommandResult>(DestructionErrors::CommandResultInvalid);
        return Result<DestructionCommandResult>::Success(
            DestructionCommandResult{std::move(command), sourceRevision, observedRevision, outcome, reason});
    }

    /** @copydoc AdmitDestructionCommand */
    Result<void> AdmitDestructionCommand(const DestructionCommand &command, const DestructionStateSnapshot &snapshot,
                                         const DestructibleDescriptor &descriptor, const DestructionCapabilityRevision capabilityRevision,
                                         const DestructionAuthorityGrant &currentAuthority, const DestructionCommandLimits &limits,
                                         const bool admissionOpen) {
        if (!snapshot.target.IsValid() || !snapshot.revision.IsValid() || !capabilityRevision.IsValid() || !currentAuthority.IsValid() ||
            !limits.IsValid())
            return Failure<void>(DestructionErrors::CommandInvalid);
        if (const auto target = ValidateDestructionCommandAccess(command.Header().id, snapshot.target); target.HasError())
            return target;
        if (!admissionOpen)
            return Failure<void>(DestructionErrors::ShutdownInProgress);
        if (command.Header().expectedRevision != snapshot.revision)
            return Failure<void>(DestructionErrors::StaleRevision);
        if (command.Header().capabilityRevision != capabilityRevision)
            return Failure<void>(DestructionErrors::CommandUnsupported);
        if (command.Header().authority != currentAuthority)
            return Failure<void>(DestructionErrors::CommandAuthorityDenied);
        if (snapshot.phase == DestructionStatePhase::Destroyed)
            return Failure<void>(DestructionErrors::StateTerminal);
        if (descriptor.Data().destructible != snapshot.target.destructible || descriptor.Data().content != snapshot.content ||
            descriptor.Data().configurationRevision != snapshot.configurationRevision)
            return Failure<void>(DestructionErrors::StaleConfiguration);
        if (const auto required = command.RequiredCapability(); !command.Header().authority.capabilities.Contains(required))
            return Failure<void>(DestructionErrors::CommandAuthorityDenied);
        if (command.Kind() == DestructionCommandKind::Script &&
            !command.Header().authority.capabilities.Contains(DestructionCommandCapability::Script))
            return Failure<void>(DestructionErrors::CommandAuthorityDenied);
        if (!TriggerAllows(descriptor.Data().behavior.trigger, command) ||
            (IsFractureIntent(command) && !snapshot.effectiveFeatures.Contains(DestructionFeature::PreCookedFracture)))
            return Failure<void>(DestructionErrors::CommandUnsupported);

        if (const auto &payload = command.Payload(); payload.damage > limits.maximumDamage || payload.impulse > limits.maximumImpulse ||
                                                     payload.radius > limits.maximumExplosionRadius)
            return Failure<void>(DestructionErrors::CommandLimitExceeded);
        return Result<void>::Success();
    }

    /** @copydoc ToDestructionStateCommand */
    Result<DestructionStateCommand> ToDestructionStateCommand(const DestructionCommand &command) {
        if (IsFractureIntent(command))
            return DestructionStateCommand::Destroy(command.Header().id, command.Header().expectedRevision);
        return DestructionStateCommand::Damage(command.Header().id, command.Header().expectedRevision, command.Payload().damage);
    }

    /** @copydoc ResolveDestructionCommandCompletion */
    Result<DestructionCommandResult> ResolveDestructionCommandCompletion(
        const DestructionCommand &command, const DestructionStateRevision sourceRevision, const DestructionStateRevision committedRevision,
        const DestructionHandle currentTarget, const DestructionStateRevision currentRevision, const bool admissionOpen) {
        if (!sourceRevision.IsValid() || sourceRevision != command.Header().expectedRevision || !committedRevision.IsValid() ||
            !currentTarget.IsValid() || !currentRevision.IsValid())
            return Failure<DestructionCommandResult>(DestructionErrors::CommandResultInvalid);
        if (command.Header().id.target.world != currentTarget.world ||
            command.Header().id.target.destructible != currentTarget.destructible)
            return Failure<DestructionCommandResult>(DestructionErrors::CommandResultInvalid);
        if (command.Header().id.target.generation != currentTarget.generation)
            return DestructionCommandResult::Terminated(command.Header().id, sourceRevision, currentRevision,
                                                        DestructionCommandOutcome::Rejected, DestructionCommandTerminalReason::Replaced);
        if (sourceRevision != currentRevision)
            return DestructionCommandResult::Terminated(command.Header().id, sourceRevision, currentRevision,
                                                        DestructionCommandOutcome::Rejected,
                                                        DestructionCommandTerminalReason::StaleRevision);
        if (!admissionOpen)
            return DestructionCommandResult::Terminated(command.Header().id, sourceRevision, currentRevision,
                                                        DestructionCommandOutcome::Cancelled, DestructionCommandTerminalReason::Shutdown);
        return DestructionCommandResult::Succeeded(command.Header().id, sourceRevision, committedRevision);
    }
}  // namespace Horo::Destruction
