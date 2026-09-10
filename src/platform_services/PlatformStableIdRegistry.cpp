#include "Horo/PlatformServices/PlatformStableIdRegistry.h"

#include <algorithm>

namespace Horo::PlatformServices {
    namespace {
        constexpr std::string_view AlgorithmDomain = "horo.platform-services.stable-id.v1";
        constexpr std::string_view FingerprintDomain = "horo.platform-services.registry-fingerprint.v1";
        constexpr std::size_t MaxEntries = 4096;
        constexpr std::size_t MaxAliasesPerEntry = 16;
        constexpr std::size_t MaxProjectIdBytes = 128;
        constexpr std::size_t MaxRemovalProvenanceBytes = 256;
        constexpr std::size_t MaxProviderMappings = 16384;

        [[nodiscard]] constexpr bool IsKnown(const PlatformServiceIdKind kind) noexcept {
            return kind >= PlatformServiceIdKind::Achievement && kind <= PlatformServiceIdKind::PresenceStatus;
        }

        [[nodiscard]] constexpr bool IsKnown(const PlatformStableIdState state) noexcept {
            return state == PlatformStableIdState::Active || state == PlatformStableIdState::Tombstoned;
        }

        [[nodiscard]] constexpr bool IsCanonicalKeyCharacter(const char character) noexcept {
            return (character >= 'a' && character <= 'z') || (character >= '0' && character <= '9') || character == '_' ||
                   character == '.' || character == '-';
        }

        [[nodiscard]] bool IsCanonicalKey(const std::string_view key) noexcept {
            if (key.empty() || key.size() > 96 || key.front() < 'a' || key.front() > 'z')
                return false;
            return std::all_of(key.begin() + 1, key.end(), IsCanonicalKeyCharacter);
        }

        [[nodiscard]] bool IsZero(const Sha256Digest &digest) noexcept {
            return std::all_of(digest.bytes.begin(), digest.bytes.end(), [](const std::uint8_t byte) {
                return byte == 0;
            });
        }

        void AppendUint64(std::vector<std::byte> &bytes, const std::uint64_t value) {
            for (int shift = 56; shift >= 0; shift -= 8)
                bytes.push_back(static_cast<std::byte>((value >> shift) & 0xffU));
        }

        void AppendUint32(std::vector<std::byte> &bytes, const std::uint32_t value) {
            for (int shift = 24; shift >= 0; shift -= 8)
                bytes.push_back(static_cast<std::byte>((value >> shift) & 0xffU));
        }

        void AppendString(std::vector<std::byte> &bytes, const std::string_view value) {
            bytes.push_back(static_cast<std::byte>((value.size() >> 8U) & 0xffU));
            bytes.push_back(static_cast<std::byte>(value.size() & 0xffU));
            for (const char character : value)
                bytes.push_back(static_cast<std::byte>(static_cast<unsigned char>(character)));
        }

        [[nodiscard]] Result<PlatformServiceStableIdValue> InvalidId(const ErrorCodeDescriptor &descriptor) {
            return Result<PlatformServiceStableIdValue>::Failure(MakeError(descriptor));
        }

        [[nodiscard]] int HexValue(const char digit) noexcept {
            if (digit >= '0' && digit <= '9')
                return digit - '0';
            if (digit >= 'a' && digit <= 'f')
                return digit - 'a' + 10;
            return -1;
        }

        [[nodiscard]] std::size_t KindIndex(const PlatformServiceIdKind kind) noexcept {
            return static_cast<std::size_t>(kind) - 1U;
        }

        struct KeyOwner final {
            PlatformServiceIdKind kind;
            std::string_view key;
            PlatformStableIdState state;
        };

        [[nodiscard]] bool HasValidEntryShape(const PlatformStableIdDeclaration &entry) noexcept {
            return IsKnown(entry.kind) && IsKnown(entry.state) && IsCanonicalKey(entry.canonicalKey) &&
                   entry.aliases.size() <= MaxAliasesPerEntry && entry.removalProvenance.size() <= MaxRemovalProvenanceBytes;
        }

