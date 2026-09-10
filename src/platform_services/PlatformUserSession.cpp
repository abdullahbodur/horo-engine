#include "Horo/PlatformServices/PlatformUserSession.h"

#include <algorithm>
#include <limits>
#include <string>

namespace Horo::PlatformServices {
    namespace {
        [[nodiscard]] constexpr bool IsKnown(const PlatformServiceKind value) noexcept {
            return value < PlatformServiceKind::Count;
        }

        [[nodiscard]] constexpr bool IsKnown(const PlatformSessionPhase value) noexcept {
            return value <= PlatformSessionPhase::Failed;
        }

        [[nodiscard]] constexpr bool IsKnown(const PlatformSessionReason value) noexcept {
            return value <= PlatformSessionReason::Shutdown;
        }

        [[nodiscard]] constexpr bool IsKnown(const PlatformSessionAccessState value) noexcept {
            return value <= PlatformSessionAccessState::Revoked;
        }

        [[nodiscard]] bool HasNoGrantedCapability(const PlatformSessionCapabilities &capabilities) noexcept {
            return std::ranges::all_of(capabilities.services, [](const PlatformSessionAccessState state) {
                return state == PlatformSessionAccessState::Unavailable;
            });
        }

        [[nodiscard]] constexpr bool HasValidReason(const PlatformSessionPhase phase, const PlatformSessionReason reason) noexcept {
            constexpr std::array<std::array<bool, 8>, 5> ValidReasons{{
                {{true, false, false, true, true, true, true, true}},
                {{true, false, false, false, false, false, false, false}},
                {{true, false, false, false, false, false, false, false}},
                {{false, false, false, true, true, true, true, true}},
                {{false, true, true, false, false, false, false, false}},
            }};
            return ValidReasons[static_cast<std::size_t>(phase)][static_cast<std::size_t>(reason)];
        }

        [[nodiscard]] constexpr bool HasValidBaseEvidence(const PlatformSessionCandidate &candidate) noexcept {
            return IsKnown(candidate.phase) && IsKnown(candidate.reason) && candidate.generation.IsValid() &&
                   candidate.providerGeneration.IsValid() && candidate.accessRevision.IsValid();
        }

        [[nodiscard]] bool HasKnownCapabilities(const PlatformSessionCapabilities &capabilities) noexcept {
            return std::ranges::all_of(capabilities.services, [](const PlatformSessionAccessState state) {
                return IsKnown(state);
            });
        }

        [[nodiscard]] bool HasValidSubjectEvidence(const PlatformSessionCandidate &candidate) noexcept {
            const bool active = candidate.phase == PlatformSessionPhase::Active;
            if (active != candidate.subjectNonce.has_value())
                return false;
            if (active && !candidate.subjectNonce->IsValid())
                return false;
            if (!active && !HasNoGrantedCapability(candidate.capabilities))
                return false;
            return true;
        }

        [[nodiscard]] Result<void> ValidateCandidate(const PlatformSessionCandidate &candidate) {
            if (!HasValidBaseEvidence(candidate) || !HasKnownCapabilities(candidate.capabilities))
                return Result<void>::Failure(MakeError(PlatformSessionErrors::InvalidSnapshot));
            if (!HasValidReason(candidate.phase, candidate.reason) || !HasValidSubjectEvidence(candidate))
                return Result<void>::Failure(MakeError(PlatformSessionErrors::InvalidSnapshot));
            return Result<void>::Success();
        }

        [[nodiscard]] constexpr bool IsLegalPhaseTransition(const PlatformSessionPhase previous, const PlatformSessionPhase next) noexcept {
            constexpr std::array<std::array<bool, 5>, 5> ValidTransitions{{
                {{true, true, false, false, false}},
                {{true, false, true, true, true}},
                {{false, false, false, true, false}},
                {{true, true, false, false, true}},
                {{true, true, false, false, false}},
            }};
            return ValidTransitions[static_cast<std::size_t>(previous)][static_cast<std::size_t>(next)];
        }

