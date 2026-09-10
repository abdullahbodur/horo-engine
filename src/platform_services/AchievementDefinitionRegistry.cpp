#include "Horo/PlatformServices/AchievementDefinitionRegistry.h"

#include <algorithm>
#include <memory>
#include <new>
#include <utility>

namespace Horo::PlatformServices {
    namespace {
        constexpr std::string_view FingerprintDomain = "horo.platform-services.achievement-definitions.v1";
        constexpr std::uint32_t HardMaximumDefinitions = 4096;
        constexpr std::uint32_t HardMaximumLocalizationKeyBytes = 512;
        constexpr std::uint32_t HardMaximumProgressTotal = 1'000'000'000;

        [[nodiscard]] bool IsZero(const Sha256Digest &digest) noexcept {
            return std::ranges::all_of(digest.bytes, [](const std::uint8_t byte) {
                return byte == 0;
            });
        }

        [[nodiscard]] bool IsLocalizationKeyCharacter(const char character) noexcept {
            return (character >= 'a' && character <= 'z') || (character >= '0' && character <= '9') || character == '_' ||
                   character == '.' || character == '-';
        }

        [[nodiscard]] bool IsLocalizationKey(const std::string_view key, const std::uint32_t maximumBytes) noexcept {
            if (key.empty() || key.size() > maximumBytes || key.front() < 'a' || key.front() > 'z')
                return false;
            return std::ranges::all_of(key.substr(1), IsLocalizationKeyCharacter);
        }

        [[nodiscard]] bool IsKnown(const ProgressionAuthorityMode mode) noexcept {
            return mode == ProgressionAuthorityMode::LocalProduct || mode == ProgressionAuthorityMode::AuthorityServer;
        }

        [[nodiscard]] bool IsKnown(const AchievementProgressKind kind) noexcept {
            return kind == AchievementProgressKind::UnlockOnce || kind == AchievementProgressKind::SetProgressMaximum;
        }

        [[nodiscard]] Error FieldError(const ErrorCodeDescriptor &descriptor, const std::size_t index, const std::string_view field,
                                       const std::string_view message) {
            Error error = MakeError(descriptor);
            error.diagnostics.push_back({.code = DiagnosticCode{descriptor.code.Value()},
                                         .severity = DiagnosticSeverity::Error,
                                         .message = std::string{message},
                                         .location = {.source = "definitions[" + std::to_string(index) + "]." + std::string{field}}});
            return error;
        }

        [[nodiscard]] Error DocumentError(const ErrorCodeDescriptor &descriptor, const std::string_view field,
                                          const std::string_view message) {
            Error error = MakeError(descriptor);
            error.diagnostics.push_back({.code = DiagnosticCode{descriptor.code.Value()},
                                         .severity = DiagnosticSeverity::Error,
                                         .message = std::string{message},
                                         .location = {.source = std::string{field}}});
            return error;
        }

        void AppendU8(std::vector<std::byte> &bytes, const std::uint8_t value) {
            bytes.push_back(static_cast<std::byte>(value));
        }

        void AppendU32(std::vector<std::byte> &bytes, const std::uint32_t value) {
            for (int shift = 24; shift >= 0; shift -= 8)
                AppendU8(bytes, static_cast<std::uint8_t>(value >> static_cast<unsigned>(shift)));
        }

        void AppendU64(std::vector<std::byte> &bytes, const std::uint64_t value) {
            for (int shift = 56; shift >= 0; shift -= 8)
                AppendU8(bytes, static_cast<std::uint8_t>(value >> static_cast<unsigned>(shift)));
        }

