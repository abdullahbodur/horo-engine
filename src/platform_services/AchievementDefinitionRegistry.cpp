#include "Horo/PlatformServices/AchievementDefinitionRegistry.h"

#include "PlatformDefinitionRegistryDetail.h"

#include <algorithm>
#include <memory>
#include <new>
#include <utility>

namespace Horo::PlatformServices {
    using DefinitionRegistryDetail::DocumentError;
    using DefinitionRegistryDetail::FieldError;
    using DefinitionRegistryDetail::FingerprintWriter;
    using DefinitionRegistryDetail::IsLocalizationKey;
    using DefinitionRegistryDetail::IsZero;
    using DefinitionRegistryDetail::MakeDescriptor;

    namespace {
        constexpr std::string_view FingerprintDomain = "horo.platform-services.achievement-definitions.v1";
        constexpr std::uint32_t HardMaximumDefinitions = 4096;
        constexpr std::uint32_t HardMaximumLocalizationKeyBytes = 512;
        constexpr std::uint32_t HardMaximumProgressTotal = 1'000'000'000;

        [[nodiscard]] bool IsKnown(const ProgressionAuthorityMode mode) noexcept {
            return mode == ProgressionAuthorityMode::LocalProduct || mode == ProgressionAuthorityMode::AuthorityServer;
        }

        [[nodiscard]] bool IsKnown(const AchievementProgressKind kind) noexcept {
            return kind == AchievementProgressKind::UnlockOnce || kind == AchievementProgressKind::SetProgressMaximum;
        }

        [[nodiscard]] bool HasValidLimits(const AchievementDefinitionRegistryLimits &limits) noexcept {
            return limits.maximumDefinitions > 0 && limits.maximumDefinitions <= HardMaximumDefinitions &&
                   limits.maximumLocalizationKeyBytes > 0 && limits.maximumLocalizationKeyBytes <= HardMaximumLocalizationKeyBytes &&
                   limits.maximumProgressTotal > 0 && limits.maximumProgressTotal <= HardMaximumProgressTotal;
        }

        [[nodiscard]] bool HasValidProgress(const AchievementProgressSchema &progress,
                                            const AchievementDefinitionRegistryLimits &limits) noexcept {
            if (!IsKnown(progress.kind) || progress.total == 0 || progress.total > limits.maximumProgressTotal)
                return false;
            if (progress.kind == AchievementProgressKind::UnlockOnce)
                return progress.total == 1;
            return progress.total > 1;
        }

        [[nodiscard]] Sha256Digest Fingerprint(const Sha256Digest &stableIds, const std::span<const AchievementDefinition> definitions) {
            FingerprintWriter writer{96U + definitions.size() * 64U};
            writer.AddText(FingerprintDomain);
            writer.AddU32(AchievementDefinitionRegistrySchemaVersion);
            writer.AddDigest(stableIds);
            writer.AddU32(static_cast<std::uint32_t>(definitions.size()));
            for (const AchievementDefinition &definition : definitions) {
                writer.AddU64(definition.id.value);
                writer.AddByte(static_cast<std::uint8_t>(definition.authority));
                writer.AddByte(static_cast<std::uint8_t>(definition.progress.kind));
                writer.AddU32(definition.progress.total);
                writer.AddText(definition.presentation.titleLocalizationKey);
                writer.AddText(definition.presentation.descriptionLocalizationKey);
                writer.AddByte(definition.presentation.hidden ? 1U : 0U);
            }
            return writer.Finish();
        }

        [[nodiscard]] const PlatformStableIdDeclaration *FindLedgerEntry(const PlatformStableIdRegistry &stableIds,
                                                                         const AchievementId id) noexcept {
            const auto entries = stableIds.Entries();
            const auto found = std::ranges::find_if(entries, [id](const PlatformStableIdDeclaration &entry) {
                return entry.kind == PlatformServiceIdKind::Achievement && entry.storedId.value == id.value;
            });
            return found == entries.end() ? nullptr : std::to_address(found);
        }