        [[nodiscard]] Result<void> CanonicalizeEntry(PlatformStableIdDeclaration &entry) {
            if (!HasValidEntryShape(entry))
                return Result<void>::Failure(MakeError(StableIdErrors::InvalidLedger));
            const bool activeHasRemoval = entry.state == PlatformStableIdState::Active && !entry.removalProvenance.empty();
            const bool tombstoneLacksRemoval = entry.state == PlatformStableIdState::Tombstoned && entry.removalProvenance.empty();
            if (activeHasRemoval || tombstoneLacksRemoval)
                return Result<void>::Failure(MakeError(StableIdErrors::InvalidLedger));
            std::sort(entry.aliases.begin(), entry.aliases.end());
            if (std::any_of(entry.aliases.begin(), entry.aliases.end(), [](const auto &alias) {
                return !IsCanonicalKey(alias);
            }))
                return Result<void>::Failure(MakeError(StableIdErrors::InvalidKey));
            return Result<void>::Success();
        }

        [[nodiscard]] bool HasValidCandidateMetadata(const PlatformStableIdRegistryCandidate &candidate) noexcept {
            return candidate.schemaVersion == PlatformStableIdRegistrySchemaVersion && !candidate.projectId.empty() &&
                   candidate.projectId.size() <= MaxProjectIdBytes && candidate.salt.IsValid() && candidate.entries.size() <= MaxEntries;
        }

        [[nodiscard]] Result<void> ValidateNumericIds(const std::vector<PlatformStableIdDeclaration> &entries,
                                                      const PlatformServicesIdSalt &salt) {
            std::vector<std::uint64_t> numericIds;
            numericIds.reserve(entries.size());
            for (const auto &entry : entries)
                numericIds.push_back(entry.storedId.value);
            std::sort(numericIds.begin(), numericIds.end());
            if (std::adjacent_find(numericIds.begin(), numericIds.end()) != numericIds.end())
                return Result<void>::Failure(MakeError(StableIdErrors::HashCollision));
            for (const auto &entry : entries) {
                const auto derived = DerivePlatformServiceStableId(salt, entry.kind, entry.canonicalKey);
                if (derived.HasError() || derived.Value() != entry.storedId)
                    return Result<void>::Failure(MakeError(StableIdErrors::StoredIdMismatch));
            }
            return Result<void>::Success();
        }

        [[nodiscard]] Result<void> ValidateKeys(const std::vector<PlatformStableIdDeclaration> &entries) {
            std::vector<KeyOwner> keys;
            keys.reserve(entries.size() * 2U);
            for (const auto &entry : entries) {
                keys.push_back({entry.kind, entry.canonicalKey, entry.state});
                for (const auto &alias : entry.aliases)
                    keys.push_back({entry.kind, alias, entry.state});
            }
            std::sort(keys.begin(), keys.end(), [](const KeyOwner &left, const KeyOwner &right) {
                if (left.kind != right.kind)
                    return left.kind < right.kind;
                return left.key < right.key;
            });
            for (std::size_t index = 1; index < keys.size(); ++index) {
                const auto &previous = keys[index - 1U];
                const auto &current = keys[index];
                if (previous.kind != current.kind || previous.key != current.key)
                    continue;
                const bool tombstoned =
                    previous.state == PlatformStableIdState::Tombstoned || current.state == PlatformStableIdState::Tombstoned;
                return Result<void>::Failure(MakeError(tombstoned ? StableIdErrors::Tombstoned : StableIdErrors::DuplicateKey));
            }
            return Result<void>::Success();
        }

        [[nodiscard]] Sha256Digest ComputeRegistryFingerprint(const std::uint32_t schemaVersion, const std::string_view projectId,
                                                              const std::vector<PlatformStableIdDeclaration> &entries) {
            std::vector<std::byte> canonical;
            canonical.reserve(64U + entries.size() * 32U);
            for (const char character : FingerprintDomain)
                canonical.push_back(static_cast<std::byte>(static_cast<unsigned char>(character)));
            canonical.push_back(std::byte{});
            AppendUint32(canonical, schemaVersion);
            for (const char character : AlgorithmDomain)
                canonical.push_back(static_cast<std::byte>(static_cast<unsigned char>(character)));
            canonical.push_back(std::byte{});
            AppendString(canonical, projectId);
            for (const auto &entry : entries) {
                canonical.push_back(static_cast<std::byte>(entry.kind));
                AppendUint64(canonical, entry.storedId.value);
                canonical.push_back(static_cast<std::byte>(entry.state));
                AppendString(canonical, entry.canonicalKey);
                canonical.push_back(static_cast<std::byte>(entry.aliases.size()));
                for (const auto &alias : entry.aliases)
                    AppendString(canonical, alias);
                AppendString(canonical, entry.removalProvenance);
            }
            return ComputeSha256(std::span<const std::byte>{canonical});
        }

