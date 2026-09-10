#pragma once

/**
 * @file PlatformUserSession.h
 * @brief Opaque platform-subject capability and immutable generation-fenced session model.
 */

#include "Horo/Foundation/ErrorCode.h"
#include "Horo/Foundation/Result.h"

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace Horo::PlatformServices {
    /** @brief Exhaustive Platform Services capability positions. */
    enum class PlatformServiceKind : std::uint8_t {
        Achievements,
        LeaderboardsAndStats,
        Cloud,
        Presence,
        Friends,
        Session,
        Count
    };

    /** @brief Nonzero provider activation generation captured by a live subject. */
    struct PlatformProviderGeneration final {
        std::uint64_t value{};

        /** @brief Checks representation. @return Whether the generation is nonzero. */
        [[nodiscard]] constexpr bool IsValid() const noexcept {
            return value != 0;
        }

        [[nodiscard]] constexpr auto operator<=>(const PlatformProviderGeneration &) const noexcept = default;
    };

    /** @brief Nonzero monotonic generation fencing one platform-subject binding. */
    struct PlatformSessionGeneration final {
        std::uint64_t value{};

        /** @brief Checks representation. @return Whether the generation is nonzero. */
        [[nodiscard]] constexpr bool IsValid() const noexcept {
            return value != 0;
        }

        [[nodiscard]] constexpr auto operator<=>(const PlatformSessionGeneration &) const noexcept = default;
    };

    /** @brief Nonzero monotonic revision fencing effective session access policy. */
    struct PlatformAccessPolicyRevision final {
        std::uint64_t value{};

        /** @brief Checks representation. @return Whether the revision is nonzero. */
        [[nodiscard]] constexpr bool IsValid() const noexcept {
            return value != 0;
        }

        [[nodiscard]] constexpr auto operator<=>(const PlatformAccessPolicyRevision &) const noexcept = default;
    };

    /** @brief Exactly 128 bits of cryptographic random evidence supplied by the identity broker. */
    struct PlatformSubjectNonce final {
        std::array<std::byte, 16> bytes{};

        /** @brief Rejects the reserved all-zero nonce. @return Whether at least one byte is nonzero. */
        [[nodiscard]] bool IsValid() const noexcept;
    };

    /**
     * @brief Equality-only live subject capability with no public nonce or native-account representation.
     * @details The value is process-local, non-serializable and valid only for its exact provider and session generations.
     */
    class PlatformSubjectHandle final {
    public:
        PlatformSubjectHandle() = default;

        /** @brief Checks the opaque capability representation. @return True only for a bound nonzero handle. */
        [[nodiscard]] bool IsValid() const noexcept;
        /** @brief Returns the captured provider generation. @return Nonzero for a valid handle. */
        [[nodiscard]] PlatformProviderGeneration ProviderGeneration() const noexcept;
        /** @brief Returns the captured session generation. @return Nonzero for a valid handle. */
        [[nodiscard]] PlatformSessionGeneration SessionGeneration() const noexcept;

        [[nodiscard]] bool operator==(const PlatformSubjectHandle &) const noexcept = default;

    private:
        friend class PlatformSessionSnapshot;
        friend Result<class PlatformSessionSnapshot> BuildPlatformSessionSnapshot(const struct PlatformSessionCandidate &);
        friend Result<class PlatformSessionSnapshot> BuildPlatformSessionSnapshotReplacement(const class PlatformSessionSnapshot &,
                                                                                             const struct PlatformSessionCandidate &);

        std::array<std::byte, 16> nonce_{};
        PlatformProviderGeneration providerGeneration_{};
        PlatformSessionGeneration sessionGeneration_{};
    };

    /** @brief Explicit lifecycle phase; only Active admits user-scoped work. */
    enum class PlatformSessionPhase : std::uint8_t {
        NoSubject,
        Authenticating,
        Active,
        Closing,
        Failed
    };

    /** @brief Privacy-safe normalized reason for a non-active session phase. */
    enum class PlatformSessionReason : std::uint8_t {
        None,
        AuthenticationFailed,
        ProviderUnavailable,
        UserSignedOut,
        ProviderSignedOut,
        AccountSwitch,
        ProviderReplaced,
        Shutdown
    };

    /** @brief Effective per-service access state; only Granted admits work. */
    enum class PlatformSessionAccessState : std::uint8_t {
        Unavailable,
        Granted,
        ConsentRequired,
        Denied,
        Restricted,
        Revoked
    };

    /** @brief Complete bounded capability projection for one immutable session snapshot. */
    struct PlatformSessionCapabilities final {
        std::array<PlatformSessionAccessState, static_cast<std::size_t>(PlatformServiceKind::Count)> services{};

        /** @brief Returns one service position. @param service Known service kind. @return Effective access state. */
        [[nodiscard]] PlatformSessionAccessState Access(PlatformServiceKind service) const noexcept;
    };

    /** @brief Detached provider-neutral candidate awaiting complete validation. */
    struct PlatformSessionCandidate final {
        PlatformSessionPhase phase{PlatformSessionPhase::NoSubject};
        PlatformSessionGeneration generation{1};
        PlatformProviderGeneration providerGeneration{1};
        PlatformAccessPolicyRevision accessRevision{1};
        std::optional<PlatformSubjectNonce> subjectNonce;
        PlatformSessionCapabilities capabilities;
        PlatformSessionReason reason{PlatformSessionReason::None};
    };

    /** @brief Immutable validated session truth with no raw/native account identity. */
    class PlatformSessionSnapshot final {
    public:
        /** @brief Returns the lifecycle phase. @return Validated phase. */
        [[nodiscard]] PlatformSessionPhase Phase() const noexcept;
        /** @brief Returns the monotonic session generation. @return Nonzero generation. */
        [[nodiscard]] PlatformSessionGeneration Generation() const noexcept;
        /** @brief Returns the provider generation. @return Nonzero captured generation. */
        [[nodiscard]] PlatformProviderGeneration ProviderGeneration() const noexcept;
        /** @brief Returns the access-policy revision. @return Nonzero monotonic revision. */
        [[nodiscard]] PlatformAccessPolicyRevision AccessRevision() const noexcept;
        /** @brief Returns the subject capability. @return Present exactly while Active. */
        [[nodiscard]] const std::optional<PlatformSubjectHandle> &Subject() const noexcept;
        /** @brief Returns the complete access projection. @return Immutable bounded capability states. */
        [[nodiscard]] const PlatformSessionCapabilities &Capabilities() const noexcept;
        /** @brief Returns the privacy-safe lifecycle reason. @return Validated normalized reason. */
        [[nodiscard]] PlatformSessionReason Reason() const noexcept;

    private:
        friend Result<PlatformSessionSnapshot> BuildPlatformSessionSnapshot(const PlatformSessionCandidate &);
        friend Result<PlatformSessionSnapshot> BuildPlatformSessionSnapshotReplacement(const PlatformSessionSnapshot &,
                                                                                       const PlatformSessionCandidate &);

        PlatformSessionSnapshot() = default;

        PlatformSessionPhase phase_{PlatformSessionPhase::NoSubject};
        PlatformSessionGeneration generation_{};
        PlatformProviderGeneration providerGeneration_{};
        PlatformAccessPolicyRevision accessRevision_{};
        std::optional<PlatformSubjectHandle> subject_;
        PlatformSessionCapabilities capabilities_;
        PlatformSessionReason reason_{PlatformSessionReason::None};
    };

    /** @brief Stable validation, lifecycle and access failures for the session model. */
    namespace PlatformSessionErrors {
        extern const ErrorCodeDescriptor InvalidSnapshot;
        extern const ErrorCodeDescriptor InvalidTransition;
        extern const ErrorCodeDescriptor GenerationExhausted;
        extern const ErrorCodeDescriptor NoSubject;
        extern const ErrorCodeDescriptor Authenticating;
        extern const ErrorCodeDescriptor Closing;
        extern const ErrorCodeDescriptor Failed;
        extern const ErrorCodeDescriptor StaleSession;
        extern const ErrorCodeDescriptor StaleAccessPolicy;
        extern const ErrorCodeDescriptor ConsentRequired;
        extern const ErrorCodeDescriptor AccessDenied;
        extern const ErrorCodeDescriptor AccessRestricted;
        extern const ErrorCodeDescriptor AccessRevoked;
        extern const ErrorCodeDescriptor AccessUnavailable;
    }  // namespace PlatformSessionErrors

    /**
     * @brief Builds one detached immutable session snapshot without invoking provider or observer code.
     * @param candidate Complete provider-neutral candidate.
     * @return Validated snapshot or typed failure.
     */
    [[nodiscard]] Result<PlatformSessionSnapshot> BuildPlatformSessionSnapshot(const PlatformSessionCandidate &candidate);

    /**
     * @brief Validates and builds a monotonic replacement while preserving the prior snapshot on failure.
     * @param previous Last published immutable snapshot.
     * @param candidate Detached replacement candidate.
     * @return Replacement or typed transition/generation failure.
     */
    [[nodiscard]] Result<PlatformSessionSnapshot> BuildPlatformSessionSnapshotReplacement(const PlatformSessionSnapshot &previous,
                                                                                          const PlatformSessionCandidate &candidate);

    /**
     * @brief Revalidates a captured subject and access revision before a user-scoped commit.
     * @param snapshot Current immutable authority.
     * @param subject Subject captured at admission.
     * @param accessRevision Access policy captured at admission.
     * @param service Service whose current access must remain Granted.
     * @return Success only when subject, generations, policy and access all remain current.
     */
    [[nodiscard]] Result<void> ValidatePlatformSessionAccess(const PlatformSessionSnapshot &snapshot, const PlatformSubjectHandle &subject,
                                                             PlatformAccessPolicyRevision accessRevision, PlatformServiceKind service);
}  // namespace Horo::PlatformServices