        [[nodiscard]] Result<void> ValidateIdentity(const PlatformStableIdRegistry &stableIds, const AchievementDefinition &definition,
                                                    const std::size_t index) {
            if (!definition.id.IsValid())
                return Result<void>::Failure(
                    FieldError(AchievementDefinitionErrors::InvalidDefinition, index, "id", "Achievement ID must be nonzero."));
            if (const PlatformStableIdDeclaration *ledgerEntry = FindLedgerEntry(stableIds, definition.id);
                ledgerEntry == nullptr || ledgerEntry->state != PlatformStableIdState::Active)
                return Result<void>::Failure(FieldError(AchievementDefinitionErrors::UnknownIdentity, index, "id",
                                                        "Achievement ID must reference one active stable ledger entry."));
            return Result<void>::Success();
        }

        [[nodiscard]] Result<void> ValidateSemantics(const AchievementDefinitionRegistryLimits &limits,
                                                     const AchievementDefinition &definition, const std::size_t index) {
            if (!IsKnown(definition.authority))
                return Result<void>::Failure(FieldError(AchievementDefinitionErrors::InvalidDefinition, index, "authority",
                                                        "Achievement authority mode is unknown."));
            if (!HasValidProgress(definition.progress, limits))
                return Result<void>::Failure(FieldError(AchievementDefinitionErrors::InvalidDefinition, index, "progress",
                                                        "Achievement progress schema is invalid or exceeds its bound."));
            return Result<void>::Success();
        }

        [[nodiscard]] Result<void> ValidatePresentation(const AchievementDefinitionRegistryLimits &limits,
                                                        const AchievementDefinition &definition, const std::size_t index) {
            if (!IsLocalizationKey(definition.presentation.titleLocalizationKey, limits.maximumLocalizationKeyBytes))
                return Result<void>::Failure(FieldError(AchievementDefinitionErrors::InvalidDefinition, index,
                                                        "presentation.titleLocalizationKey",
                                                        "Achievement title localization key is malformed or unbounded."));
            if (!IsLocalizationKey(definition.presentation.descriptionLocalizationKey, limits.maximumLocalizationKeyBytes))
                return Result<void>::Failure(FieldError(AchievementDefinitionErrors::InvalidDefinition, index,
                                                        "presentation.descriptionLocalizationKey",
                                                        "Achievement description localization key is malformed or unbounded."));
            return Result<void>::Success();
        }

        [[nodiscard]] Result<void> ValidateDefinition(const PlatformStableIdRegistry &stableIds,
                                                      const AchievementDefinitionRegistryLimits &limits,
                                                      const AchievementDefinition &definition, const std::size_t index) {
            if (const Result<void> identity = ValidateIdentity(stableIds, definition, index); identity.HasError())
                return identity;
            if (const Result<void> semantics = ValidateSemantics(limits, definition, index); semantics.HasError())
                return semantics;
            return ValidatePresentation(limits, definition, index);
        }

        [[nodiscard]] Result<void> CanonicalizeDefinitions(const PlatformStableIdRegistry &stableIds,
                                                           const AchievementDefinitionRegistryLimits &limits,
                                                           std::vector<AchievementDefinition> &definitions) {
            for (std::size_t index = 0; index < definitions.size(); ++index) {
                if (const Result<void> valid = ValidateDefinition(stableIds, limits, definitions[index], index); valid.HasError())
                    return valid;
            }
            std::ranges::sort(definitions, {}, [](const AchievementDefinition &definition) {
                return definition.id.value;
            });
            const auto duplicate = std::ranges::adjacent_find(definitions, {}, [](const AchievementDefinition &definition) {
                return definition.id.value;
            });
            if (duplicate == definitions.end())
                return Result<void>::Success();
            return Result<void>::Failure(FieldError(AchievementDefinitionErrors::DuplicateDefinition,
                                                    static_cast<std::size_t>(std::distance(definitions.begin(), duplicate)) + 1U, "id",
                                                    "Achievement stable ID is duplicated."));
        }

        [[nodiscard]] Result<void> ValidateCompleteness(const PlatformStableIdRegistry &stableIds,
                                                        const std::span<const AchievementDefinition> definitions) {
            for (const PlatformStableIdDeclaration &entry : stableIds.Entries()) {
                if (entry.kind != PlatformServiceIdKind::Achievement || entry.state != PlatformStableIdState::Active)
                    continue;
                const auto found = std::ranges::lower_bound(definitions, entry.storedId.value, {}, [](const AchievementDefinition &value) {
                    return value.id.value;
                });
                if (found == definitions.end() || found->id.value != entry.storedId.value)
                    return Result<void>::Failure(DocumentError(AchievementDefinitionErrors::IncompleteRegistry, "definitions",
                                                               "Every active achievement stable ID requires one definition."));
            }
            return Result<void>::Success();
        }

