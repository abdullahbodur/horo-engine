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
            switch (action) {
                case ExtensionLifecycleAction::GrantTrust:
                case ExtensionLifecycleAction::RevokeTrust:
                    return ExtensionLifecycleOwner::TrustService;
                case ExtensionLifecycleAction::PublishInstalledComposition:
                case ExtensionLifecycleAction::RemoveInstalledComposition:
                case ExtensionLifecycleAction::EnableForProject:
                case ExtensionLifecycleAction::DisableForProject:
                case ExtensionLifecycleAction::MarkCompatible:
                case ExtensionLifecycleAction::MarkIncompatible:
                case ExtensionLifecycleAction::MarkUnsupportedInHostProfile:
                case ExtensionLifecycleAction::ReplaceInstalledComposition:
                    return ExtensionLifecycleOwner::PackageLifecycleService;
                case ExtensionLifecycleAction::MarkLoaded:
                case ExtensionLifecycleAction::MarkActive:
                case ExtensionLifecycleAction::MarkInactive:
                    return ExtensionLifecycleOwner::ExtensionHost;
                case ExtensionLifecycleAction::RecordActivationFailure:
                    return ExtensionLifecycleOwner::PackageLifecycleService;
            }
            return ExtensionLifecycleOwner::PackageLifecycleService;
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

        [[nodiscard]] Result<void> ApplyAction(ExtensionActivationProjection &next, const ExtensionLifecycleCommand &command) {
            using enum ExtensionLifecycleAction;
            switch (command.action) {
                case PublishInstalledComposition:
                    if (next.installation != ExtensionInstallationState::NotInstalled || !ValidCompositionEvidence(command.composition))
                        break;
                    next.installation = ExtensionInstallationState::Installed;
                    next.installedComposition = command.composition;
                    return Result<void>::Success();
                case RemoveInstalledComposition:
                    if (next.installation != ExtensionInstallationState::Installed ||
                        next.runtime != ExtensionRuntimeActivityState::Inactive)
                        break;
                    next.installation = ExtensionInstallationState::NotInstalled;
                    next.trust = ExtensionTrustState::Untrusted;
                    next.compatibility = ExtensionHostCompatibilityState::NotEvaluated;
                    next.outcome = ExtensionActivationOutcome::NotAttempted;
                    next.failure = {};
                    next.installedComposition.clear();
                    next.trustedComposition.clear();
                    return Result<void>::Success();
                case GrantTrust:
                    if (next.HasCurrentTrust() || !ValidCompositionEvidence(command.composition) ||
                        command.composition != next.installedComposition)
                        break;
                    next.trust = ExtensionTrustState::Trusted;
                    next.trustedComposition = command.composition;
                    return Result<void>::Success();
                case RevokeTrust:
                    if (next.trust == ExtensionTrustState::Untrusted)
                        break;
                    next.trust = ExtensionTrustState::Untrusted;
                    next.trustedComposition.clear();
                    return Result<void>::Success();
                case EnableForProject:
                    if (next.enablement == ExtensionEnablementState::Enabled)
                        break;
                    next.enablement = ExtensionEnablementState::Enabled;
                    return Result<void>::Success();
                case DisableForProject:
                    if (next.enablement == ExtensionEnablementState::Disabled)
                        break;
                    next.enablement = ExtensionEnablementState::Disabled;
                    return Result<void>::Success();
                case MarkCompatible:
                    if (next.compatibility == ExtensionHostCompatibilityState::Compatible)
                        break;
                    next.compatibility = ExtensionHostCompatibilityState::Compatible;
                    return Result<void>::Success();
                case MarkIncompatible:
                    if (next.compatibility == ExtensionHostCompatibilityState::Incompatible)
                        break;
                    next.compatibility = ExtensionHostCompatibilityState::Incompatible;
                    return Result<void>::Success();
                case MarkUnsupportedInHostProfile:
                    if (next.compatibility == ExtensionHostCompatibilityState::UnsupportedInHostProfile)
                        break;
                    next.compatibility = ExtensionHostCompatibilityState::UnsupportedInHostProfile;
                    return Result<void>::Success();
                case MarkLoaded:
                    if (next.DesiredActivation() != ExtensionDesiredActivation::Active || !next.HasCurrentTrust() ||
                        next.compatibility != ExtensionHostCompatibilityState::Compatible ||
                        next.runtime != ExtensionRuntimeActivityState::Inactive || command.composition != next.installedComposition)
                        break;
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
                        break;
                    next.runtime = ExtensionRuntimeActivityState::Active;
                    next.activationGeneration = command.activationGeneration;
                    next.outcome = ExtensionActivationOutcome::Succeeded;
                    next.failure = {};
                    return Result<void>::Success();
                case MarkInactive:
                    if (next.runtime == ExtensionRuntimeActivityState::Inactive)
                        break;
                    next.runtime = ExtensionRuntimeActivityState::Inactive;
                    next.runtimeComposition.clear();
                    return Result<void>::Success();
                case RecordActivationFailure:
                    if (next.DesiredActivation() != ExtensionDesiredActivation::Active || command.failure.Empty() ||
                        command.failure.message.size() > MaximumFailureMessageBytes ||
                        next.runtime == ExtensionRuntimeActivityState::Active)
                        break;
                    next.runtime = ExtensionRuntimeActivityState::Inactive;
                    next.runtimeComposition.clear();
                    next.outcome = ExtensionActivationOutcome::Failed;
                    next.failure = command.failure;
                    return Result<void>::Success();
                case ReplaceInstalledComposition:
                    if (!ValidCompositionEvidence(command.composition) || command.composition == next.installedComposition)
                        break;
                    next.installedComposition = command.composition;
                    next.trust = ExtensionTrustState::Untrusted;
                    next.trustedComposition.clear();
                    next.compatibility = ExtensionHostCompatibilityState::NotEvaluated;
                    next.outcome = ExtensionActivationOutcome::NotAttempted;
                    next.failure = {};
                    return Result<void>::Success();
            }
            return Result<void>::Failure(MakeError(ExtensionErrors::LifecycleTransitionInvalid));
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
        if (runtime != ExtensionRuntimeActivityState::Inactive && runtimeComposition != installedComposition)
            return ExtensionRestartReason::ReplacementRequired;
        if (runtime != ExtensionRuntimeActivityState::Inactive && DesiredActivation() == ExtensionDesiredActivation::Inactive)
            return ExtensionRestartReason::DeactivationRequired;
        if (enablement == ExtensionEnablementState::Disabled)
            return ExtensionRestartReason::None;
        if (installation == ExtensionInstallationState::NotInstalled)
            return ExtensionRestartReason::InstallationRequired;
        if (enablement == ExtensionEnablementState::Enabled && !HasCurrentTrust())
            return ExtensionRestartReason::TrustRequired;
        if (outcome == ExtensionActivationOutcome::Failed)
            return ExtensionRestartReason::ActivationFailed;
        if (compatibility == ExtensionHostCompatibilityState::NotEvaluated)
            return ExtensionRestartReason::CompatibilityRequired;
        if (compatibility == ExtensionHostCompatibilityState::Incompatible ||
            compatibility == ExtensionHostCompatibilityState::UnsupportedInHostProfile)
            return ExtensionRestartReason::HostIncompatible;
        if (runtime == ExtensionRuntimeActivityState::Inactive && DesiredActivation() == ExtensionDesiredActivation::Active)
            return ExtensionRestartReason::ActivationRequired;
        return ExtensionRestartReason::None;
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
