#include "Horo/PlatformServices/PlatformDefinitionRegistries.h"

#include <algorithm>
#include <format>
#include <memory>
#include <new>
#include <utility>

namespace Horo::PlatformServices {
    namespace {
        constexpr std::uint32_t HardMaximumDefinitions = 4096;
        constexpr std::uint32_t HardMaximumLocalizationKeyBytes = 512;
        constexpr std::uint32_t HardMaximumPresenceDetailBytes = 4096;

        [[nodiscard]] bool IsZero(const Sha256Digest &digest) noexcept {
            return std::ranges::all_of(digest.bytes, [](const std::uint8_t byte) {
                return byte == 0;
            });
        }

        [[nodiscard]] bool IsLocalizationCharacter(const char value) noexcept {
            return (value >= 'a' && value <= 'z') || (value >= '0' && value <= '9') || value == '_' || value == '.' || value == '-';
        }

        [[nodiscard]] bool IsLocalizationKey(const std::string_view key, const std::uint32_t maximumBytes) noexcept {
            if (key.empty() || key.size() > maximumBytes || key.front() < 'a' || key.front() > 'z')
                return false;
            return std::ranges::all_of(key.substr(1), IsLocalizationCharacter);
        }

        [[nodiscard]] bool HasValidLimits(const PlatformDefinitionRegistryLimits &limits) noexcept {
            return limits.maximumDefinitions > 0 && limits.maximumDefinitions <= HardMaximumDefinitions &&
                   limits.maximumLocalizationKeyBytes > 0 && limits.maximumLocalizationKeyBytes <= HardMaximumLocalizationKeyBytes &&
                   limits.maximumPresenceDetailBytes > 0 && limits.maximumPresenceDetailBytes <= HardMaximumPresenceDetailBytes;
        }

        [[nodiscard]] bool IsKnown(const ProgressionAuthorityMode value) noexcept {
            return value == ProgressionAuthorityMode::LocalProduct || value == ProgressionAuthorityMode::AuthorityServer;
        }

        [[nodiscard]] bool IsKnown(const ProgressionValueKind value) noexcept {
            return value == ProgressionValueKind::SignedInteger64 || value == ProgressionValueKind::UnsignedInteger64;
        }

        [[nodiscard]] bool IsKnown(const StatMutationPolicy value) noexcept {
            return value >= StatMutationPolicy::SetMaximum && value <= StatMutationPolicy::AddOnce;
        }

        [[nodiscard]] bool IsKnown(const LeaderboardOrdering value) noexcept {
            return value == LeaderboardOrdering::HighestFirst || value == LeaderboardOrdering::LowestFirst;
        }

        [[nodiscard]] bool IsKnown(const PresenceDetailPolicy value) noexcept {
            return value == PresenceDetailPolicy::Forbidden || value == PresenceDetailPolicy::Optional;
        }

        [[nodiscard]] Error DiagnosticError(const ErrorCodeDescriptor &descriptor, std::string source, const std::string_view message) {
            Error error = MakeError(descriptor);
            error.diagnostics.push_back({.code = DiagnosticCode{descriptor.code.Value()},
                                         .severity = DiagnosticSeverity::Error,
                                         .message = std::string{message},
                                         .location = {.source = std::move(source)}});
            return error;
        }

        [[nodiscard]] Error FieldError(const ErrorCodeDescriptor &descriptor, const std::size_t index, const std::string_view field,
                                       const std::string_view message) {
            return DiagnosticError(descriptor, std::format("definitions[{}].{}", index, field), message);
        }

