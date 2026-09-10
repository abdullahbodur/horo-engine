#include "Horo/PlatformServices/PlatformDefinitionRegistries.h"

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

    namespace {
        constexpr std::uint32_t HardMaximumDefinitions = 4096;
        constexpr std::uint32_t HardMaximumLocalizationKeyBytes = 512;
        constexpr std::uint32_t HardMaximumPresenceDetailBytes = 4096;

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

        void AddRange(FingerprintWriter &writer, const ProgressionNumericRange &range) {
            writer.AddU64(static_cast<std::uint64_t>(range.minimum));
            writer.AddU64(static_cast<std::uint64_t>(range.maximum));
        }

        template <typename Definition> void AddProgressionSemantics(FingerprintWriter &writer, const Definition &definition) {
            writer.AddU64(definition.id.value);
            writer.AddByte(static_cast<std::uint8_t>(definition.authority));
            writer.AddByte(static_cast<std::uint8_t>(definition.valueKind));
            AddRange(writer, definition.range);
        }

        void AddPresentation(FingerprintWriter &writer, const std::string_view localizationKey, const bool hidden) {
            writer.AddText(localizationKey);
            writer.AddByte(hidden ? 1U : 0U);
        }

        [[nodiscard]] Sha256Digest Fingerprint(const Sha256Digest &stableIds, const std::span<const StatDefinition> definitions) {
            FingerprintWriter writer{64U + definitions.size() * 48U};
            writer.AddText("horo.platform-services.stat-definitions.v1");
            writer.AddDigest(stableIds);
            writer.AddU32(static_cast<std::uint32_t>(definitions.size()));
            for (const StatDefinition &definition : definitions) {
                AddProgressionSemantics(writer, definition);
                writer.AddByte(static_cast<std::uint8_t>(definition.mutation));
                AddPresentation(writer, definition.localizationKey, definition.hidden);
            }
            return writer.Finish();
        }

        [[nodiscard]] Sha256Digest Fingerprint(const Sha256Digest &stableIds, const std::span<const LeaderboardDefinition> definitions) {
            FingerprintWriter writer{64U + definitions.size() * 56U};
            writer.AddText("horo.platform-services.leaderboard-definitions.v1");
            writer.AddDigest(stableIds);
            writer.AddU32(static_cast<std::uint32_t>(definitions.size()));
            for (const LeaderboardDefinition &definition : definitions) {
                AddProgressionSemantics(writer, definition);
                writer.AddByte(static_cast<std::uint8_t>(definition.ordering));
                writer.AddByte(definition.sourceStat.has_value() ? 1U : 0U);
                if (definition.sourceStat)
                    writer.AddU64(definition.sourceStat->value);
                AddPresentation(writer, definition.localizationKey, definition.hidden);
            }
            return writer.Finish();
        }

        [[nodiscard]] Sha256Digest Fingerprint(const Sha256Digest &stableIds, const std::span<const PresenceDefinition> definitions) {
            FingerprintWriter writer{64U + definitions.size() * 32U};
            writer.AddText("horo.platform-services.presence-definitions.v1");
            writer.AddDigest(stableIds);
            writer.AddU32(static_cast<std::uint32_t>(definitions.size()));
            for (const PresenceDefinition &definition : definitions) {
                writer.AddU64(definition.id.value);
                writer.AddByte(static_cast<std::uint8_t>(definition.detailPolicy));
                writer.AddU32(definition.maximumDetailUtf8Bytes);
                AddPresentation(writer, definition.localizationKey, definition.hidden);
            }
            return writer.Finish();
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
            if (const auto source = stats.Find(*definition.sourceStat);
                source.HasError() || source.Value()->valueKind != definition.valueKind ||
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

    /** @copydoc StatDefinitionRegistry::Find */
    Result<const StatDefinition *> StatDefinitionRegistry::Find(const StatId id) const {
        return FindDefinition(std::span<const StatDefinition>{definitions_}, id);
    }

    /** @copydoc LeaderboardDefinitionRegistry::Find */
    Result<const LeaderboardDefinition *> LeaderboardDefinitionRegistry::Find(const LeaderboardId id) const {
        return FindDefinition(std::span<const LeaderboardDefinition>{definitions_}, id);
    }

    /** @copydoc PresenceDefinitionRegistry::Find */
    Result<const PresenceDefinition *> PresenceDefinitionRegistry::Find(const PresenceStatusId id) const {
        return FindDefinition(std::span<const PresenceDefinition>{definitions_}, id);
    }

    /** @copydoc BuildStatDefinitionRegistry */
    Result<StatDefinitionRegistry> BuildStatDefinitionRegistry(const PlatformStableIdRegistry &stableIds,
                                                               const StatDefinitionRegistryCandidate &candidate,
                                                               const PlatformDefinitionRegistryLimits &limits) {
        auto snapshot =
            BuildCanonicalSnapshot<StatDefinition>(stableIds, candidate, limits, PlatformServiceIdKind::Stat,
                                                   [&stableIds, &limits](const StatDefinition &definition, const std::size_t index) {
            return ValidateDefinition(stableIds, limits, definition, index);
        });
        if (snapshot.HasError())
            return Result<StatDefinitionRegistry>::Failure(snapshot.ErrorValue());
        auto value = std::move(snapshot).Value();
        return Result<StatDefinitionRegistry>::Success(
            StatDefinitionRegistry{std::move(value.projectId), value.stableFingerprint, value.fingerprint, std::move(value.definitions)});
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
        auto snapshot = BuildCanonicalSnapshot<LeaderboardDefinition>(stableIds, candidate, limits, PlatformServiceIdKind::Leaderboard,
                                                                      [&stableIds, &stats, &limits](const LeaderboardDefinition &definition,
                                                                                                    const std::size_t index) {
            return ValidateDefinition(stableIds, stats, limits, definition, index);
        });
        if (snapshot.HasError())
            return Result<LeaderboardDefinitionRegistry>::Failure(snapshot.ErrorValue());
        auto value = std::move(snapshot).Value();
        return Result<LeaderboardDefinitionRegistry>::Success(LeaderboardDefinitionRegistry{std::move(value.projectId),
                                                                                            value.stableFingerprint, value.fingerprint,
                                                                                            std::move(value.definitions)});
    }

    /** @copydoc BuildPresenceDefinitionRegistry */
    Result<PresenceDefinitionRegistry> BuildPresenceDefinitionRegistry(const PlatformStableIdRegistry &stableIds,
                                                                       const PresenceDefinitionRegistryCandidate &candidate,
                                                                       const PlatformDefinitionRegistryLimits &limits) {
        auto snapshot = BuildCanonicalSnapshot<PresenceDefinition>(stableIds, candidate, limits, PlatformServiceIdKind::PresenceStatus,
                                                                   [&stableIds, &limits](const PresenceDefinition &definition,
                                                                                         const std::size_t index) {
            return ValidateDefinition(stableIds, limits, definition, index);
        });
        if (snapshot.HasError())
            return Result<PresenceDefinitionRegistry>::Failure(snapshot.ErrorValue());
        auto value = std::move(snapshot).Value();
        return Result<PresenceDefinitionRegistry>::Success(PresenceDefinitionRegistry{std::move(value.projectId), value.stableFingerprint,
                                                                                      value.fingerprint, std::move(value.definitions)});
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
        if (const auto compatible = ValidateReplacement(previous.Definitions(), replacement.Value(), stableIds, PlatformServiceIdKind::Stat,
                                                        [](const StatDefinition &prior, const StatDefinition &current) {
            return prior.authority == current.authority && prior.valueKind == current.valueKind && prior.range == current.range &&
                   prior.mutation == current.mutation;
        });
            compatible.HasError())
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
        if (const auto compatible =
                ValidateReplacement(previous.Definitions(), replacement.Value(), stableIds, PlatformServiceIdKind::Leaderboard,
                                    [](const LeaderboardDefinition &prior, const LeaderboardDefinition &current) {
            return prior.authority == current.authority && prior.valueKind == current.valueKind && prior.range == current.range &&
                   prior.ordering == current.ordering && prior.sourceStat == current.sourceStat;
        });
            compatible.HasError())
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
        if (const auto compatible =
                ValidateReplacement(previous.Definitions(), replacement.Value(), stableIds, PlatformServiceIdKind::PresenceStatus,
                                    [](const PresenceDefinition &prior, const PresenceDefinition &current) {
            return prior.detailPolicy == current.detailPolicy && prior.maximumDetailUtf8Bytes == current.maximumDetailUtf8Bytes;
        });
            compatible.HasError())
            return Result<PresenceDefinitionRegistry>::Failure(compatible.ErrorValue());
        return replacement;
    }
}  // namespace Horo::PlatformServices