        [[nodiscard]] bool IsValidProviderMapping(const PlatformStableIdRegistry &registry, const PlatformProviderId provider,
                                                  const std::uint64_t expectedRevision,
                                                  const PlatformProviderMappingEvidence &mapping) noexcept {
            return mapping.provider == provider && mapping.registryFingerprint == registry.Fingerprint() && mapping.mappingRevision != 0 &&
                   mapping.mappingRevision == expectedRevision && !IsZero(mapping.providerValueDigest) &&
                   registry.ContainsActive(mapping.kind, mapping.id);
        }

        [[nodiscard]] bool HasDuplicates(std::vector<PlatformServiceStableIdValue> &mappedIds, std::vector<Sha256Digest> &providerValues) {
            std::sort(mappedIds.begin(), mappedIds.end());
            std::sort(providerValues.begin(), providerValues.end());
            return std::adjacent_find(mappedIds.begin(), mappedIds.end()) != mappedIds.end() ||
                   std::adjacent_find(providerValues.begin(), providerValues.end()) != providerValues.end();
        }

        [[nodiscard]] bool HasAllRequiredMappings(const PlatformStableIdRegistry &registry, const PlatformProviderMappingPolicy &policy,
                                                  const std::vector<PlatformServiceStableIdValue> &mappedIds) {
            return std::all_of(registry.Entries().begin(), registry.Entries().end(), [&](const auto &entry) {
                return entry.state != PlatformStableIdState::Active || !policy.requiredKinds[KindIndex(entry.kind)] ||
                       std::binary_search(mappedIds.begin(), mappedIds.end(), entry.storedId);
            });
        }
    }  // namespace

    namespace StableIdErrors {
        namespace {
            const ErrorDomainId Domain{"horo.platform.identity"};
        }

        const ErrorCodeDescriptor InvalidSalt{Domain,
                                              ErrorCode{"platform.identity.invalid_salt"},
                                              ErrorSeverity::Error,
                                              "Platform Services ID salt is invalid.",
                                              "Provide one canonical nonzero 128-bit project salt.",
                                              false,
                                              false};
        const ErrorCodeDescriptor InvalidKey{Domain,
                                             ErrorCode{"platform.identity.invalid_key"},
                                             ErrorSeverity::Error,
                                             "Platform Services identity key is invalid.",
                                             "Use 1-96 lowercase ASCII machine-identity bytes.",
                                             false,
                                             false};
        const ErrorCodeDescriptor InvalidLedger{Domain,
                                                ErrorCode{"platform.identity.invalid_ledger"},
                                                ErrorSeverity::Error,
                                                "Platform Services identity ledger is invalid.",
                                                "Correct the bounded versioned project ledger.",
                                                false,
                                                false};
        const ErrorCodeDescriptor StoredIdMismatch{Domain,
                                                   ErrorCode{"platform.identity.stored_id_mismatch"},
                                                   ErrorSeverity::Error,
                                                   "Stored Platform Services ID does not match deterministic derivation.",
                                                   "Restore the reviewed project ledger value.",
                                                   false,
                                                   false};
        const ErrorCodeDescriptor HashCollision{Domain,
                                                ErrorCode{"platform.identity.hash_collision"},
                                                ErrorSeverity::Critical,
                                                "Two Platform Services definitions share one numeric identity.",
                                                "Choose a new unpublished canonical key or perform an explicit migration.",
                                                false,
                                                false};
        const ErrorCodeDescriptor DuplicateKey{Domain,
                                               ErrorCode{"platform.identity.duplicate_key"},
                                               ErrorSeverity::Error,
                                               "A Platform Services primary key or alias is duplicated.",
                                               "Keep every key unique within its identity kind.",
                                               false,
                                               false};
        const ErrorCodeDescriptor Tombstoned{Domain,
                                             ErrorCode{"platform.identity.tombstoned"},
                                             ErrorSeverity::Error,
                                             "The requested Platform Services identity is permanently tombstoned.",
                                             "Use an explicit compatible restore or choose a new semantic key.",
                                             false,
                                             false};
        const ErrorCodeDescriptor UnknownIdentity{Domain,
                                                  ErrorCode{"platform.identity.unknown"},
                                                  ErrorSeverity::Error,
                                                  "The Platform Services identity is not registered.",
                                                  "Reference an active registry definition.",
                                                  false,
                                                  false};
        const ErrorCodeDescriptor InvalidProviderMapping{Domain,
                                                         ErrorCode{"platform.identity.invalid_provider_mapping"},
                                                         ErrorSeverity::Error,
                                                         "Provider mapping evidence is incomplete, stale, or ambiguous.",
                                                         "Regenerate mappings from the captured registry generation.",
                                                         false,
                                                         false};
    }  // namespace StableIdErrors

