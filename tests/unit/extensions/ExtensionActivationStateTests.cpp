#include "Horo/Extensions/ExtensionActivationState.h"
#include "Horo/Extensions/ExtensionErrors.h"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <limits>
#include <string>

namespace Horo::Extensions {
    namespace {
        [[nodiscard]] ExtensionActivationProjection Installed(std::string composition = "sha256:installed") {
            ExtensionActivationProjection state;
            state.installation = ExtensionInstallationState::Installed;
            state.installedComposition = std::move(composition);
            return state;
        }

        [[nodiscard]] ExtensionLifecycleOwner Owner(const ExtensionLifecycleAction action) {
            switch (action) {
                case ExtensionLifecycleAction::GrantTrust:
                case ExtensionLifecycleAction::RevokeTrust:
                    return ExtensionLifecycleOwner::TrustService;
                case ExtensionLifecycleAction::MarkLoaded:
                case ExtensionLifecycleAction::MarkActive:
                case ExtensionLifecycleAction::MarkInactive:
                    return ExtensionLifecycleOwner::ExtensionHost;
                case ExtensionLifecycleAction::RecordActivationFailure:
                    return ExtensionLifecycleOwner::PackageLifecycleService;
                default:
                    return ExtensionLifecycleOwner::PackageLifecycleService;
            }
        }

        [[nodiscard]] ExtensionLifecycleTransition Apply(const ExtensionActivationProjection &current,
                                                         const ExtensionLifecycleAction action, std::string composition = {},
                                                         const std::uint64_t generation = 0, ExtensionActivationFailure failure = {}) {
            auto applied = TransitionExtensionActivation(current, {.action = action,
                                                                   .owner = Owner(action),
                                                                   .expectedRevision = current.stateRevision,
                                                                   .activationGeneration = generation,
                                                                   .composition = std::move(composition),
                                                                   .failure = std::move(failure)});
            REQUIRE(applied.HasValue());
            return applied.Value();
        }

        template <typename T> void RequireError(const Result<T> &result, const ErrorCodeDescriptor &descriptor) {
            REQUIRE(result.HasError());
            CHECK(result.ErrorValue().code.Value() == descriptor.code.Value());
        }

        [[nodiscard]] ExtensionActivationProjection Ready() {
            auto state = Installed();
            state = Apply(state, ExtensionLifecycleAction::EnableForProject).next;
            state = Apply(state, ExtensionLifecycleAction::GrantTrust, state.installedComposition).next;
            return Apply(state, ExtensionLifecycleAction::MarkCompatible).next;
        }
    }  // namespace

    TEST_CASE("Extension lifecycle publishes and removes installation independently from project intent", "[Extensions][Lifecycle]") {
        ExtensionActivationProjection missing;
        missing = Apply(missing, ExtensionLifecycleAction::EnableForProject).next;
        CHECK(missing.RestartReason() == ExtensionRestartReason::InstallationRequired);
        RequireError(TransitionExtensionActivation(missing, {.action = ExtensionLifecycleAction::MarkCompatible,
                                                             .owner = ExtensionLifecycleOwner::PackageLifecycleService,
                                                             .expectedRevision = missing.stateRevision}),
                     ExtensionErrors::LifecycleTransitionInvalid);
        CHECK(missing.compatibility == ExtensionHostCompatibilityState::NotEvaluated);
        auto installed = Apply(missing, ExtensionLifecycleAction::PublishInstalledComposition, "sha256:installed").next;
        CHECK(installed.installation == ExtensionInstallationState::Installed);
        CHECK(installed.enablement == ExtensionEnablementState::Enabled);
        CHECK(installed.trust == ExtensionTrustState::Untrusted);
        CHECK(installed.RestartReason() == ExtensionRestartReason::TrustRequired);
        installed = Apply(installed, ExtensionLifecycleAction::DisableForProject).next;
        const auto removed = Apply(installed, ExtensionLifecycleAction::RemoveInstalledComposition).next;
        CHECK(removed.installation == ExtensionInstallationState::NotInstalled);
        CHECK(removed.installedComposition.empty());
        CHECK(removed.enablement == ExtensionEnablementState::Disabled);
        CHECK(removed.RestartReason() == ExtensionRestartReason::None);
    }