        [[nodiscard]] bool ImmutableSemanticsMatch(const AchievementDefinition &left, const AchievementDefinition &right) noexcept {
            return left.authority == right.authority && left.progress == right.progress;
        }
    }  // namespace

    namespace AchievementDefinitionErrors {
        namespace {
            const ErrorDomainId Domain{"horo.platform.achievement-definition"};
        }

        const ErrorCodeDescriptor UnsupportedVersion =
            MakeDescriptor(Domain, "platform.achievement.version_unsupported", "Achievement definition schema version is unsupported.",
                           "Migrate the document to the supported schema.", true);
        const ErrorCodeDescriptor CapacityExceeded =
            MakeDescriptor(Domain, "platform.achievement.capacity_exceeded", "Achievement definition bounds were exceeded.",
                           "Reduce the definition document or use supported finite limits.", true);
        const ErrorCodeDescriptor InvalidDefinition =
            MakeDescriptor(Domain, "platform.achievement.definition_invalid", "Achievement definition is malformed or incomplete.",
                           "Correct the field identified by the diagnostic.", true);
        const ErrorCodeDescriptor DuplicateDefinition =
            MakeDescriptor(Domain, "platform.achievement.definition_duplicate", "Achievement stable ID is defined more than once.",
                           "Retain exactly one definition for the stable ID.", true);
        const ErrorCodeDescriptor UnknownIdentity =
            MakeDescriptor(Domain, "platform.achievement.identity_unknown", "Achievement definition references no active stable identity.",
                           "Use one active achievement ID from the captured ledger.", true);
        const ErrorCodeDescriptor IncompleteRegistry =
            MakeDescriptor(Domain, "platform.achievement.registry_incomplete", "An active achievement has no definition.",
                           "Define every active achievement before publication.", true);
        const ErrorCodeDescriptor StaleIdentityRegistry =
            MakeDescriptor(Domain, "platform.achievement.identity_registry_stale",
                           "Achievement definitions target a different stable-ID registry generation.",
                           "Rebuild definitions against the captured stable-ID fingerprint.", false);
        const ErrorCodeDescriptor ImmutableContractChanged =
            MakeDescriptor(Domain, "platform.achievement.immutable_contract_changed",
                           "Published achievement authority or progress semantics changed.",
                           "Create an explicit product migration instead of live replacement.", true);
    }  // namespace AchievementDefinitionErrors

    /** @copydoc AchievementDefinitionRegistry::StableIdProjectId */
    std::string_view AchievementDefinitionRegistry::StableIdProjectId() const noexcept {
        return stableIdProjectId_;
    }

    /** @copydoc AchievementDefinitionRegistry::StableIdRegistryFingerprint */
    const Sha256Digest &AchievementDefinitionRegistry::StableIdRegistryFingerprint() const noexcept {
        return stableIdRegistryFingerprint_;
    }

    /** @copydoc AchievementDefinitionRegistry::Fingerprint */
    const Sha256Digest &AchievementDefinitionRegistry::Fingerprint() const noexcept {
        return fingerprint_;
    }

    /** @copydoc AchievementDefinitionRegistry::Definitions */
    std::span<const AchievementDefinition> AchievementDefinitionRegistry::Definitions() const noexcept {
        return definitions_;
    }

    /** @copydoc AchievementDefinitionRegistry::Find */
    Result<const AchievementDefinition *> AchievementDefinitionRegistry::Find(const AchievementId id) const {
        const auto found = std::ranges::lower_bound(definitions_, id.value, {}, [](const AchievementDefinition &definition) {
            return definition.id.value;
        });
        if (!id.IsValid() || found == definitions_.end() || found->id != id)
            return Result<const AchievementDefinition *>::Failure(MakeError(AchievementDefinitionErrors::UnknownIdentity));
        return Result<const AchievementDefinition *>::Success(std::to_address(found));
    }