        void AppendText(std::vector<std::byte> &bytes, const std::string_view text) {
            AppendU32(bytes, static_cast<std::uint32_t>(text.size()));
            for (const char character : text)
                AppendU8(bytes, static_cast<std::uint8_t>(character));
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
            std::vector<std::byte> bytes;
            bytes.reserve(96U + definitions.size() * 64U);
            AppendText(bytes, FingerprintDomain);
            AppendU32(bytes, AchievementDefinitionRegistrySchemaVersion);
            for (const std::uint8_t byte : stableIds.bytes)
                AppendU8(bytes, byte);
            AppendU32(bytes, static_cast<std::uint32_t>(definitions.size()));
            for (const AchievementDefinition &definition : definitions) {
                AppendU64(bytes, definition.id.value);
                AppendU8(bytes, static_cast<std::uint8_t>(definition.authority));
                AppendU8(bytes, static_cast<std::uint8_t>(definition.progress.kind));
                AppendU32(bytes, definition.progress.total);
                AppendText(bytes, definition.presentation.titleLocalizationKey);
                AppendText(bytes, definition.presentation.descriptionLocalizationKey);
                AppendU8(bytes, definition.presentation.hidden ? 1U : 0U);
            }
            return ComputeSha256(bytes);
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
            const PlatformStableIdDeclaration *ledgerEntry = FindLedgerEntry(stableIds, definition.id);
            if (ledgerEntry == nullptr || ledgerEntry->state != PlatformStableIdState::Active)
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

        const ErrorCodeDescriptor UnsupportedVersion{Domain,
                                                     ErrorCode{"platform.achievement.version_unsupported"},
                                                     ErrorSeverity::Error,
                                                     "Achievement definition schema version is unsupported.",
                                                     "Migrate the document to the supported schema.",
                                                     false,
                                                     true};
        const ErrorCodeDescriptor CapacityExceeded{Domain,
                                                   ErrorCode{"platform.achievement.capacity_exceeded"},
                                                   ErrorSeverity::Error,
                                                   "Achievement definition bounds were exceeded.",
                                                   "Reduce the definition document or use supported finite limits.",
                                                   false,
                                                   true};
        const ErrorCodeDescriptor InvalidDefinition{Domain,
                                                    ErrorCode{"platform.achievement.definition_invalid"},
                                                    ErrorSeverity::Error,
                                                    "Achievement definition is malformed or incomplete.",
                                                    "Correct the field identified by the diagnostic.",
                                                    false,
                                                    true};
        const ErrorCodeDescriptor DuplicateDefinition{Domain,
                                                      ErrorCode{"platform.achievement.definition_duplicate"},
                                                      ErrorSeverity::Error,
                                                      "Achievement stable ID is defined more than once.",
                                                      "Retain exactly one definition for the stable ID.",
                                                      false,
                                                      true};
        const ErrorCodeDescriptor UnknownIdentity{Domain,
                                                  ErrorCode{"platform.achievement.identity_unknown"},
                                                  ErrorSeverity::Error,
                                                  "Achievement definition references no active stable identity.",
                                                  "Use one active achievement ID from the captured ledger.",
                                                  false,
                                                  true};
        const ErrorCodeDescriptor IncompleteRegistry{Domain,
                                                     ErrorCode{"platform.achievement.registry_incomplete"},
                                                     ErrorSeverity::Error,
                                                     "An active achievement has no definition.",
                                                     "Define every active achievement before publication.",
                                                     false,
                                                     true};
        const ErrorCodeDescriptor StaleIdentityRegistry{Domain,
                                                        ErrorCode{"platform.achievement.identity_registry_stale"},
                                                        ErrorSeverity::Error,
                                                        "Achievement definitions target a different stable-ID registry generation.",
                                                        "Rebuild definitions against the captured stable-ID fingerprint.",
                                                        false,
                                                        false};
        const ErrorCodeDescriptor ImmutableContractChanged{Domain,
                                                           ErrorCode{"platform.achievement.immutable_contract_changed"},
                                                           ErrorSeverity::Error,
                                                           "Published achievement authority or progress semantics changed.",
                                                           "Create an explicit product migration instead of live replacement.",
                                                           false,
                                                           true};
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
            const auto current = replacement.Value().Find(prior.id);
            if (current.HasValue()) {
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