    TEST_CASE("Extension lifecycle keeps install trust enablement and compatibility independent", "[Extensions][Lifecycle]") {
        auto installOnly = Installed();
        CHECK(installOnly.DesiredActivation() == ExtensionDesiredActivation::Inactive);
        CHECK(installOnly.RestartReason() == ExtensionRestartReason::None);

        const auto trustOnly = Apply(installOnly, ExtensionLifecycleAction::GrantTrust, installOnly.installedComposition).next;
        CHECK(trustOnly.trust == ExtensionTrustState::Trusted);
        CHECK(trustOnly.enablement == ExtensionEnablementState::Disabled);
        CHECK(trustOnly.DesiredActivation() == ExtensionDesiredActivation::Inactive);

        const auto enableOnly = Apply(installOnly, ExtensionLifecycleAction::EnableForProject).next;
        CHECK(enableOnly.enablement == ExtensionEnablementState::Enabled);
        CHECK(enableOnly.trust == ExtensionTrustState::Untrusted);
        CHECK(enableOnly.RestartReason() == ExtensionRestartReason::TrustRequired);

        auto incompatible = Apply(trustOnly, ExtensionLifecycleAction::EnableForProject).next;
        incompatible = Apply(incompatible, ExtensionLifecycleAction::MarkIncompatible).next;
        CHECK(incompatible.DesiredActivation() == ExtensionDesiredActivation::Active);
        CHECK(incompatible.RestartReason() == ExtensionRestartReason::HostIncompatible);
        CHECK(incompatible.runtime == ExtensionRuntimeActivityState::Inactive);
    }

    TEST_CASE("Extension lifecycle projects every trust enablement and compatibility pair independently", "[Extensions][Lifecycle]") {
        constexpr std::array trustStates{ExtensionTrustState::Untrusted, ExtensionTrustState::Trusted};
        constexpr std::array enablementStates{ExtensionEnablementState::Disabled, ExtensionEnablementState::Enabled};
        constexpr std::array compatibilityStates{
            ExtensionHostCompatibilityState::NotEvaluated,
            ExtensionHostCompatibilityState::Compatible,
            ExtensionHostCompatibilityState::Incompatible,
            ExtensionHostCompatibilityState::UnsupportedInHostProfile,
        };
        for (const auto trust : trustStates) {
            for (const auto enablement : enablementStates) {
                for (const auto compatibility : compatibilityStates) {
                    auto state = Installed();
                    state.trust = trust;
                    state.trustedComposition = trust == ExtensionTrustState::Trusted ? state.installedComposition : std::string{};
                    state.enablement = enablement;
                    state.compatibility = compatibility;
                    const bool desired = trust == ExtensionTrustState::Trusted && enablement == ExtensionEnablementState::Enabled;
                    CHECK(state.DesiredActivation() ==
                          (desired ? ExtensionDesiredActivation::Active : ExtensionDesiredActivation::Inactive));
                    if (enablement == ExtensionEnablementState::Disabled)
                        CHECK(state.RestartReason() == ExtensionRestartReason::None);
                    else if (trust == ExtensionTrustState::Untrusted)
                        CHECK(state.RestartReason() == ExtensionRestartReason::TrustRequired);
                    else if (compatibility == ExtensionHostCompatibilityState::NotEvaluated)
                        CHECK(state.RestartReason() == ExtensionRestartReason::CompatibilityRequired);
                    else if (compatibility == ExtensionHostCompatibilityState::Compatible)
                        CHECK(state.RestartReason() == ExtensionRestartReason::ActivationRequired);
                    else
                        CHECK(state.RestartReason() == ExtensionRestartReason::HostIncompatible);
                }
            }
        }
    }

    TEST_CASE("Extension lifecycle admits a complete activation and keeps desired and actual state distinct", "[Extensions][Lifecycle]") {
        auto state = Ready();
        CHECK(state.DesiredActivation() == ExtensionDesiredActivation::Active);
        CHECK(state.RestartReason() == ExtensionRestartReason::ActivationRequired);
        state = Apply(state, ExtensionLifecycleAction::MarkLoaded, state.installedComposition).next;
        CHECK(state.runtime == ExtensionRuntimeActivityState::Loaded);
        CHECK(state.outcome == ExtensionActivationOutcome::NotAttempted);
        state = Apply(state, ExtensionLifecycleAction::MarkActive, state.installedComposition, 1).next;
        CHECK(state.runtime == ExtensionRuntimeActivityState::Active);
        CHECK(state.outcome == ExtensionActivationOutcome::Succeeded);
        CHECK(state.activationGeneration == 1);
        CHECK(state.RestartReason() == ExtensionRestartReason::None);
    }