    /** @copydoc PlatformServicesIdSalt::IsValid */
    bool PlatformServicesIdSalt::IsValid() const noexcept {
        return std::any_of(bytes.begin(), bytes.end(), [](const std::byte byte) {
            return byte != std::byte{};
        });
    }

    /** @copydoc DerivePlatformServiceStableId */
    Result<PlatformServiceStableIdValue> DerivePlatformServiceStableId(const PlatformServicesIdSalt &salt, const PlatformServiceIdKind kind,
                                                                       const std::string_view canonicalKey) {
        if (!salt.IsValid())
            return InvalidId(StableIdErrors::InvalidSalt);
        if (!IsKnown(kind) || !IsCanonicalKey(canonicalKey))
            return InvalidId(StableIdErrors::InvalidKey);
        std::vector<std::byte> preimage;
        preimage.reserve(AlgorithmDomain.size() + 20U + canonicalKey.size());
        for (const char character : AlgorithmDomain)
            preimage.push_back(static_cast<std::byte>(static_cast<unsigned char>(character)));
        preimage.push_back(std::byte{});
        preimage.insert(preimage.end(), salt.bytes.begin(), salt.bytes.end());
        preimage.push_back(static_cast<std::byte>(kind));
        AppendString(preimage, canonicalKey);
        const auto digest = ComputeSha256(std::span<const std::byte>{preimage});
        std::uint64_t value{};
        for (std::size_t index = 0; index < 8; ++index)
            value = (value << 8U) | digest.bytes[index];
        if (value == 0)
            return InvalidId(StableIdErrors::HashCollision);
        return Result<PlatformServiceStableIdValue>::Success({value});
    }

    /** @copydoc BuildPlatformStableIdRegistry */
    Result<PlatformStableIdRegistry> BuildPlatformStableIdRegistry(const PlatformStableIdRegistryCandidate &candidate) {
        if (!HasValidCandidateMetadata(candidate))
            return Result<PlatformStableIdRegistry>::Failure(MakeError(StableIdErrors::InvalidLedger));

        PlatformStableIdRegistry registry;
        registry.projectId_ = candidate.projectId;
        registry.entries_ = candidate.entries;
        std::sort(registry.entries_.begin(), registry.entries_.end(), [](const auto &left, const auto &right) {
            if (left.kind != right.kind)
                return left.kind < right.kind;
            return left.storedId < right.storedId;
        });

        for (auto &entry : registry.entries_) {
            const auto canonicalized = CanonicalizeEntry(entry);
            if (canonicalized.HasError())
                return Result<PlatformStableIdRegistry>::Failure(canonicalized.ErrorValue());
        }
        const auto numericIds = ValidateNumericIds(registry.entries_, candidate.salt);
        if (numericIds.HasError())
            return Result<PlatformStableIdRegistry>::Failure(numericIds.ErrorValue());
        const auto keys = ValidateKeys(registry.entries_);
        if (keys.HasError())
            return Result<PlatformStableIdRegistry>::Failure(keys.ErrorValue());
        registry.fingerprint_ = ComputeRegistryFingerprint(candidate.schemaVersion, registry.projectId_, registry.entries_);
        return Result<PlatformStableIdRegistry>::Success(std::move(registry));
    }