        [[nodiscard]] Error DocumentError(const ErrorCodeDescriptor &descriptor, const std::string_view field,
                                          const std::string_view message) {
            return DiagnosticError(descriptor, std::string{field}, message);
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

        void AppendText(std::vector<std::byte> &bytes, const std::string_view value) {
            AppendU32(bytes, static_cast<std::uint32_t>(value.size()));
            for (const char character : value)
                AppendU8(bytes, static_cast<std::uint8_t>(character));
        }

        void AppendDigest(std::vector<std::byte> &bytes, const Sha256Digest &digest) {
            for (const std::uint8_t byte : digest.bytes)
                AppendU8(bytes, byte);
        }

        void AppendRange(std::vector<std::byte> &bytes, const ProgressionNumericRange &range) {
            AppendU64(bytes, static_cast<std::uint64_t>(range.minimum));
            AppendU64(bytes, static_cast<std::uint64_t>(range.maximum));
        }

        [[nodiscard]] Sha256Digest Fingerprint(const Sha256Digest &stableIds, const std::span<const StatDefinition> definitions) {
            std::vector<std::byte> bytes;
            bytes.reserve(64U + definitions.size() * 48U);
            AppendText(bytes, "horo.platform-services.stat-definitions.v1");
            AppendDigest(bytes, stableIds);
            AppendU32(bytes, static_cast<std::uint32_t>(definitions.size()));
            for (const StatDefinition &definition : definitions) {
                AppendU64(bytes, definition.id.value);
                AppendU8(bytes, static_cast<std::uint8_t>(definition.authority));
                AppendU8(bytes, static_cast<std::uint8_t>(definition.valueKind));
                AppendRange(bytes, definition.range);
                AppendU8(bytes, static_cast<std::uint8_t>(definition.mutation));
                AppendText(bytes, definition.localizationKey);
                AppendU8(bytes, definition.hidden ? 1U : 0U);
            }
            return ComputeSha256(bytes);
        }

        [[nodiscard]] Sha256Digest Fingerprint(const Sha256Digest &stableIds, const std::span<const LeaderboardDefinition> definitions) {
            std::vector<std::byte> bytes;
            bytes.reserve(64U + definitions.size() * 56U);
            AppendText(bytes, "horo.platform-services.leaderboard-definitions.v1");
            AppendDigest(bytes, stableIds);
            AppendU32(bytes, static_cast<std::uint32_t>(definitions.size()));
            for (const LeaderboardDefinition &definition : definitions) {
                AppendU64(bytes, definition.id.value);
                AppendU8(bytes, static_cast<std::uint8_t>(definition.authority));
                AppendU8(bytes, static_cast<std::uint8_t>(definition.valueKind));
                AppendRange(bytes, definition.range);
                AppendU8(bytes, static_cast<std::uint8_t>(definition.ordering));
                AppendU8(bytes, definition.sourceStat.has_value() ? 1U : 0U);
                if (definition.sourceStat)
                    AppendU64(bytes, definition.sourceStat->value);
                AppendText(bytes, definition.localizationKey);
                AppendU8(bytes, definition.hidden ? 1U : 0U);
            }
            return ComputeSha256(bytes);
        }

        [[nodiscard]] Sha256Digest Fingerprint(const Sha256Digest &stableIds, const std::span<const PresenceDefinition> definitions) {
            std::vector<std::byte> bytes;
            bytes.reserve(64U + definitions.size() * 32U);
            AppendText(bytes, "horo.platform-services.presence-definitions.v1");
            AppendDigest(bytes, stableIds);
            AppendU32(bytes, static_cast<std::uint32_t>(definitions.size()));
            for (const PresenceDefinition &definition : definitions) {
                AppendU64(bytes, definition.id.value);
                AppendU8(bytes, static_cast<std::uint8_t>(definition.detailPolicy));
                AppendU32(bytes, definition.maximumDetailUtf8Bytes);
                AppendText(bytes, definition.localizationKey);
                AppendU8(bytes, definition.hidden ? 1U : 0U);
            }
            return ComputeSha256(bytes);
        }

        [[nodiscard]] const PlatformStableIdDeclaration *FindLedgerEntry(const PlatformStableIdRegistry &stableIds,
                                                                         const PlatformServiceIdKind kind,
                                                                         const std::uint64_t id) noexcept {
            const auto entries = stableIds.Entries();
            const auto found = std::ranges::find_if(entries, [kind, id](const PlatformStableIdDeclaration &entry) {
                return entry.kind == kind && entry.storedId.value == id;
            });
            return found == entries.end() ? nullptr : std::to_address(found);
        }

        template <typename Definition>
        [[nodiscard]] Result<void> ValidateIdentity(const PlatformStableIdRegistry &stableIds, const PlatformServiceIdKind kind,
                                                    const Definition &definition, const std::size_t index) {
            if (!definition.id.IsValid())
                return Result<void>::Failure(
                    FieldError(PlatformDefinitionErrors::InvalidDefinition, index, "id", "Stable definition ID must be nonzero."));
            if (const PlatformStableIdDeclaration *entry = FindLedgerEntry(stableIds, kind, definition.id.value);
                entry == nullptr || entry->state != PlatformStableIdState::Active)
                return Result<void>::Failure(FieldError(PlatformDefinitionErrors::UnknownIdentity, index, "id",
                                                        "Definition ID must reference one active ledger entry of the correct kind."));
            return Result<void>::Success();
        }

        [[nodiscard]] bool HasValidRange(const ProgressionValueKind kind, const ProgressionNumericRange range) noexcept {
            return range.minimum <= range.maximum && (kind != ProgressionValueKind::UnsignedInteger64 || range.minimum >= 0);
        }

        [[nodiscard]] bool HasValidLeaderboardSemantics(const LeaderboardDefinition &definition) noexcept {
            return IsKnown(definition.authority) && IsKnown(definition.valueKind) &&
                   HasValidRange(definition.valueKind, definition.range) && IsKnown(definition.ordering);
        }

        [[nodiscard]] Result<void> ValidateSourceStat(const StatDefinitionRegistry &stats, const LeaderboardDefinition &definition,
                                                      const std::size_t index) {
            if (!definition.sourceStat)
                return Result<void>::Success();
            const auto source = stats.Find(*definition.sourceStat);
            if (source.HasError() || source.Value()->valueKind != definition.valueKind ||
                source.Value()->range.minimum > definition.range.minimum || source.Value()->range.maximum < definition.range.maximum)
                return Result<void>::Failure(FieldError(PlatformDefinitionErrors::InvalidCrossReference, index, "sourceStat",
                                                        "Source stat must exist with a compatible kind and covering range."));
            return Result<void>::Success();
        }

        [[nodiscard]] Result<void> ValidateDefinition(const PlatformStableIdRegistry &stableIds,
                                                      const PlatformDefinitionRegistryLimits &limits, const StatDefinition &definition,
                                                      const std::size_t index) {
            if (const auto identity = ValidateIdentity(stableIds, PlatformServiceIdKind::Stat, definition, index); identity.HasError())
                return identity;
            if (!IsKnown(definition.authority))
                return Result<void>::Failure(
                    FieldError(PlatformDefinitionErrors::InvalidDefinition, index, "authority", "Stat authority mode is unknown."));
            if (!IsKnown(definition.valueKind) || !HasValidRange(definition.valueKind, definition.range))
                return Result<void>::Failure(FieldError(PlatformDefinitionErrors::InvalidDefinition, index, "range",
                                                        "Stat numeric kind or inclusive range is invalid."));
            if (!IsKnown(definition.mutation))
                return Result<void>::Failure(
                    FieldError(PlatformDefinitionErrors::InvalidDefinition, index, "mutation", "Stat mutation policy is unknown."));
            if (!IsLocalizationKey(definition.localizationKey, limits.maximumLocalizationKeyBytes))
                return Result<void>::Failure(FieldError(PlatformDefinitionErrors::InvalidDefinition, index, "localizationKey",
                                                        "Stat localization key is malformed or unbounded."));
            return Result<void>::Success();
        }

        [[nodiscard]] Result<void> ValidateDefinition(const PlatformStableIdRegistry &stableIds, const StatDefinitionRegistry &stats,
                                                      const PlatformDefinitionRegistryLimits &limits,
                                                      const LeaderboardDefinition &definition, const std::size_t index) {
            if (const auto identity = ValidateIdentity(stableIds, PlatformServiceIdKind::Leaderboard, definition, index);
                identity.HasError())
                return identity;
            if (!HasValidLeaderboardSemantics(definition))
                return Result<void>::Failure(FieldError(PlatformDefinitionErrors::InvalidDefinition, index, "semantics",
                                                        "Leaderboard authority, numeric range, or ordering is invalid."));
            if (!IsLocalizationKey(definition.localizationKey, limits.maximumLocalizationKeyBytes))
                return Result<void>::Failure(FieldError(PlatformDefinitionErrors::InvalidDefinition, index, "localizationKey",
                                                        "Leaderboard localization key is malformed or unbounded."));
            return ValidateSourceStat(stats, definition, index);
        }

        [[nodiscard]] Result<void> ValidateDefinition(const PlatformStableIdRegistry &stableIds,
                                                      const PlatformDefinitionRegistryLimits &limits, const PresenceDefinition &definition,
                                                      const std::size_t index) {
            if (const auto identity = ValidateIdentity(stableIds, PlatformServiceIdKind::PresenceStatus, definition, index);
                identity.HasError())
                return identity;
            if (!IsKnown(definition.detailPolicy) ||
                (definition.detailPolicy == PresenceDetailPolicy::Forbidden && definition.maximumDetailUtf8Bytes != 0) ||
                (definition.detailPolicy == PresenceDetailPolicy::Optional &&
                 (definition.maximumDetailUtf8Bytes == 0 || definition.maximumDetailUtf8Bytes > limits.maximumPresenceDetailBytes)))
                return Result<void>::Failure(FieldError(PlatformDefinitionErrors::InvalidDefinition, index, "maximumDetailUtf8Bytes",
                                                        "Presence detail policy and finite bound are inconsistent."));
            if (!IsLocalizationKey(definition.localizationKey, limits.maximumLocalizationKeyBytes))
                return Result<void>::Failure(FieldError(PlatformDefinitionErrors::InvalidDefinition, index, "localizationKey",
                                                        "Presence localization key is malformed or unbounded."));
            return Result<void>::Success();
        }

        template <typename Definition, typename Validator>
        [[nodiscard]] Result<void> Canonicalize(std::vector<Definition> &definitions, Validator &&validate) {
            for (std::size_t index = 0; index < definitions.size(); ++index) {
                if (const Result<void> valid = validate(definitions[index], index); valid.HasError())
                    return valid;
            }
            std::ranges::sort(definitions, {}, [](const Definition &definition) {
                return definition.id.value;
            });
            const auto duplicate = std::ranges::adjacent_find(definitions, {}, [](const Definition &definition) {
                return definition.id.value;
            });
            if (duplicate == definitions.end())
                return Result<void>::Success();
            return Result<void>::Failure(FieldError(PlatformDefinitionErrors::DuplicateDefinition,
                                                    static_cast<std::size_t>(std::distance(definitions.begin(), duplicate)) + 1U, "id",
                                                    "Stable definition ID is duplicated."));
        }

        template <typename Definition>
        [[nodiscard]] Result<void> ValidateCompleteness(const PlatformStableIdRegistry &stableIds, const PlatformServiceIdKind kind,
                                                        const std::span<const Definition> definitions) {
            for (const PlatformStableIdDeclaration &entry : stableIds.Entries()) {
                if (entry.kind != kind || entry.state != PlatformStableIdState::Active)
                    continue;
                const auto found = std::ranges::lower_bound(definitions, entry.storedId.value, {}, [](const Definition &definition) {
                    return definition.id.value;
                });
                if (found == definitions.end() || found->id.value != entry.storedId.value)
                    return Result<void>::Failure(DocumentError(PlatformDefinitionErrors::IncompleteRegistry, "definitions",
                                                               "Every active stable identity requires exactly one definition."));
            }
            return Result<void>::Success();
        }

        template <typename Candidate>
        [[nodiscard]] Result<void> ValidateDocument(const PlatformStableIdRegistry &stableIds, const Candidate &candidate,
                                                    const PlatformDefinitionRegistryLimits &limits) {
            if (!HasValidLimits(limits))
                return Result<void>::Failure(
                    DocumentError(PlatformDefinitionErrors::CapacityExceeded, "limits", "Definition limits are zero or unsupported."));
            if (candidate.schemaVersion != PlatformDefinitionRegistrySchemaVersion)
                return Result<void>::Failure(DocumentError(PlatformDefinitionErrors::UnsupportedVersion, "schemaVersion",
                                                           "Definition schema version is unsupported."));
            if (IsZero(candidate.stableIdRegistryFingerprint) || candidate.stableIdRegistryFingerprint != stableIds.Fingerprint())
                return Result<void>::Failure(DocumentError(PlatformDefinitionErrors::StaleIdentityRegistry, "stableIdRegistryFingerprint",
                                                           "Stable-ID registry fingerprint is missing or stale."));
            if (candidate.definitions.size() > limits.maximumDefinitions)
                return Result<void>::Failure(
                    DocumentError(PlatformDefinitionErrors::CapacityExceeded, "definitions", "Definition count exceeds its finite bound."));
            return Result<void>::Success();
        }

        template <typename Definition>
        [[nodiscard]] bool WasTombstoned(const PlatformStableIdRegistry &stableIds, const PlatformServiceIdKind kind,
                                         const Definition &prior) noexcept {
            const PlatformStableIdDeclaration *entry = FindLedgerEntry(stableIds, kind, prior.id.value);
            return entry != nullptr && entry->state == PlatformStableIdState::Tombstoned;
        }

        template <typename Definition, typename Id>
        [[nodiscard]] Result<const Definition *> FindDefinition(const std::span<const Definition> definitions, const Id id) {
            const auto found = std::ranges::lower_bound(definitions, id.value, {}, [](const Definition &definition) {
                return definition.id.value;
            });
            if (!id.IsValid() || found == definitions.end() || found->id != id)
                return Result<const Definition *>::Failure(MakeError(PlatformDefinitionErrors::UnknownIdentity));
            return Result<const Definition *>::Success(std::to_address(found));
        }

        template <typename Definition> struct CanonicalSnapshot final {
            std::string projectId;
            Sha256Digest stableFingerprint{};
            Sha256Digest fingerprint{};
            std::vector<Definition> definitions;
        };

        template <typename Definition, typename Candidate, typename Validator>
        [[nodiscard]] Result<CanonicalSnapshot<Definition>> BuildCanonicalSnapshot(const PlatformStableIdRegistry &stableIds,
                                                                                   const Candidate &candidate,
                                                                                   const PlatformDefinitionRegistryLimits &limits,
                                                                                   const PlatformServiceIdKind kind,
                                                                                   Validator &&validator) {
            try {
                if (const auto document = ValidateDocument(stableIds, candidate, limits); document.HasError())
                    return Result<CanonicalSnapshot<Definition>>::Failure(document.ErrorValue());
                CanonicalSnapshot<Definition> snapshot{.projectId = std::string{stableIds.ProjectId()},
                                                       .stableFingerprint = candidate.stableIdRegistryFingerprint,
                                                       .definitions = candidate.definitions};
                if (const auto canonical = Canonicalize(snapshot.definitions, std::forward<Validator>(validator)); canonical.HasError())
                    return Result<CanonicalSnapshot<Definition>>::Failure(canonical.ErrorValue());
                if (const auto complete = ValidateCompleteness(stableIds, kind, std::span<const Definition>{snapshot.definitions});
                    complete.HasError())
                    return Result<CanonicalSnapshot<Definition>>::Failure(complete.ErrorValue());
                snapshot.fingerprint = Fingerprint(snapshot.stableFingerprint, std::span<const Definition>{snapshot.definitions});
                return Result<CanonicalSnapshot<Definition>>::Success(std::move(snapshot));
            } catch (const std::bad_alloc &) {
                return Result<CanonicalSnapshot<Definition>>::Failure(MakeError(PlatformDefinitionErrors::CapacityExceeded));
            }
        }

        template <typename Definition, typename Registry, typename Compatible>
        [[nodiscard]] Result<void> ValidateReplacement(const std::span<const Definition> previous, const Registry &replacement,
                                                       const PlatformStableIdRegistry &stableIds, const PlatformServiceIdKind kind,
                                                       Compatible &&compatible) {
            for (const Definition &prior : previous) {
                if (const auto current = replacement.Find(prior.id); current.HasValue()) {
                    if (!compatible(prior, *current.Value()))
                        return Result<void>::Failure(MakeError(PlatformDefinitionErrors::ImmutableContractChanged));
                } else if (!WasTombstoned(stableIds, kind, prior)) {
                    return Result<void>::Failure(MakeError(PlatformDefinitionErrors::ImmutableContractChanged));
                }
            }
            return Result<void>::Success();
        }
    }  // namespace