    /** @copydoc BuildAchievementDefinitionRegistry */
    Result<AchievementDefinitionRegistry> BuildAchievementDefinitionRegistry(const PlatformStableIdRegistry &stableIds,
                                                                             const AchievementDefinitionRegistryCandidate &candidate,
                                                                             const AchievementDefinitionRegistryLimits &limits) {
        try {
            if (!HasValidLimits(limits))
                return Result<AchievementDefinitionRegistry>::Failure(
                    DocumentError(AchievementDefinitionErrors::CapacityExceeded, "limits", "Achievement limits are zero or unsupported."));
            if (candidate.schemaVersion != AchievementDefinitionRegistrySchemaVersion)
                return Result<AchievementDefinitionRegistry>::Failure(DocumentError(AchievementDefinitionErrors::UnsupportedVersion,
                                                                                    "schemaVersion",
                                                                                    "Achievement schema version is unsupported."));
            if (IsZero(candidate.stableIdRegistryFingerprint) || candidate.stableIdRegistryFingerprint != stableIds.Fingerprint())
                return Result<AchievementDefinitionRegistry>::Failure(DocumentError(AchievementDefinitionErrors::StaleIdentityRegistry,
                                                                                    "stableIdRegistryFingerprint",
                                                                                    "Stable-ID registry fingerprint is missing or stale."));
            if (candidate.definitions.size() > limits.maximumDefinitions)
                return Result<AchievementDefinitionRegistry>::Failure(DocumentError(AchievementDefinitionErrors::CapacityExceeded,
                                                                                    "definitions",
                                                                                    "Achievement definition count exceeds its bound."));

            AchievementDefinitionRegistry registry;
            registry.stableIdProjectId_ = stableIds.ProjectId();
            registry.stableIdRegistryFingerprint_ = candidate.stableIdRegistryFingerprint;
            registry.definitions_ = candidate.definitions;
            if (const Result<void> canonical = CanonicalizeDefinitions(stableIds, limits, registry.definitions_); canonical.HasError())
                return Result<AchievementDefinitionRegistry>::Failure(canonical.ErrorValue());
            if (const Result<void> complete = ValidateCompleteness(stableIds, registry.definitions_); complete.HasError())
                return Result<AchievementDefinitionRegistry>::Failure(complete.ErrorValue());
            registry.fingerprint_ = Fingerprint(registry.stableIdRegistryFingerprint_, registry.definitions_);
            return Result<AchievementDefinitionRegistry>::Success(std::move(registry));
        } catch (const std::bad_alloc &) {
            return Result<AchievementDefinitionRegistry>::Failure(MakeError(AchievementDefinitionErrors::CapacityExceeded));
        }
    }

    /** @copydoc BuildAchievementDefinitionRegistryReplacement */
    Result<AchievementDefinitionRegistry> BuildAchievementDefinitionRegistryReplacement(
        const AchievementDefinitionRegistry &previous, const PlatformStableIdRegistry &stableIds,
        const AchievementDefinitionRegistryCandidate &candidate, const AchievementDefinitionRegistryLimits &limits) {
        if (previous.StableIdProjectId() != stableIds.ProjectId())
            return Result<AchievementDefinitionRegistry>::Failure(
                DocumentError(AchievementDefinitionErrors::StaleIdentityRegistry, "stableIdRegistryFingerprint",
                              "Achievement definitions cannot cross project identity namespaces."));
        Result<AchievementDefinitionRegistry> replacement = BuildAchievementDefinitionRegistry(stableIds, candidate, limits);
        if (replacement.HasError())
            return replacement;
        for (const AchievementDefinition &prior : previous.Definitions()) {
            if (const auto current = replacement.Value().Find(prior.id); current.HasValue()) {
                if (!ImmutableSemanticsMatch(prior, *current.Value()))
                    return Result<AchievementDefinitionRegistry>::Failure(MakeError(AchievementDefinitionErrors::ImmutableContractChanged));
                continue;
            }
            const PlatformStableIdDeclaration *ledgerEntry = FindLedgerEntry(stableIds, prior.id);
            if (ledgerEntry == nullptr || ledgerEntry->state != PlatformStableIdState::Tombstoned)
                return Result<AchievementDefinitionRegistry>::Failure(MakeError(AchievementDefinitionErrors::ImmutableContractChanged));
        }
        return replacement;
    }
}  // namespace Horo::PlatformServices
