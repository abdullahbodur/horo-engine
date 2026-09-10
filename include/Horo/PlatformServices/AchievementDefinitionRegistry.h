#pragma once

/**
 * @file AchievementDefinitionRegistry.h
 * @brief Typed immutable achievement authoring-definition registry contract.
 */

#include "Horo/PlatformServices/PlatformStableIdRegistry.h"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Horo::PlatformServices {
    inline constexpr std::uint32_t AchievementDefinitionRegistrySchemaVersion = 1;

    /** @brief Portable achievement mutation algebra selected at authoring time. */
    enum class AchievementProgressKind : std::uint8_t {
        UnlockOnce,
        SetProgressMaximum
    };

    /** @brief Typed progress contract shared by cook, frontend policy, and provider qualification. */
    struct AchievementProgressSchema final {
        AchievementProgressKind kind{AchievementProgressKind::UnlockOnce}; /**< Exact portable mutation algebra. */
        std::uint32_t total{1}; /**< Inclusive completion target; binary unlock requires exactly one. */

        [[nodiscard]] auto operator<=>(const AchievementProgressSchema &) const noexcept = default;
    };

    /** @brief Presentation-only localization identity and platform-hidden policy. */
    struct LocalizedAchievementPresentation final {
        std::string titleLocalizationKey;       /**< Required bounded localization key, never durable identity. */
        std::string descriptionLocalizationKey; /**< Required bounded localization key, never durable identity. */
        bool hidden{};                          /**< Whether providers should conceal pre-unlock presentation. */

        [[nodiscard]] bool operator==(const LocalizedAchievementPresentation &) const noexcept = default;
    };

    /** @brief One complete provider-neutral authored achievement definition. */
    struct AchievementDefinition final {
        AchievementId id;                              /**< Active ADR-132 achievement identity. */
        ProgressionAuthorityMode authority{};          /**< Immutable local/server fact authority. */
        AchievementProgressSchema progress;            /**< Immutable retry/replay mutation semantics. */
        LocalizedAchievementPresentation presentation; /**< Mutable localized presentation metadata. */

        [[nodiscard]] bool operator==(const AchievementDefinition &) const noexcept = default;
    };

    /** @brief Explicit finite validation bounds for one detached candidate. */
    struct AchievementDefinitionRegistryLimits final {
        std::uint32_t maximumDefinitions{2048};            /**< Maximum complete active achievement set. */
        std::uint32_t maximumLocalizationKeyBytes{128};    /**< Maximum bytes in either localization key. */
        std::uint32_t maximumProgressTotal{1'000'000'000}; /**< Maximum portable monotonic target. */
    };

    /** @brief Detached complete definition document awaiting transactional validation. */
    struct AchievementDefinitionRegistryCandidate final {
        std::uint32_t schemaVersion{AchievementDefinitionRegistrySchemaVersion}; /**< Exact document schema. */
        Sha256Digest stableIdRegistryFingerprint{};     /**< ADR-132 ledger generation referenced by every definition. */
        std::vector<AchievementDefinition> definitions; /**< Complete active achievement definition set. */
    };

    /** @brief Stable failures emitted by achievement definition validation. */
    namespace AchievementDefinitionErrors {
        extern const ErrorCodeDescriptor UnsupportedVersion;
        extern const ErrorCodeDescriptor CapacityExceeded;
        extern const ErrorCodeDescriptor InvalidDefinition;
        extern const ErrorCodeDescriptor DuplicateDefinition;
        extern const ErrorCodeDescriptor UnknownIdentity;
        extern const ErrorCodeDescriptor IncompleteRegistry;
        extern const ErrorCodeDescriptor StaleIdentityRegistry;
        extern const ErrorCodeDescriptor ImmutableContractChanged;
    }  // namespace AchievementDefinitionErrors

    /** @brief Immutable identity-sorted achievement definition snapshot. */
    class AchievementDefinitionRegistry final {
    public:
        /** @brief Returns the owning stable-ID project namespace. @return Borrowed immutable project ID. */
        [[nodiscard]] std::string_view StableIdProjectId() const noexcept;
        /** @brief Returns the referenced stable-ID ledger fingerprint. @return Borrowed immutable digest. */
        [[nodiscard]] const Sha256Digest &StableIdRegistryFingerprint() const noexcept;
        /** @brief Returns the canonical semantic definition fingerprint. @return Borrowed immutable digest. */
        [[nodiscard]] const Sha256Digest &Fingerprint() const noexcept;
        /** @brief Returns identity-sorted definitions. @return Borrowed complete definition span. */
        [[nodiscard]] std::span<const AchievementDefinition> Definitions() const noexcept;
        /** @brief Finds an exact achievement without fallback. @param id Active stable achievement identity. @return Snapshot-owned
         * definition or typed unknown failure. */
        [[nodiscard]] Result<const AchievementDefinition *> Find(AchievementId id) const;

    private:
        friend Result<AchievementDefinitionRegistry> BuildAchievementDefinitionRegistry(const PlatformStableIdRegistry &,
                                                                                        const AchievementDefinitionRegistryCandidate &,
                                                                                        const AchievementDefinitionRegistryLimits &);
        std::string stableIdProjectId_;
        Sha256Digest stableIdRegistryFingerprint_{};
        Sha256Digest fingerprint_{};
        std::vector<AchievementDefinition> definitions_;
    };

    /**
     * @brief Validates and builds one complete immutable achievement definition snapshot.
     * @param stableIds Captured ADR-132 identity ledger generation.
     * @param candidate Detached complete definition document.
     * @param limits Explicit finite validation bounds.
     * @return Canonical registry or a typed error containing one field-level diagnostic.
     */
    [[nodiscard]] Result<AchievementDefinitionRegistry> BuildAchievementDefinitionRegistry(
        const PlatformStableIdRegistry &stableIds, const AchievementDefinitionRegistryCandidate &candidate,
        const AchievementDefinitionRegistryLimits &limits = {});

    /**
     * @brief Builds a compatible complete replacement without mutating the prior snapshot.
     * @param previous Prior immutable registry retained by current consumers.
     * @param stableIds Captured current ADR-132 identity ledger generation.
     * @param candidate Detached complete replacement definitions.
     * @param limits Explicit finite validation bounds.
     * @return New registry, or a typed failure with `previous` unchanged.
     */
    [[nodiscard]] Result<AchievementDefinitionRegistry> BuildAchievementDefinitionRegistryReplacement(
        const AchievementDefinitionRegistry &previous, const PlatformStableIdRegistry &stableIds,
        const AchievementDefinitionRegistryCandidate &candidate, const AchievementDefinitionRegistryLimits &limits = {});
}  // namespace Horo::PlatformServices