    /** @copydoc StatDefinitionRegistry::StableIdProjectId */
    std::string_view StatDefinitionRegistry::StableIdProjectId() const noexcept {
        return projectId_;
    }

    /** @copydoc StatDefinitionRegistry::StableIdRegistryFingerprint */
    const Sha256Digest &StatDefinitionRegistry::StableIdRegistryFingerprint() const noexcept {
        return stableFingerprint_;
    }

    /** @copydoc StatDefinitionRegistry::Fingerprint */
    const Sha256Digest &StatDefinitionRegistry::Fingerprint() const noexcept {
        return fingerprint_;
    }

    /** @copydoc StatDefinitionRegistry::Definitions */
    std::span<const StatDefinition> StatDefinitionRegistry::Definitions() const noexcept {
        return definitions_;
    }

    /** @copydoc StatDefinitionRegistry::Find */
    Result<const StatDefinition *> StatDefinitionRegistry::Find(const StatId id) const {
        return FindDefinition(std::span<const StatDefinition>{definitions_}, id);
    }

    /** @copydoc LeaderboardDefinitionRegistry::StableIdProjectId */
    std::string_view LeaderboardDefinitionRegistry::StableIdProjectId() const noexcept {
        return projectId_;
    }

    /** @copydoc LeaderboardDefinitionRegistry::StableIdRegistryFingerprint */
    const Sha256Digest &LeaderboardDefinitionRegistry::StableIdRegistryFingerprint() const noexcept {
        return stableFingerprint_;
    }