    TEST_CASE("Extension lifecycle disables and revokes trust without pretending live code stopped", "[Extensions][Lifecycle]") {
        auto active = Ready();
        active = Apply(active, ExtensionLifecycleAction::MarkLoaded, active.installedComposition).next;
        active = Apply(active, ExtensionLifecycleAction::MarkActive, active.installedComposition, 4).next;

        auto disabled = Apply(active, ExtensionLifecycleAction::DisableForProject).next;
        CHECK(disabled.runtime == ExtensionRuntimeActivityState::Active);
        CHECK(disabled.DesiredActivation() == ExtensionDesiredActivation::Inactive);
        CHECK(disabled.RestartReason() == ExtensionRestartReason::DeactivationRequired);
        disabled = Apply(disabled, ExtensionLifecycleAction::EnableForProject).next;
        CHECK(disabled.RestartReason() == ExtensionRestartReason::None);

        const auto revoked = Apply(disabled, ExtensionLifecycleAction::RevokeTrust).next;
        CHECK(revoked.trust == ExtensionTrustState::Untrusted);
        CHECK(revoked.runtime == ExtensionRuntimeActivityState::Active);
        CHECK(revoked.RestartReason() == ExtensionRestartReason::DeactivationRequired);
    }

    TEST_CASE("Extension replacement invalidates trust while retained runtime evidence requires restart", "[Extensions][Lifecycle]") {
        auto active = Ready();
        active = Apply(active, ExtensionLifecycleAction::MarkLoaded, active.installedComposition).next;
        active = Apply(active, ExtensionLifecycleAction::MarkActive, active.installedComposition, 2).next;
        const auto replacement = Apply(active, ExtensionLifecycleAction::ReplaceInstalledComposition, "sha256:replacement").next;
        CHECK(replacement.installedComposition == "sha256:replacement");
        CHECK(replacement.runtimeComposition == "sha256:installed");
        CHECK(replacement.trust == ExtensionTrustState::Untrusted);
        CHECK(replacement.compatibility == ExtensionHostCompatibilityState::NotEvaluated);
        CHECK(replacement.RestartReason() == ExtensionRestartReason::ReplacementRequired);

        RequireError(TransitionExtensionActivation(replacement, {.action = ExtensionLifecycleAction::GrantTrust,
                                                                 .owner = ExtensionLifecycleOwner::TrustService,
                                                                 .expectedRevision = replacement.stateRevision,
                                                                 .composition = "sha256:installed"}),
                     ExtensionErrors::LifecycleTransitionInvalid);
        CHECK(replacement.trust == ExtensionTrustState::Untrusted);
    }

    TEST_CASE("Extension activation failure is typed and retry does not become disablement", "[Extensions][Lifecycle]") {
        auto state = Ready();
        state = Apply(state, ExtensionLifecycleAction::RecordActivationFailure, {}, 0,
                      {.reason = ExtensionActivationFailureReason::HostLoadFailed, .message = "fixture load failed"})
                    .next;
        CHECK(state.outcome == ExtensionActivationOutcome::Failed);
        CHECK(state.failure.reason == ExtensionActivationFailureReason::HostLoadFailed);
        CHECK(state.enablement == ExtensionEnablementState::Enabled);
        CHECK(state.trust == ExtensionTrustState::Trusted);
        CHECK(state.RestartReason() == ExtensionRestartReason::ActivationFailed);

        state = Apply(state, ExtensionLifecycleAction::MarkLoaded, state.installedComposition).next;
        CHECK(state.outcome == ExtensionActivationOutcome::NotAttempted);
        CHECK(state.failure.Empty());
        state = Apply(state, ExtensionLifecycleAction::MarkActive, state.installedComposition, 1).next;
        CHECK(state.outcome == ExtensionActivationOutcome::Succeeded);
    }

    TEST_CASE("Extension host-profile rejection remains distinct from failure and enablement", "[Extensions][Lifecycle]") {
        auto state = Installed();
        state = Apply(state, ExtensionLifecycleAction::EnableForProject).next;
        state = Apply(state, ExtensionLifecycleAction::GrantTrust, state.installedComposition).next;
        state = Apply(state, ExtensionLifecycleAction::MarkUnsupportedInHostProfile).next;
        CHECK(state.compatibility == ExtensionHostCompatibilityState::UnsupportedInHostProfile);
        CHECK(state.outcome == ExtensionActivationOutcome::NotAttempted);
        CHECK(state.enablement == ExtensionEnablementState::Enabled);
        CHECK(state.RestartReason() == ExtensionRestartReason::HostIncompatible);
    }