        [[nodiscard]] bool IsValidAccessRefresh(const PlatformSessionSnapshot &previous,
                                                const PlatformSessionSnapshot &replacement) noexcept {
            if (previous.Phase() != PlatformSessionPhase::Active || replacement.Phase() != PlatformSessionPhase::Active)
                return false;
            if (replacement.ProviderGeneration() != previous.ProviderGeneration() ||
                replacement.AccessRevision() <= previous.AccessRevision())
                return false;
            return previous.Subject() && replacement.Subject() == previous.Subject();
        }

        [[nodiscard]] Result<void> ValidateGenerationAdvance(const PlatformSessionSnapshot &previous,
                                                             const PlatformSessionSnapshot &replacement) {
            if (previous.Generation().value == std::numeric_limits<std::uint64_t>::max())
                return Result<void>::Failure(MakeError(PlatformSessionErrors::GenerationExhausted));
            if (replacement.Generation().value != previous.Generation().value + 1)
                return Result<void>::Failure(MakeError(PlatformSessionErrors::InvalidTransition));
            if (replacement.AccessRevision() <= previous.AccessRevision() || !IsLegalPhaseTransition(previous.Phase(), replacement.Phase()))
                return Result<void>::Failure(MakeError(PlatformSessionErrors::InvalidTransition));
            return Result<void>::Success();
        }

        [[nodiscard]] bool IsCurrentSubject(const PlatformSessionSnapshot &snapshot, const PlatformSubjectHandle &subject) noexcept {
            if (!snapshot.Subject() || *snapshot.Subject() != subject)
                return false;
            return subject.ProviderGeneration() == snapshot.ProviderGeneration() && subject.SessionGeneration() == snapshot.Generation();
        }

        [[nodiscard]] Error AccessError(const PlatformSessionAccessState state) {
            using enum PlatformSessionAccessState;
            switch (state) {
                case ConsentRequired:
                    return MakeError(PlatformSessionErrors::ConsentRequired);
                case Denied:
                    return MakeError(PlatformSessionErrors::AccessDenied);
                case Restricted:
                    return MakeError(PlatformSessionErrors::AccessRestricted);
                case Revoked:
                    return MakeError(PlatformSessionErrors::AccessRevoked);
                case Unavailable:
                    return MakeError(PlatformSessionErrors::AccessUnavailable);
                case Granted:
                    break;
            }
            return MakeError(PlatformSessionErrors::InvalidSnapshot);
        }

        [[nodiscard]] Error InactiveSessionError(const PlatformSessionPhase phase) {
            using enum PlatformSessionPhase;
            switch (phase) {
                case NoSubject:
                    return MakeError(PlatformSessionErrors::NoSubject);
                case Authenticating:
                    return MakeError(PlatformSessionErrors::Authenticating);
                case Closing:
                    return MakeError(PlatformSessionErrors::Closing);
                case Failed:
                    return MakeError(PlatformSessionErrors::Failed);
                case Active:
                    break;
            }
            return MakeError(PlatformSessionErrors::InvalidSnapshot);
        }
    }  // namespace

    namespace PlatformSessionErrors {
        namespace {
            const ErrorDomainId Domain{"horo.platform.session"};

            [[nodiscard]] ErrorCodeDescriptor Descriptor(const std::string_view code, const std::string_view message,
                                                         const std::string_view remediation, const bool retryable = false) {
                return {Domain, ErrorCode{std::string{code}}, ErrorSeverity::Error, message, remediation, false, retryable};
            }
        }  // namespace

