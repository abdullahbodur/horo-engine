#include "Horo/Extensions/ExtensionActivationState.h"

#include "Horo/Extensions/ExtensionErrors.h"

#include <limits>

namespace Horo::Extensions {
    namespace {
        constexpr std::size_t MaximumFailureMessageBytes = 1024;

        [[nodiscard]] Result<ExtensionLifecycleTransition> Reject(const ErrorCodeDescriptor &descriptor) {
            return Result<ExtensionLifecycleTransition>::Failure(MakeError(descriptor));
        }

        [[nodiscard]] ExtensionLifecycleOwner RequiredOwner(const ExtensionLifecycleAction action) noexcept {
            using enum ExtensionLifecycleAction;
            using enum ExtensionLifecycleOwner;
            switch (action) {
                case GrantTrust:
                case RevokeTrust:
                    return TrustService;
                case PublishInstalledComposition:
                case RemoveInstalledComposition:
                case EnableForProject:
                case DisableForProject:
                case MarkCompatible:
                case MarkIncompatible:
                case MarkUnsupportedInHostProfile:
                case ReplaceInstalledComposition:
                    return PackageLifecycleService;
                case MarkLoaded:
                case MarkActive:
                case MarkInactive:
                    return ExtensionHost;
                case RecordActivationFailure:
                    return PackageLifecycleService;
            }
            return PackageLifecycleService;
        }

        [[nodiscard]] bool ValidCurrent(const ExtensionActivationProjection &current) noexcept {
            if (current.stateRevision == 0 || current.installedComposition.size() > 512 || current.trustedComposition.size() > 512 ||
                current.runtimeComposition.size() > 512 || current.failure.message.size() > MaximumFailureMessageBytes)
                return false;
            if (current.installation == ExtensionInstallationState::NotInstalled)
                return current.installedComposition.empty() && current.trust == ExtensionTrustState::Untrusted &&
                       current.trustedComposition.empty() && current.compatibility == ExtensionHostCompatibilityState::NotEvaluated &&
                       current.runtime == ExtensionRuntimeActivityState::Inactive && current.runtimeComposition.empty() &&
                       current.outcome == ExtensionActivationOutcome::NotAttempted && current.failure.Empty();
            if (current.installedComposition.empty())
                return false;
            if (current.trust == ExtensionTrustState::Trusted && current.trustedComposition != current.installedComposition)
                return false;
            if (current.trust == ExtensionTrustState::Untrusted && !current.trustedComposition.empty())
                return false;
            if (current.runtime != ExtensionRuntimeActivityState::Inactive && current.runtimeComposition.empty())
                return false;
            if (current.runtime == ExtensionRuntimeActivityState::Inactive && !current.runtimeComposition.empty())
                return false;
            if (current.outcome == ExtensionActivationOutcome::Failed)
                return !current.failure.Empty();
            return current.failure.Empty() && current.failure.message.empty();
        }

        [[nodiscard]] bool ValidCompositionEvidence(const std::string_view value) noexcept {
            return !value.empty() && value.size() <= 512;
        }

        [[nodiscard]] Result<void> InvalidTransition() {
            return Result<void>::Failure(MakeError(ExtensionErrors::LifecycleTransitionInvalid));
        }

        [[nodiscard]] Result<void> ApplyInstallation(ExtensionActivationProjection &next, const ExtensionLifecycleCommand &command) {
            using enum ExtensionLifecycleAction;
            using enum ExtensionInstallationState;
            switch (command.action) {
                case PublishInstalledComposition:
                    if (next.installation != NotInstalled || !ValidCompositionEvidence(command.composition))
                        return InvalidTransition();
                    next.installation = Installed;
                    next.installedComposition = command.composition;
                    return Result<void>::Success();
                case RemoveInstalledComposition:
                    if (next.installation != Installed || next.runtime != ExtensionRuntimeActivityState::Inactive)
                        return InvalidTransition();
                    next.installation = NotInstalled;
                    next.trust = ExtensionTrustState::Untrusted;
                    next.compatibility = ExtensionHostCompatibilityState::NotEvaluated;
                    next.outcome = ExtensionActivationOutcome::NotAttempted;
                    next.failure = {};
                    next.installedComposition.clear();
                    next.trustedComposition.clear();
                    return Result<void>::Success();
                default:
                    return InvalidTransition();
            }
        }

        [[nodiscard]] Result<void> ApplyPolicy(ExtensionActivationProjection &next, const ExtensionLifecycleCommand &command) {
            using enum ExtensionLifecycleAction;
            switch (command.action) {
                case GrantTrust:
                    if (next.HasCurrentTrust() || !ValidCompositionEvidence(command.composition) ||
                        command.composition != next.installedComposition)
                        return InvalidTransition();
                    next.trust = ExtensionTrustState::Trusted;
                    next.trustedComposition = command.composition;
                    return Result<void>::Success();
                case RevokeTrust:
                    if (next.trust == ExtensionTrustState::Untrusted)
                        return InvalidTransition();
                    next.trust = ExtensionTrustState::Untrusted;
                    next.trustedComposition.clear();
                    return Result<void>::Success();
                case EnableForProject:
                    if (next.enablement == ExtensionEnablementState::Enabled)
                        return InvalidTransition();
                    next.enablement = ExtensionEnablementState::Enabled;
                    return Result<void>::Success();
                case DisableForProject:
                    if (next.enablement == ExtensionEnablementState::Disabled)
                        return InvalidTransition();
                    next.enablement = ExtensionEnablementState::Disabled;
                    return Result<void>::Success();
                default:
                    return InvalidTransition();
            }
        }