    TEST_CASE("Extension lifecycle rejects stale duplicate out-of-order and wrong-owner transitions atomically",
              "[Extensions][Lifecycle]") {
        const auto initial = Installed();
        const auto enabled = Apply(initial, ExtensionLifecycleAction::EnableForProject).next;
        RequireError(TransitionExtensionActivation(enabled, {.action = ExtensionLifecycleAction::DisableForProject,
                                                             .owner = ExtensionLifecycleOwner::PackageLifecycleService,
                                                             .expectedRevision = initial.stateRevision}),
                     ExtensionErrors::LifecycleRevisionStale);
        RequireError(TransitionExtensionActivation(enabled, {.action = ExtensionLifecycleAction::EnableForProject,
                                                             .owner = ExtensionLifecycleOwner::PackageLifecycleService,
                                                             .expectedRevision = enabled.stateRevision}),
                     ExtensionErrors::LifecycleTransitionInvalid);
        RequireError(TransitionExtensionActivation(enabled, {.action = ExtensionLifecycleAction::GrantTrust,
                                                             .owner = ExtensionLifecycleOwner::ExtensionHost,
                                                             .expectedRevision = enabled.stateRevision,
                                                             .composition = enabled.installedComposition}),
                     ExtensionErrors::LifecycleTransitionInvalid);
        RequireError(TransitionExtensionActivation(enabled, {.action = ExtensionLifecycleAction::MarkActive,
                                                             .owner = ExtensionLifecycleOwner::ExtensionHost,
                                                             .expectedRevision = enabled.stateRevision,
                                                             .activationGeneration = 1,
                                                             .composition = enabled.installedComposition}),
                     ExtensionErrors::LifecycleTransitionInvalid);
        CHECK(enabled.stateRevision == 2);
        CHECK(enabled.runtime == ExtensionRuntimeActivityState::Inactive);
    }

    TEST_CASE("Extension lifecycle bounds revisions failure details and audit history", "[Extensions][Lifecycle]") {
        auto exhausted = Installed();
        exhausted.stateRevision = std::numeric_limits<std::uint64_t>::max();
        RequireError(TransitionExtensionActivation(exhausted, {.action = ExtensionLifecycleAction::EnableForProject,
                                                               .owner = ExtensionLifecycleOwner::PackageLifecycleService,
                                                               .expectedRevision = exhausted.stateRevision}),
                     ExtensionErrors::LifecycleCapacityExceeded);

        auto exhaustedGeneration = Ready();
        exhaustedGeneration =
            Apply(exhaustedGeneration, ExtensionLifecycleAction::MarkLoaded, exhaustedGeneration.installedComposition).next;
        exhaustedGeneration.activationGeneration = std::numeric_limits<std::uint64_t>::max();
        RequireError(TransitionExtensionActivation(exhaustedGeneration, {.action = ExtensionLifecycleAction::MarkActive,
                                                                         .owner = ExtensionLifecycleOwner::ExtensionHost,
                                                                         .expectedRevision = exhaustedGeneration.stateRevision,
                                                                         .activationGeneration = std::numeric_limits<std::uint64_t>::max(),
                                                                         .composition = exhaustedGeneration.installedComposition}),
                     ExtensionErrors::LifecycleCapacityExceeded);

        auto ready = Ready();
        const std::string maximumFailureMessage(1024, 'x');
        const auto exactFailure = Apply(ready, ExtensionLifecycleAction::RecordActivationFailure, {}, 0,
                                        {.reason = ExtensionActivationFailureReason::HostLoadFailed, .message = maximumFailureMessage})
                                      .next;
        CHECK(exactFailure.failure.message.size() == maximumFailureMessage.size());
        RequireError(TransitionExtensionActivation(ready, {.action = ExtensionLifecycleAction::RecordActivationFailure,
                                                           .owner = ExtensionLifecycleOwner::PackageLifecycleService,
                                                           .expectedRevision = ready.stateRevision,
                                                           .failure = {.reason = ExtensionActivationFailureReason::HostLoadFailed,
                                                                       .message = std::string(1025, 'x')}}),
                     ExtensionErrors::LifecycleTransitionInvalid);

        ExtensionActivationAuditHistory history{2};
        ExtensionActivationAuditHistory noHistory{0};
        const auto first = Apply(Installed(), ExtensionLifecycleAction::EnableForProject).audit;
        RequireError(noHistory.Append(first), ExtensionErrors::LifecycleCapacityExceeded);
        REQUIRE(history.Append(first).HasValue());
        auto trustedBase = Installed();
        trustedBase.stateRevision = first.resultingRevision;
        const auto second = Apply(trustedBase, ExtensionLifecycleAction::GrantTrust, trustedBase.installedComposition).audit;
        REQUIRE(history.Append(second).HasValue());
        CHECK(history.Records().size() == 2);
        RequireError(history.Append(second), ExtensionErrors::LifecycleCapacityExceeded);
        CHECK(history.Records().size() == 2);
    }
}  // namespace Horo::Extensions