    /** @copydoc LeaderboardDefinitionRegistry::Fingerprint */
    const Sha256Digest &LeaderboardDefinitionRegistry::Fingerprint() const noexcept {
        return fingerprint_;
    }

    /** @copydoc LeaderboardDefinitionRegistry::Definitions */
    std::span<const LeaderboardDefinition> LeaderboardDefinitionRegistry::Definitions() const noexcept {
        return definitions_;
    }

    /** @copydoc LeaderboardDefinitionRegistry::Find */
    Result<const LeaderboardDefinition *> LeaderboardDefinitionRegistry::Find(const LeaderboardId id) const {
        return FindDefinition(std::span<const LeaderboardDefinition>{definitions_}, id);
    }

    /** @copydoc PresenceDefinitionRegistry::StableIdProjectId */
    std::string_view PresenceDefinitionRegistry::StableIdProjectId() const noexcept {
        return projectId_;
    }

    /** @copydoc PresenceDefinitionRegistry::StableIdRegistryFingerprint */
    const Sha256Digest &PresenceDefinitionRegistry::StableIdRegistryFingerprint() const noexcept {
        return stableFingerprint_;
    }

    /** @copydoc PresenceDefinitionRegistry::Fingerprint */
    const Sha256Digest &PresenceDefinitionRegistry::Fingerprint() const noexcept {
        return fingerprint_;
    }