        [[nodiscard]] Result<void> ApplyCompatibility(ExtensionActivationProjection &next, const ExtensionLifecycleAction action) {
            ExtensionHostCompatibilityState compatibility;
            using enum ExtensionLifecycleAction;
            using enum ExtensionHostCompatibilityState;
            switch (action) {
                case MarkCompatible:
                    compatibility = Compatible;
                    break;
                case MarkIncompatible:
                    compatibility = Incompatible;
                    break;
                case MarkUnsupportedInHostProfile:
                    compatibility = UnsupportedInHostProfile;
                    break;
                default:
                    return InvalidTransition();
            }
            if (next.compatibility == compatibility)
                return InvalidTransition();
            next.compatibility = compatibility;
            return Result<void>::Success();
        }

        [[nodiscard]] Result<void> ApplyRuntime(ExtensionActivationProjection &next, const ExtensionLifecycleCommand &command) {
            using enum ExtensionLifecycleAction;
            switch (command.action) {
                case MarkLoaded:
                    if (next.DesiredActivation() != ExtensionDesiredActivation::Active || !next.HasCurrentTrust() ||
                        next.compatibility != ExtensionHostCompatibilityState::Compatible ||
                        next.runtime != ExtensionRuntimeActivityState::Inactive || command.composition != next.installedComposition)
                        return InvalidTransition();
                    next.runtime = ExtensionRuntimeActivityState::Loaded;
                    next.runtimeComposition = command.composition;
                    next.outcome = ExtensionActivationOutcome::NotAttempted;
                    next.failure = {};
                    return Result<void>::Success();
                case MarkActive:
                    if (next.DesiredActivation() != ExtensionDesiredActivation::Active || !next.HasCurrentTrust() ||
                        next.compatibility != ExtensionHostCompatibilityState::Compatible ||
                        next.runtime != ExtensionRuntimeActivityState::Loaded || command.composition != next.installedComposition ||
                        command.composition != next.runtimeComposition || command.activationGeneration <= next.activationGeneration)
                        return InvalidTransition();
                    next.runtime = ExtensionRuntimeActivityState::Active;
                    next.activationGeneration = command.activationGeneration;
                    next.outcome = ExtensionActivationOutcome::Succeeded;
                    next.failure = {};
                    return Result<void>::Success();
                case MarkInactive:
                    if (next.runtime == ExtensionRuntimeActivityState::Inactive)
                        return InvalidTransition();
                    next.runtime = ExtensionRuntimeActivityState::Inactive;
                    next.runtimeComposition.clear();
                    return Result<void>::Success();
                default:
                    return InvalidTransition();
            }
        }

        [[nodiscard]] Result<void> ApplyOutcomeOrReplacement(ExtensionActivationProjection &next,
                                                             const ExtensionLifecycleCommand &command) {
            using enum ExtensionLifecycleAction;
            switch (command.action) {
                case RecordActivationFailure:
                    if (next.DesiredActivation() != ExtensionDesiredActivation::Active || command.failure.Empty() ||
                        command.failure.message.size() > MaximumFailureMessageBytes ||
                        next.runtime == ExtensionRuntimeActivityState::Active)
                        return InvalidTransition();
                    next.runtime = ExtensionRuntimeActivityState::Inactive;
                    next.runtimeComposition.clear();
                    next.outcome = ExtensionActivationOutcome::Failed;
                    next.failure = command.failure;
                    return Result<void>::Success();
                case ReplaceInstalledComposition:
                    if (!ValidCompositionEvidence(command.composition) || command.composition == next.installedComposition)
                        return InvalidTransition();
                    next.installedComposition = command.composition;
                    next.trust = ExtensionTrustState::Untrusted;
                    next.trustedComposition.clear();
                    next.compatibility = ExtensionHostCompatibilityState::NotEvaluated;
                    next.outcome = ExtensionActivationOutcome::NotAttempted;
                    next.failure = {};
                    return Result<void>::Success();
                default:
                    return InvalidTransition();
            }
        }