    /** @copydoc PlatformStableIdRegistry::ProjectId */
    std::string_view PlatformStableIdRegistry::ProjectId() const noexcept {
        return projectId_;
    }

    /** @copydoc PlatformStableIdRegistry::Fingerprint */
    const Sha256Digest &PlatformStableIdRegistry::Fingerprint() const noexcept {
        return fingerprint_;
    }

    /** @copydoc PlatformStableIdRegistry::Entries */
    std::span<const PlatformStableIdDeclaration> PlatformStableIdRegistry::Entries() const noexcept {
        return entries_;
    }

    /** @copydoc PlatformStableIdRegistry::Resolve */
    Result<PlatformServiceStableIdValue> PlatformStableIdRegistry::Resolve(const PlatformServiceIdKind kind,
                                                                           const std::string_view key) const {
        if (!IsKnown(kind) || !IsCanonicalKey(key))
            return InvalidId(StableIdErrors::InvalidKey);
        for (const auto &entry : entries_) {
            if (entry.kind != kind ||
                (entry.canonicalKey != key && std::find(entry.aliases.begin(), entry.aliases.end(), key) == entry.aliases.end()))
                continue;
            if (entry.state == PlatformStableIdState::Tombstoned)
                return InvalidId(StableIdErrors::Tombstoned);
            return Result<PlatformServiceStableIdValue>::Success(entry.storedId);
        }
        return InvalidId(StableIdErrors::UnknownIdentity);
    }

    namespace {
        template <typename StableId>
        [[nodiscard]] Result<StableId> ResolveTyped(const PlatformStableIdRegistry &registry, const PlatformServiceIdKind kind,
                                                    const std::string_view key) {
            const auto resolved = registry.Resolve(kind, key);
            if (resolved.HasError())
                return Result<StableId>::Failure(resolved.ErrorValue());
            return Result<StableId>::Success({resolved.Value().value});
        }
    }  // namespace

    /** @copydoc PlatformStableIdRegistry::ResolveAchievement */
    Result<AchievementId> PlatformStableIdRegistry::ResolveAchievement(const std::string_view key) const {
        return ResolveTyped<AchievementId>(*this, PlatformServiceIdKind::Achievement, key);
    }

    /** @copydoc PlatformStableIdRegistry::ResolveLeaderboard */
    Result<LeaderboardId> PlatformStableIdRegistry::ResolveLeaderboard(const std::string_view key) const {
        return ResolveTyped<LeaderboardId>(*this, PlatformServiceIdKind::Leaderboard, key);
    }

    /** @copydoc PlatformStableIdRegistry::ResolveStat */
    Result<StatId> PlatformStableIdRegistry::ResolveStat(const std::string_view key) const {
        return ResolveTyped<StatId>(*this, PlatformServiceIdKind::Stat, key);
    }

    /** @copydoc PlatformStableIdRegistry::ResolvePresenceStatus */
    Result<PresenceStatusId> PlatformStableIdRegistry::ResolvePresenceStatus(const std::string_view key) const {
        return ResolveTyped<PresenceStatusId>(*this, PlatformServiceIdKind::PresenceStatus, key);
    }

    /** @copydoc PlatformStableIdRegistry::ContainsActive */
    bool PlatformStableIdRegistry::ContainsActive(const PlatformServiceIdKind kind, const PlatformServiceStableIdValue id) const noexcept {
        if (!IsKnown(kind) || !id.IsValid())
            return false;
        const auto found =
            std::lower_bound(entries_.begin(), entries_.end(), std::pair{kind, id}, [](const auto &entry, const auto &needle) {
            if (entry.kind != needle.first)
                return entry.kind < needle.first;
            return entry.storedId < needle.second;
        });
        return found != entries_.end() && found->kind == kind && found->storedId == id && found->state == PlatformStableIdState::Active;
    }

