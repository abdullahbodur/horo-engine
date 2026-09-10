#pragma once

/**
 * @file ExtensionActivationState.h
 * @brief Typed extension trust, enablement, compatibility, and runtime activation state.
 */

#include "Horo/Foundation/Result.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Horo::Extensions {
    /** @brief Whether verified package content is locally available. */
    enum class ExtensionInstallationState : std::uint8_t {
        NotInstalled,
        Installed,
    };

    /** @brief Explicit execution trust supplied by local or organization policy. */
    enum class ExtensionTrustState : std::uint8_t {
        Untrusted,
        Trusted,
    };

    /** @brief Portable project intent, which never carries execution trust. */
    enum class ExtensionEnablementState : std::uint8_t {
        Disabled,
        Enabled,
    };

    /** @brief Result of evaluating the exact package against one host composition. */
    enum class ExtensionHostCompatibilityState : std::uint8_t {
        NotEvaluated,
        Compatible,
        Incompatible,
        UnsupportedInHostProfile,
    };

    /** @brief Desired next-composition activity derived from trust and enablement facts. */
    enum class ExtensionDesiredActivation : std::uint8_t {
        Inactive,
        Active,
    };

    /** @brief Process-local activity proven by ExtensionHost. */
    enum class ExtensionRuntimeActivityState : std::uint8_t {
        Inactive,
        Loaded,
        Active,
    };

    /** @brief Last activation attempt, kept separate from policy and compatibility. */
    enum class ExtensionActivationOutcome : std::uint8_t {
        NotAttempted,
        Succeeded,
        Failed,
    };

    /** @brief Typed reason associated with a failed activation attempt. */
    enum class ExtensionActivationFailureReason : std::uint8_t {
        None,
        CandidateRejected,
        HostLoadFailed,
        ContributionRejected,
        RuntimeInitializationFailed,
    };

    /** @brief Why current process state cannot yet match desired state. */
    enum class ExtensionRestartReason : std::uint8_t {
        None,
        InstallationRequired,
        ActivationRequired,
        DeactivationRequired,
        ReplacementRequired,
        TrustRequired,
        CompatibilityRequired,
        HostIncompatible,
        ActivationFailed,
    };

    /** @brief Authority allowed to own a lifecycle transition. */
    enum class ExtensionLifecycleOwner : std::uint8_t {
        TrustService,
        PackageLifecycleService,
        ExtensionHost,
    };

    /** @brief Closed set of lifecycle mutations accepted by the reducer. */
    enum class ExtensionLifecycleAction : std::uint8_t {
        PublishInstalledComposition,
        RemoveInstalledComposition,
        GrantTrust,
        RevokeTrust,
        EnableForProject,
        DisableForProject,
        MarkCompatible,
        MarkIncompatible,
        MarkUnsupportedInHostProfile,
        MarkLoaded,
        MarkActive,
        MarkInactive,
        RecordActivationFailure,
        ReplaceInstalledComposition,
    };

    /** @brief One typed presentation-only activation failure. */
    struct ExtensionActivationFailure final {
        ExtensionActivationFailureReason reason{ExtensionActivationFailureReason::None}; /**< Control-flow reason. */
        std::string message;                                                             /**< Bounded presentation detail. */

        /** @brief Returns whether no failure is recorded. */
        [[nodiscard]] bool Empty() const noexcept {
            return reason == ExtensionActivationFailureReason::None;
        }
    };

    /** @brief Complete authoritative facts for one extension package lifecycle. */
    struct ExtensionActivationProjection final {
        ExtensionInstallationState installation{ExtensionInstallationState::NotInstalled};            /**< Package availability. */
        ExtensionTrustState trust{ExtensionTrustState::Untrusted};                                    /**< Local/policy trust. */
        ExtensionEnablementState enablement{ExtensionEnablementState::Disabled};                      /**< Portable project intent. */
        ExtensionHostCompatibilityState compatibility{ExtensionHostCompatibilityState::NotEvaluated}; /**< Host fit. */
        ExtensionRuntimeActivityState runtime{ExtensionRuntimeActivityState::Inactive};               /**< Live activity. */
        ExtensionActivationOutcome outcome{ExtensionActivationOutcome::NotAttempted};                 /**< Last attempt. */
        ExtensionActivationFailure failure{}; /**< Typed failure and presentation detail. */
        std::string installedComposition;     /**< Exact installed composition identity. */
        std::string trustedComposition;       /**< Composition identity covered by trust. */
        std::string runtimeComposition;       /**< Composition identity held by the live host. */
        std::uint64_t stateRevision{1};       /**< Monotonic lifecycle revision. */
        std::uint64_t activationGeneration{}; /**< Monotonic live generation, zero before activation. */

        /** @brief Returns explicit desired activity without consulting runtime state. @return Derived desired activity. */
        [[nodiscard]] ExtensionDesiredActivation DesiredActivation() const noexcept;
        /** @brief Returns whether trust covers the exact installed composition. @return True only for exact current trust. */
        [[nodiscard]] bool HasCurrentTrust() const noexcept;
        /** @brief Returns the typed restart/blocking reason for desired versus live state. @return Current blocking reason. */
        [[nodiscard]] ExtensionRestartReason RestartReason() const noexcept;
    };

    /** @brief Optimistic-concurrency lifecycle mutation supplied by the owning service. */
    struct ExtensionLifecycleCommand final {
        ExtensionLifecycleAction action{ExtensionLifecycleAction::EnableForProject};     /**< Requested mutation. */
        ExtensionLifecycleOwner owner{ExtensionLifecycleOwner::PackageLifecycleService}; /**< Claimed authority. */
        std::uint64_t expectedRevision{};                                                /**< Exact state revision being replaced. */
        std::uint64_t activationGeneration{};                                            /**< New host generation for MarkActive. */
        std::string composition;              /**< Exact composition for trust/replacement/runtime evidence. */
        ExtensionActivationFailure failure{}; /**< Failure evidence for RecordActivationFailure. */
    };

    /** @brief Immutable audit evidence emitted only for an accepted atomic transition. */
    struct ExtensionActivationAuditRecord final {
        ExtensionLifecycleAction action{};    /**< Accepted action. */
        ExtensionLifecycleOwner owner{};      /**< Authority that owned the action. */
        std::uint64_t priorRevision{};        /**< Revision compared by the reducer. */
        std::uint64_t resultingRevision{};    /**< Revision published by the reducer. */
        std::uint64_t activationGeneration{}; /**< Resulting live generation. */
    };

    /** @brief Accepted next state and its immutable audit evidence. */
    struct ExtensionLifecycleTransition final {
        ExtensionActivationProjection next;   /**< Complete replacement state. */
        ExtensionActivationAuditRecord audit; /**< Evidence for the accepted mutation. */
    };

    /**
     * @brief Applies one owner-authorized lifecycle mutation without partial state changes.
     * @param current Complete current projection.
     * @param command Exact expected revision, owner, and action-specific evidence.
     * @return Replacement projection and audit record, or a typed stale/invalid failure.
     */
    [[nodiscard]] Result<ExtensionLifecycleTransition> TransitionExtensionActivation(const ExtensionActivationProjection &current,
                                                                                     const ExtensionLifecycleCommand &command);

    /** @brief Bounded append-only audit batch owned by the package lifecycle boundary. */
    class ExtensionActivationAuditHistory final {
    public:
        /**
         * @brief Creates a bounded history.
         * @param capacity Maximum accepted records; zero rejects all appends.
         */
        explicit ExtensionActivationAuditHistory(std::size_t capacity) : capacity_(capacity) {}

        /**
         * @brief Appends one record or fails without mutation when capacity is exhausted.
         * @param record Immutable accepted-transition evidence.
         * @return Success, or a typed capacity/order failure.
         */
        [[nodiscard]] Result<void> Append(ExtensionActivationAuditRecord record);

        /** @brief Returns immutable accepted records in revision order. @return Read-only record storage. */
        [[nodiscard]] const std::vector<ExtensionActivationAuditRecord> &Records() const noexcept {
            return records_;
        }

    private:
        std::size_t capacity_{};
        std::vector<ExtensionActivationAuditRecord> records_;
    };
}  // namespace Horo::Extensions