        const ErrorCodeDescriptor InvalidSnapshot =
            Descriptor("platform.session.invalid", "Platform session evidence is malformed.",
                       "Reject the detached candidate and rebuild complete bounded session evidence.");
        const ErrorCodeDescriptor InvalidTransition =
            Descriptor("platform.session.transition_invalid", "Platform session transition is invalid.",
                       "Close and bind through the explicit session lifecycle.");
        const ErrorCodeDescriptor GenerationExhausted =
            Descriptor("platform.session.generation_exhausted", "Platform session generation is exhausted.",
                       "Restart the owning frontend without reusing a process generation.");
        const ErrorCodeDescriptor NoSubject = Descriptor("platform.session.no_subject", "No platform subject is active.",
                                                         "Complete an explicit sign-in before retrying.", true);
        const ErrorCodeDescriptor Authenticating =
            Descriptor("platform.session.authenticating", "Platform authentication is still in progress.",
                       "Wait for a later immutable session snapshot.", true);
        const ErrorCodeDescriptor Closing = Descriptor("platform.session.closing", "The platform session is closing.",
                                                       "Wait for closure and explicit reauthentication.", true);
        const ErrorCodeDescriptor Failed = Descriptor("platform.session.failed", "The platform session failed.",
                                                      "Inspect the normalized session reason and reauthenticate.", true);
        const ErrorCodeDescriptor StaleSession = Descriptor("platform.session.stale", "The captured platform subject is stale.",
                                                            "Discard the result and capture the current active session.");
        const ErrorCodeDescriptor StaleAccessPolicy =
            Descriptor("platform.access.policy_stale", "The captured platform access policy is stale.",
                       "Reauthorize against the current access-policy revision.");
        const ErrorCodeDescriptor ConsentRequired = Descriptor("platform.access.consent_required", "Platform service consent is required.",
                                                               "Request consent through the owning policy authority.");
        const ErrorCodeDescriptor AccessDenied =
            Descriptor("platform.access.denied", "Platform service access is denied.", "Respect the effective product/provider policy.");
        const ErrorCodeDescriptor AccessRestricted = Descriptor("platform.access.restricted", "Platform service access is restricted.",
                                                                "Respect parental, region, account, and platform restrictions.");
        const ErrorCodeDescriptor AccessRevoked = Descriptor("platform.access.revoked", "Platform service access was revoked.",
                                                             "Stop publication and obtain fresh authorization.");
        const ErrorCodeDescriptor AccessUnavailable = Descriptor("platform.access.unavailable", "Platform service access is unavailable.",
                                                                 "Use only an explicitly available provider capability.");
    }  // namespace PlatformSessionErrors

    /** @copydoc PlatformSubjectNonce::IsValid */
    bool PlatformSubjectNonce::IsValid() const noexcept {
        return std::ranges::any_of(bytes, [](const std::byte value) {
            return value != std::byte{};
        });
    }

    /** @copydoc PlatformSubjectHandle::IsValid */
    bool PlatformSubjectHandle::IsValid() const noexcept {
        return PlatformSubjectNonce{nonce_}.IsValid() && providerGeneration_.IsValid() && sessionGeneration_.IsValid();
    }

    /** @copydoc PlatformSubjectHandle::ProviderGeneration */
    PlatformProviderGeneration PlatformSubjectHandle::ProviderGeneration() const noexcept {
        return providerGeneration_;
    }

    /** @copydoc PlatformSubjectHandle::SessionGeneration */
    PlatformSessionGeneration PlatformSubjectHandle::SessionGeneration() const noexcept {
        return sessionGeneration_;
    }

    /** @copydoc PlatformSessionCapabilities::Access */
    PlatformSessionAccessState PlatformSessionCapabilities::Access(const PlatformServiceKind service) const noexcept {
        if (!IsKnown(service))
            return PlatformSessionAccessState::Unavailable;
        return services[static_cast<std::size_t>(service)];
    }

    /** @copydoc PlatformSessionSnapshot::Phase */
    PlatformSessionPhase PlatformSessionSnapshot::Phase() const noexcept {
        return phase_;
    }

    /** @copydoc PlatformSessionSnapshot::Generation */
    PlatformSessionGeneration PlatformSessionSnapshot::Generation() const noexcept {
        return generation_;
    }

    /** @copydoc PlatformSessionSnapshot::ProviderGeneration */
    PlatformProviderGeneration PlatformSessionSnapshot::ProviderGeneration() const noexcept {
        return providerGeneration_;
    }