        [[nodiscard]] Result<void> ApplyAction(ExtensionActivationProjection &next, const ExtensionLifecycleCommand &command) {
            using enum ExtensionLifecycleAction;
            switch (command.action) {
                case PublishInstalledComposition:
                case RemoveInstalledComposition:
                    return ApplyInstallation(next, command);
                case GrantTrust:
                case RevokeTrust:
                case EnableForProject:
                case DisableForProject:
                    return ApplyPolicy(next, command);
                case MarkCompatible:
                case MarkIncompatible:
                case MarkUnsupportedInHostProfile:
                    return ApplyCompatibility(next, command.action);
                case MarkLoaded:
                case MarkActive:
                case MarkInactive:
                    return ApplyRuntime(next, command);
                case RecordActivationFailure:
                case ReplaceInstalledComposition:
                    return ApplyOutcomeOrReplacement(next, command);
            }
            return InvalidTransition();
        }
    }  // namespace

    /** @copydoc ExtensionActivationProjection::DesiredActivation */
    ExtensionDesiredActivation ExtensionActivationProjection::DesiredActivation() const noexcept {
        return installation == ExtensionInstallationState::Installed && enablement == ExtensionEnablementState::Enabled && HasCurrentTrust()
                   ? ExtensionDesiredActivation::Active
                   : ExtensionDesiredActivation::Inactive;
    }

    /** @copydoc ExtensionActivationProjection::HasCurrentTrust */
    bool ExtensionActivationProjection::HasCurrentTrust() const noexcept {
        return trust == ExtensionTrustState::Trusted && trustedComposition == installedComposition && !installedComposition.empty();
    }

    /** @copydoc ExtensionActivationProjection::RestartReason */
    ExtensionRestartReason ExtensionActivationProjection::RestartReason() const noexcept {
        using enum ExtensionRestartReason;
        if (runtime != ExtensionRuntimeActivityState::Inactive && runtimeComposition != installedComposition)
            return ReplacementRequired;
        if (runtime != ExtensionRuntimeActivityState::Inactive && DesiredActivation() == ExtensionDesiredActivation::Inactive)
            return DeactivationRequired;
        if (enablement == ExtensionEnablementState::Disabled)
            return None;
        if (installation == ExtensionInstallationState::NotInstalled)
            return InstallationRequired;
        if (enablement == ExtensionEnablementState::Enabled && !HasCurrentTrust())
            return TrustRequired;
        if (outcome == ExtensionActivationOutcome::Failed)
            return ActivationFailed;
        if (compatibility == ExtensionHostCompatibilityState::NotEvaluated)
            return CompatibilityRequired;
        if (compatibility == ExtensionHostCompatibilityState::Incompatible ||
            compatibility == ExtensionHostCompatibilityState::UnsupportedInHostProfile)
            return HostIncompatible;
        if (runtime == ExtensionRuntimeActivityState::Inactive && DesiredActivation() == ExtensionDesiredActivation::Active)
            return ActivationRequired;
        return None;
    }

    /** @copydoc TransitionExtensionActivation */
    Result<ExtensionLifecycleTransition> TransitionExtensionActivation(const ExtensionActivationProjection &current,
                                                                       const ExtensionLifecycleCommand &command) {
        if (!ValidCurrent(current) || command.owner != RequiredOwner(command.action))
            return Reject(ExtensionErrors::LifecycleTransitionInvalid);
        if (command.expectedRevision != current.stateRevision)
            return Reject(ExtensionErrors::LifecycleRevisionStale);
        if (current.stateRevision == std::numeric_limits<std::uint64_t>::max())
            return Reject(ExtensionErrors::LifecycleCapacityExceeded);
        if (command.action == ExtensionLifecycleAction::MarkActive &&
            current.activationGeneration == std::numeric_limits<std::uint64_t>::max())
            return Reject(ExtensionErrors::LifecycleCapacityExceeded);

        ExtensionActivationProjection next = current;
        if (auto applied = ApplyAction(next, command); applied.HasError() || !ValidCurrent(next))
            return Reject(ExtensionErrors::LifecycleTransitionInvalid);
        ++next.stateRevision;
        const auto resultingGeneration = next.activationGeneration;
        return Result<ExtensionLifecycleTransition>::Success({
            .next = std::move(next),
            .audit = {.action = command.action,
                      .owner = command.owner,
                      .priorRevision = current.stateRevision,
                      .resultingRevision = current.stateRevision + 1,
                      .activationGeneration = resultingGeneration},
        });
    }

    /** @copydoc ExtensionActivationAuditHistory::Append */
    Result<void> ExtensionActivationAuditHistory::Append(ExtensionActivationAuditRecord record) {
        if (records_.size() >= capacity_)
            return Result<void>::Failure(MakeError(ExtensionErrors::LifecycleCapacityExceeded));
        if (record.priorRevision == std::numeric_limits<std::uint64_t>::max() || record.resultingRevision != record.priorRevision + 1)
            return Result<void>::Failure(MakeError(ExtensionErrors::LifecycleTransitionInvalid));
        if (!records_.empty() && record.priorRevision != records_.back().resultingRevision)
            return Result<void>::Failure(MakeError(ExtensionErrors::LifecycleRevisionStale));
        records_.push_back(std::move(record));
        return Result<void>::Success();
    }
}  // namespace Horo::Extensions