    /** @copydoc PresenceDefinitionRegistry::Definitions */
    std::span<const PresenceDefinition> PresenceDefinitionRegistry::Definitions() const noexcept {
        return definitions_;
    }

    /** @copydoc PresenceDefinitionRegistry::Find */
    Result<const PresenceDefinition *> PresenceDefinitionRegistry::Find(const PresenceStatusId id) const {
        return FindDefinition(std::span<const PresenceDefinition>{definitions_}, id);
    }

    /** @copydoc BuildStatDefinitionRegistry */
    Result<StatDefinitionRegistry> BuildStatDefinitionRegistry(const PlatformStableIdRegistry &stableIds,
                                                               const StatDefinitionRegistryCandidate &candidate,
                                                               const PlatformDefinitionRegistryLimits &limits) {
        auto snapshot = BuildCanonicalSnapshot<StatDefinition>(stableIds, candidate, limits, PlatformServiceIdKind::Stat,
                                                               [&](const StatDefinition &definition, const std::size_t index) {
            return ValidateDefinition(stableIds, limits, definition, index);
        });
        if (snapshot.HasError())
            return Result<StatDefinitionRegistry>::Failure(snapshot.ErrorValue());
        StatDefinitionRegistry registry;
        registry.projectId_ = std::move(snapshot.Value().projectId);
        registry.stableFingerprint_ = snapshot.Value().stableFingerprint;
        registry.fingerprint_ = snapshot.Value().fingerprint;
        registry.definitions_ = std::move(snapshot.Value().definitions);
        return Result<StatDefinitionRegistry>::Success(std::move(registry));
    }