    /** @copydoc PlatformSessionSnapshot::AccessRevision */
    PlatformAccessPolicyRevision PlatformSessionSnapshot::AccessRevision() const noexcept {
        return accessRevision_;
    }

    /** @copydoc PlatformSessionSnapshot::Subject */
    const std::optional<PlatformSubjectHandle> &PlatformSessionSnapshot::Subject() const noexcept {
        return subject_;
    }

    /** @copydoc PlatformSessionSnapshot::Capabilities */
    const PlatformSessionCapabilities &PlatformSessionSnapshot::Capabilities() const noexcept {
        return capabilities_;
    }

    /** @copydoc PlatformSessionSnapshot::Reason */
    PlatformSessionReason PlatformSessionSnapshot::Reason() const noexcept {
        return reason_;
    }

    /** @copydoc BuildPlatformSessionSnapshot */
    Result<PlatformSessionSnapshot> BuildPlatformSessionSnapshot(const PlatformSessionCandidate &candidate) {
        if (const auto valid = ValidateCandidate(candidate); valid.HasError())
            return Result<PlatformSessionSnapshot>::Failure(valid.ErrorValue());

        PlatformSessionSnapshot result;
        result.phase_ = candidate.phase;
        result.generation_ = candidate.generation;
        result.providerGeneration_ = candidate.providerGeneration;
        result.accessRevision_ = candidate.accessRevision;
        result.capabilities_ = candidate.capabilities;
        result.reason_ = candidate.reason;
        if (candidate.subjectNonce) {
            PlatformSubjectHandle subject;
            subject.nonce_ = candidate.subjectNonce->bytes;
            subject.providerGeneration_ = candidate.providerGeneration;
            subject.sessionGeneration_ = candidate.generation;
            result.subject_ = subject;
        }
        return Result<PlatformSessionSnapshot>::Success(std::move(result));
    }

    /** @copydoc BuildPlatformSessionSnapshotReplacement */
    Result<PlatformSessionSnapshot> BuildPlatformSessionSnapshotReplacement(const PlatformSessionSnapshot &previous,
                                                                            const PlatformSessionCandidate &candidate) {
        auto replacement = BuildPlatformSessionSnapshot(candidate);
        if (replacement.HasError())
            return replacement;

        if (candidate.generation == previous.Generation()) {
            if (!IsValidAccessRefresh(previous, replacement.Value()))
                return Result<PlatformSessionSnapshot>::Failure(MakeError(PlatformSessionErrors::InvalidTransition));
            return replacement;
        }

        if (const auto generation = ValidateGenerationAdvance(previous, replacement.Value()); generation.HasError())
            return Result<PlatformSessionSnapshot>::Failure(generation.ErrorValue());
        if (previous.subject_ && replacement.Value().subject_ && previous.subject_->nonce_ == replacement.Value().subject_->nonce_)
            return Result<PlatformSessionSnapshot>::Failure(MakeError(PlatformSessionErrors::InvalidSnapshot));
        return replacement;
    }

    /** @copydoc ValidatePlatformSessionAccess */
    Result<void> ValidatePlatformSessionAccess(const PlatformSessionSnapshot &snapshot, const PlatformSubjectHandle &subject,
                                               const PlatformAccessPolicyRevision accessRevision, const PlatformServiceKind service) {
        if (!IsKnown(service) || !subject.IsValid())
            return Result<void>::Failure(MakeError(PlatformSessionErrors::InvalidSnapshot));
        if (snapshot.Phase() != PlatformSessionPhase::Active)
            return Result<void>::Failure(InactiveSessionError(snapshot.Phase()));
        if (!IsCurrentSubject(snapshot, subject))
            return Result<void>::Failure(MakeError(PlatformSessionErrors::StaleSession));
        if (accessRevision != snapshot.AccessRevision())
            return Result<void>::Failure(MakeError(PlatformSessionErrors::StaleAccessPolicy));
        if (const auto access = snapshot.Capabilities().Access(service); access != PlatformSessionAccessState::Granted)
            return Result<void>::Failure(AccessError(access));
        return Result<void>::Success();
    }
}  // namespace Horo::PlatformServices