    /** @copydoc ValidatePlatformProviderMappings */
    Result<void> ValidatePlatformProviderMappings(const PlatformStableIdRegistry &registry, const PlatformProviderId provider,
                                                  const PlatformProviderMappingPolicy &policy,
                                                  const std::span<const PlatformProviderMappingEvidence> mappings) {
        if (!provider.IsValid() || mappings.size() > MaxProviderMappings)
            return Result<void>::Failure(MakeError(StableIdErrors::InvalidProviderMapping));
        std::vector<PlatformServiceStableIdValue> mappedIds;
        std::vector<Sha256Digest> providerValues;
        const std::uint64_t expectedRevision = mappings.empty() ? 0 : mappings.front().mappingRevision;
        mappedIds.reserve(mappings.size());
        providerValues.reserve(mappings.size());
        for (const auto &mapping : mappings) {
            if (!IsValidProviderMapping(registry, provider, expectedRevision, mapping))
                return Result<void>::Failure(MakeError(StableIdErrors::InvalidProviderMapping));
            mappedIds.push_back(mapping.id);
            providerValues.push_back(mapping.providerValueDigest);
        }
        if (HasDuplicates(mappedIds, providerValues))
            return Result<void>::Failure(MakeError(StableIdErrors::InvalidProviderMapping));
        if (!HasAllRequiredMappings(registry, policy, mappedIds))
            return Result<void>::Failure(MakeError(StableIdErrors::InvalidProviderMapping));
        return Result<void>::Success();
    }

    /** @copydoc FormatPlatformServicesIdSalt */
    std::string FormatPlatformServicesIdSalt(const PlatformServicesIdSalt &salt) {
        constexpr char Hex[] = "0123456789abcdef";
        std::string result = "psid1:";
        result.reserve(38);
        for (const auto byte : salt.bytes) {
            const auto value = std::to_integer<unsigned>(byte);
            result.push_back(Hex[value >> 4U]);
            result.push_back(Hex[value & 0x0fU]);
        }
        return result;
    }

    /** @copydoc ParsePlatformServicesIdSalt */
    Result<PlatformServicesIdSalt> ParsePlatformServicesIdSalt(const std::string_view text) {
        if (!text.starts_with("psid1:") || text.size() != 38)
            return Result<PlatformServicesIdSalt>::Failure(MakeError(StableIdErrors::InvalidSalt));
        PlatformServicesIdSalt salt;
        for (std::size_t index = 0; index < salt.bytes.size(); ++index) {
            const int high = HexValue(text[6U + index * 2U]);
            const int low = HexValue(text[7U + index * 2U]);
            if (high < 0 || low < 0)
                return Result<PlatformServicesIdSalt>::Failure(MakeError(StableIdErrors::InvalidSalt));
            salt.bytes[index] = static_cast<std::byte>((high << 4) | low);
        }
        if (!salt.IsValid())
            return Result<PlatformServicesIdSalt>::Failure(MakeError(StableIdErrors::InvalidSalt));
        return Result<PlatformServicesIdSalt>::Success(salt);
    }

    /** @copydoc FormatPlatformServiceStableId */
    std::string FormatPlatformServiceStableId(const PlatformServiceStableIdValue id) {
        constexpr char Hex[] = "0123456789abcdef";
        std::string result = "sid1:";
        result.reserve(21);
        for (int shift = 60; shift >= 0; shift -= 4)
            result.push_back(Hex[(id.value >> shift) & 0x0fU]);
        return result;
    }

    /** @copydoc ParsePlatformServiceStableId */
    Result<PlatformServiceStableIdValue> ParsePlatformServiceStableId(const std::string_view text) {
        if (!text.starts_with("sid1:") || text.size() != 21)
            return InvalidId(StableIdErrors::InvalidLedger);
        std::uint64_t value{};
        for (const char digit : text.substr(5)) {
            const int nibble = HexValue(digit);
            if (nibble < 0)
                return InvalidId(StableIdErrors::InvalidLedger);
            value = (value << 4U) | static_cast<unsigned>(nibble);
        }
        if (value == 0)
            return InvalidId(StableIdErrors::InvalidLedger);
        return Result<PlatformServiceStableIdValue>::Success({value});
    }
}  // namespace Horo::PlatformServices