    /** @copydoc BuildLeaderboardDefinitionRegistry */
    Result<LeaderboardDefinitionRegistry> BuildLeaderboardDefinitionRegistry(const PlatformStableIdRegistry &stableIds,
                                                                             const StatDefinitionRegistry &stats,
                                                                             const LeaderboardDefinitionRegistryCandidate &candidate,
                                                                             const PlatformDefinitionRegistryLimits &limits) {
        if (stats.StableIdProjectId() != stableIds.ProjectId() || stats.StableIdRegistryFingerprint() != stableIds.Fingerprint())
            return Result<LeaderboardDefinitionRegistry>::Failure(DocumentError(PlatformDefinitionErrors::InvalidCrossReference,
                                                                                "statRegistry",
                                                                                "Stat registry does not match the identity snapshot."));
        auto snapshot =
            BuildCanonicalSnapshot<LeaderboardDefinition>(stableIds, candidate, limits, PlatformServiceIdKind::Leaderboard,
                                                          [&](const LeaderboardDefinition &definition, const std::size_t index) {
            return ValidateDefinition(stableIds, stats, limits, definition, index);
        });
        if (snapshot.HasError())
            return Result<LeaderboardDefinitionRegistry>::Failure(snapshot.ErrorValue());
        LeaderboardDefinitionRegistry registry;
        registry.projectId_ = std::move(snapshot.Value().projectId);
        registry.stableFingerprint_ = snapshot.Value().stableFingerprint;
        registry.fingerprint_ = snapshot.Value().fingerprint;
        registry.definitions_ = std::move(snapshot.Value().definitions);
        return Result<LeaderboardDefinitionRegistry>::Success(std::move(registry));
    }

    /** @copydoc BuildPresenceDefinitionRegistry */
    Result<PresenceDefinitionRegistry> BuildPresenceDefinitionRegistry(const PlatformStableIdRegistry &stableIds,
                                                                       const PresenceDefinitionRegistryCandidate &candidate,
                                                                       const PlatformDefinitionRegistryLimits &limits) {
        auto snapshot = BuildCanonicalSnapshot<PresenceDefinition>(stableIds, candidate, limits, PlatformServiceIdKind::PresenceStatus,
                                                                   [&](const PresenceDefinition &definition, const std::size_t index) {
            return ValidateDefinition(stableIds, limits, definition, index);
        });
        if (snapshot.HasError())
            return Result<PresenceDefinitionRegistry>::Failure(snapshot.ErrorValue());
        PresenceDefinitionRegistry registry;
        registry.projectId_ = std::move(snapshot.Value().projectId);
        registry.stableFingerprint_ = snapshot.Value().stableFingerprint;
        registry.fingerprint_ = snapshot.Value().fingerprint;
        registry.definitions_ = std::move(snapshot.Value().definitions);
        return Result<PresenceDefinitionRegistry>::Success(std::move(registry));
    }

    /** @copydoc BuildStatDefinitionRegistryReplacement */
    Result<StatDefinitionRegistry> BuildStatDefinitionRegistryReplacement(const StatDefinitionRegistry &previous,
                                                                          const PlatformStableIdRegistry &stableIds,
                                                                          const StatDefinitionRegistryCandidate &candidate,
                                                                          const PlatformDefinitionRegistryLimits &limits) {
        if (previous.StableIdProjectId() != stableIds.ProjectId())
            return Result<StatDefinitionRegistry>::Failure(MakeError(PlatformDefinitionErrors::StaleIdentityRegistry));
        auto replacement = BuildStatDefinitionRegistry(stableIds, candidate, limits);
        if (replacement.HasError())
            return replacement;
        const auto compatible = ValidateReplacement(previous.Definitions(), replacement.Value(), stableIds, PlatformServiceIdKind::Stat,
                                                    [](const StatDefinition &prior, const StatDefinition &current) {
            return prior.authority == current.authority && prior.valueKind == current.valueKind && prior.range == current.range &&
                   prior.mutation == current.mutation;
        });
        if (compatible.HasError())
            return Result<StatDefinitionRegistry>::Failure(compatible.ErrorValue());
        return replacement;
    }

    /** @copydoc BuildLeaderboardDefinitionRegistryReplacement */
    Result<LeaderboardDefinitionRegistry> BuildLeaderboardDefinitionRegistryReplacement(
        const LeaderboardDefinitionRegistry &previous, const PlatformStableIdRegistry &stableIds, const StatDefinitionRegistry &stats,
        const LeaderboardDefinitionRegistryCandidate &candidate, const PlatformDefinitionRegistryLimits &limits) {
        if (previous.StableIdProjectId() != stableIds.ProjectId())
            return Result<LeaderboardDefinitionRegistry>::Failure(MakeError(PlatformDefinitionErrors::StaleIdentityRegistry));
        auto replacement = BuildLeaderboardDefinitionRegistry(stableIds, stats, candidate, limits);
        if (replacement.HasError())
            return replacement;
        const auto compatible =
            ValidateReplacement(previous.Definitions(), replacement.Value(), stableIds, PlatformServiceIdKind::Leaderboard,
                                [](const LeaderboardDefinition &prior, const LeaderboardDefinition &current) {
            return prior.authority == current.authority && prior.valueKind == current.valueKind && prior.range == current.range &&
                   prior.ordering == current.ordering && prior.sourceStat == current.sourceStat;
        });
        if (compatible.HasError())
            return Result<LeaderboardDefinitionRegistry>::Failure(compatible.ErrorValue());
        return replacement;
    }

    /** @copydoc BuildPresenceDefinitionRegistryReplacement */
    Result<PresenceDefinitionRegistry> BuildPresenceDefinitionRegistryReplacement(const PresenceDefinitionRegistry &previous,
                                                                                  const PlatformStableIdRegistry &stableIds,
                                                                                  const PresenceDefinitionRegistryCandidate &candidate,
                                                                                  const PlatformDefinitionRegistryLimits &limits) {
        if (previous.StableIdProjectId() != stableIds.ProjectId())
            return Result<PresenceDefinitionRegistry>::Failure(MakeError(PlatformDefinitionErrors::StaleIdentityRegistry));
        auto replacement = BuildPresenceDefinitionRegistry(stableIds, candidate, limits);
        if (replacement.HasError())
            return replacement;
        const auto compatible =
            ValidateReplacement(previous.Definitions(), replacement.Value(), stableIds, PlatformServiceIdKind::PresenceStatus,
                                [](const PresenceDefinition &prior, const PresenceDefinition &current) {
            return prior.detailPolicy == current.detailPolicy && prior.maximumDetailUtf8Bytes == current.maximumDetailUtf8Bytes;
        });
        if (compatible.HasError())
            return Result<PresenceDefinitionRegistry>::Failure(compatible.ErrorValue());
        return replacement;
    }
}  // namespace Horo::PlatformServices
