#include "Horo/PlatformServices/PlatformStableIdRegistry.h"

#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <concepts>
#include <string>
#include <utility>

namespace Horo::PlatformServices {
    namespace {
        [[nodiscard]] PlatformServicesIdSalt TestSalt() {
            PlatformServicesIdSalt salt;
            for (std::size_t index = 0; index < salt.bytes.size(); ++index)
                salt.bytes[index] = static_cast<std::byte>(index + 1U);
            return salt;
        }

        [[nodiscard]] PlatformStableIdDeclaration Declaration(const PlatformServicesIdSalt &salt, const PlatformServiceIdKind kind,
                                                              std::string key, std::vector<std::string> aliases = {}) {
            const auto id = DerivePlatformServiceStableId(salt, kind, key);
            REQUIRE(id.HasValue());
            return {.kind = kind, .canonicalKey = std::move(key), .storedId = id.Value(), .aliases = std::move(aliases)};
        }

        [[nodiscard]] PlatformStableIdRegistryCandidate Candidate(std::vector<PlatformStableIdDeclaration> entries) {
            return {.projectId = "project.stable-identities", .salt = TestSalt(), .entries = std::move(entries)};
        }

        [[nodiscard]] Sha256Digest OpaqueDigest(const std::uint8_t seed) {
            Sha256Digest digest;
            digest.bytes.front() = seed;
            return digest;
        }

        [[nodiscard]] PlatformStableIdRegistry BuildRegistry() {
            auto candidate =
                Candidate({Declaration(TestSalt(), PlatformServiceIdKind::Achievement, "campaign.first_win", {"campaign.first_victory"}),
                           Declaration(TestSalt(), PlatformServiceIdKind::Leaderboard, "ranked.score"),
                           Declaration(TestSalt(), PlatformServiceIdKind::Stat, "player.level"),
                           Declaration(TestSalt(), PlatformServiceIdKind::PresenceStatus, "presence.online")});
            auto built = BuildPlatformStableIdRegistry(candidate);
            REQUIRE(built.HasValue());
            return std::move(built).Value();
        }

        void CheckError(const auto &result, const ErrorCodeDescriptor &descriptor) {
            REQUIRE(result.HasError());
            CHECK(result.ErrorValue().code.Value() == descriptor.code.Value());
        }
    }  // namespace

    static_assert(!std::same_as<AchievementId, LeaderboardId>);
    static_assert(!std::same_as<LeaderboardId, StatId>);
    static_assert(!std::same_as<StatId, PresenceStatusId>);
    static_assert(!std::same_as<CloudObjectId, PlatformServiceStableIdValue>);

    TEST_CASE("Platform stable IDs follow schema-v1 golden vectors and canonical text", "[platform-services][stable-id]") {
        const auto salt = TestSalt();
        const std::array vectors{std::pair{PlatformServiceIdKind::Achievement,
                                           std::pair{"campaign.first_win", UINT64_C(0x26f5b94170b5e592)}},
                                 std::pair{PlatformServiceIdKind::Leaderboard, std::pair{"ranked.score", UINT64_C(0x28314616f492726e)}},
                                 std::pair{PlatformServiceIdKind::Stat, std::pair{"player.level", UINT64_C(0x9b2be22eb27d292e)}},
                                 std::pair{PlatformServiceIdKind::PresenceStatus,
                                           std::pair{"presence.online", UINT64_C(0x2dfba046d594f911)}}};

        for (const auto &[kind, fixture] : vectors) {
            const auto derived = DerivePlatformServiceStableId(salt, kind, fixture.first);
            REQUIRE(derived.HasValue());
            CHECK(derived.Value().value == fixture.second);
            const auto text = FormatPlatformServiceStableId(derived.Value());
            const auto reparsed = ParsePlatformServiceStableId(text);
            REQUIRE(reparsed.HasValue());
            CHECK(reparsed.Value() == derived.Value());
        }

        CHECK(FormatPlatformServicesIdSalt(salt) == "psid1:0102030405060708090a0b0c0d0e0f10");
        const auto reparsedSalt = ParsePlatformServicesIdSalt(FormatPlatformServicesIdSalt(salt));
        REQUIRE(reparsedSalt.HasValue());
        CHECK(reparsedSalt.Value() == salt);
        const auto smallId = ParsePlatformServiceStableId("sid1:0000000000000001");
        REQUIRE(smallId.HasValue());
        CHECK(smallId.Value().value == 1);
    }

    TEST_CASE("Platform stable ID parsing and derivation reject noncanonical or invalid input", "[platform-services][stable-id]") {
        CheckError(ParsePlatformServicesIdSalt("psid1:00000000000000000000000000000000"), StableIdErrors::InvalidSalt);
        CheckError(ParsePlatformServicesIdSalt("psid1:0102030405060708090A0b0c0d0e0f10"), StableIdErrors::InvalidSalt);
        CheckError(ParsePlatformServicesIdSalt("0102030405060708090a0b0c0d0e0f10"), StableIdErrors::InvalidSalt);
        CheckError(ParsePlatformServiceStableId("sid1:0000000000000000"), StableIdErrors::InvalidLedger);
        CheckError(ParsePlatformServiceStableId("sid1:A621df23e371c366"), StableIdErrors::InvalidLedger);
        CheckError(ParsePlatformServiceStableId("a621df23e371c366"), StableIdErrors::InvalidLedger);
        CheckError(DerivePlatformServiceStableId({}, PlatformServiceIdKind::Achievement, "valid.key"), StableIdErrors::InvalidSalt);
        CheckError(DerivePlatformServiceStableId(TestSalt(), PlatformServiceIdKind::Achievement, "Invalid.Key"),
                   StableIdErrors::InvalidKey);
        CheckError(DerivePlatformServiceStableId(TestSalt(), static_cast<PlatformServiceIdKind>(99), "valid.key"),
                   StableIdErrors::InvalidKey);
        CheckError(DerivePlatformServiceStableId(TestSalt(), PlatformServiceIdKind::Stat, std::string(97, 'a')),
                   StableIdErrors::InvalidKey);
    }

    TEST_CASE("Registry snapshots sort deterministically and expose only typed active resolution", "[platform-services][stable-id]") {
        auto firstCandidate = Candidate({Declaration(TestSalt(), PlatformServiceIdKind::Stat, "player.level"),
                                         Declaration(TestSalt(), PlatformServiceIdKind::Achievement, "campaign.first_win",
                                                     {"campaign.won_first", "campaign.first_victory"})});
        auto secondCandidate = firstCandidate;
        std::reverse(secondCandidate.entries.begin(), secondCandidate.entries.end());
        std::reverse(secondCandidate.entries.back().aliases.begin(), secondCandidate.entries.back().aliases.end());
        const auto first = BuildPlatformStableIdRegistry(firstCandidate);
        const auto second = BuildPlatformStableIdRegistry(secondCandidate);
        REQUIRE(first.HasValue());
        REQUIRE(second.HasValue());
        CHECK(first.Value().Fingerprint() == second.Value().Fingerprint());
        CHECK(first.Value().Entries().front().kind == PlatformServiceIdKind::Achievement);
        CHECK(std::ranges::is_sorted(first.Value().Entries().front().aliases));

        const auto achievement = first.Value().ResolveAchievement("campaign.first_victory");
        REQUIRE(achievement.HasValue());
        CHECK(achievement.Value().value == UINT64_C(0x26f5b94170b5e592));
        REQUIRE(first.Value().ResolveStat("player.level").HasValue());
        CheckError(first.Value().ResolveLeaderboard("player.level"), StableIdErrors::UnknownIdentity);
        CheckError(first.Value().ResolveAchievement("Invalid.Key"), StableIdErrors::InvalidKey);
        CHECK(first.Value().ContainsActive(PlatformServiceIdKind::Achievement, {achievement.Value().value}));
        CHECK_FALSE(first.Value().ContainsActive(PlatformServiceIdKind::Achievement, {}));
    }

    TEST_CASE("Registry construction fails closed on corrupt, duplicate, colliding, or unbounded ledgers",
              "[platform-services][stable-id]") {
        auto good = Candidate({Declaration(TestSalt(), PlatformServiceIdKind::Achievement, "campaign.first_win"),
                               Declaration(TestSalt(), PlatformServiceIdKind::Stat, "player.level")});

        auto wrongVersion = good;
        ++wrongVersion.schemaVersion;
        CheckError(BuildPlatformStableIdRegistry(wrongVersion), StableIdErrors::InvalidLedger);
        auto emptyProject = good;
        emptyProject.projectId.clear();
        CheckError(BuildPlatformStableIdRegistry(emptyProject), StableIdErrors::InvalidLedger);
        auto storedMismatch = good;
        ++storedMismatch.entries.front().storedId.value;
        CheckError(BuildPlatformStableIdRegistry(storedMismatch), StableIdErrors::StoredIdMismatch);
        auto collision = good;
        collision.entries.back().storedId = collision.entries.front().storedId;
        CheckError(BuildPlatformStableIdRegistry(collision), StableIdErrors::HashCollision);
        auto duplicateKey = good;
        duplicateKey.entries.back().kind = PlatformServiceIdKind::Achievement;
        duplicateKey.entries.back().canonicalKey = "campaign.first_win";
        duplicateKey.entries.back().storedId = duplicateKey.entries.front().storedId;
        CheckError(BuildPlatformStableIdRegistry(duplicateKey), StableIdErrors::HashCollision);
        auto duplicateAlias = good;
        duplicateAlias.entries.back().aliases = {"player.level"};
        CheckError(BuildPlatformStableIdRegistry(duplicateAlias), StableIdErrors::DuplicateKey);
        auto invalidState = good;
        invalidState.entries.front().state = static_cast<PlatformStableIdState>(99);
        CheckError(BuildPlatformStableIdRegistry(invalidState), StableIdErrors::InvalidLedger);
        auto excessiveAliases = good;
        excessiveAliases.entries.front().aliases.assign(17, "valid.alias");
        CheckError(BuildPlatformStableIdRegistry(excessiveAliases), StableIdErrors::InvalidLedger);
        auto excessiveEntries = Candidate({});
        excessiveEntries.entries.resize(4097);
        CheckError(BuildPlatformStableIdRegistry(excessiveEntries), StableIdErrors::InvalidLedger);
    }

    TEST_CASE("Tombstones permanently reserve primary keys, aliases, and numeric identities", "[platform-services][stable-id]") {
        auto removed = Declaration(TestSalt(), PlatformServiceIdKind::Achievement, "campaign.retired", {"campaign.legacy_retired"});
        removed.state = PlatformStableIdState::Tombstoned;
        removed.removalProvenance = "removed-by-migration:PLS-003.2";
        auto candidate = Candidate({removed});
        const auto registry = BuildPlatformStableIdRegistry(candidate);
        REQUIRE(registry.HasValue());
        CheckError(registry.Value().ResolveAchievement("campaign.retired"), StableIdErrors::Tombstoned);
        CheckError(registry.Value().ResolveAchievement("campaign.legacy_retired"), StableIdErrors::Tombstoned);
        CHECK_FALSE(registry.Value().ContainsActive(PlatformServiceIdKind::Achievement, removed.storedId));

        candidate.entries.push_back(Declaration(TestSalt(), PlatformServiceIdKind::Achievement, "campaign.new", {"campaign.retired"}));
        CheckError(BuildPlatformStableIdRegistry(candidate), StableIdErrors::Tombstoned);
        candidate.entries.pop_back();
        candidate.entries.front().removalProvenance.clear();
        CheckError(BuildPlatformStableIdRegistry(candidate), StableIdErrors::InvalidLedger);
    }

    TEST_CASE("Provider mapping evidence is opaque, generation-bound, complete, and one-to-one", "[platform-services][stable-id]") {
        const auto registry = BuildRegistry();
        const PlatformProviderId provider{42};
        PlatformProviderMappingPolicy policy;
        policy.requiredKinds = {true, true, false, false};
        const auto achievement = registry.Resolve(PlatformServiceIdKind::Achievement, "campaign.first_win").Value();
        const auto leaderboard = registry.Resolve(PlatformServiceIdKind::Leaderboard, "ranked.score").Value();
        std::vector<PlatformProviderMappingEvidence> mappings{{.provider = provider,
                                                               .kind = PlatformServiceIdKind::Achievement,
                                                               .id = achievement,
                                                               .providerValueDigest = OpaqueDigest(1),
                                                               .registryFingerprint = registry.Fingerprint(),
                                                               .mappingRevision = 7},
                                                              {.provider = provider,
                                                               .kind = PlatformServiceIdKind::Leaderboard,
                                                               .id = leaderboard,
                                                               .providerValueDigest = OpaqueDigest(2),
                                                               .registryFingerprint = registry.Fingerprint(),
                                                               .mappingRevision = 7}};
        CHECK(ValidatePlatformProviderMappings(registry, provider, policy, mappings).HasValue());

        auto missing = mappings;
        missing.pop_back();
        CheckError(ValidatePlatformProviderMappings(registry, provider, policy, missing), StableIdErrors::InvalidProviderMapping);
        auto stale = mappings;
        stale.front().registryFingerprint.bytes.front() ^= 0xffU;
        CheckError(ValidatePlatformProviderMappings(registry, provider, policy, stale), StableIdErrors::InvalidProviderMapping);
        auto mixedRevision = mappings;
        ++mixedRevision.back().mappingRevision;
        CheckError(ValidatePlatformProviderMappings(registry, provider, policy, mixedRevision), StableIdErrors::InvalidProviderMapping);
        auto duplicateId = mappings;
        duplicateId.back().kind = PlatformServiceIdKind::Achievement;
        duplicateId.back().id = achievement;
        CheckError(ValidatePlatformProviderMappings(registry, provider, policy, duplicateId), StableIdErrors::InvalidProviderMapping);
        auto duplicateNativeEvidence = mappings;
        duplicateNativeEvidence.back().providerValueDigest = duplicateNativeEvidence.front().providerValueDigest;
        CheckError(ValidatePlatformProviderMappings(registry, provider, policy, duplicateNativeEvidence),
                   StableIdErrors::InvalidProviderMapping);
        auto wrongProvider = mappings;
        wrongProvider.front().provider = {99};
        CheckError(ValidatePlatformProviderMappings(registry, provider, policy, wrongProvider), StableIdErrors::InvalidProviderMapping);
        CheckError(ValidatePlatformProviderMappings(registry, {}, policy, mappings), StableIdErrors::InvalidProviderMapping);
        auto zeroRevision = mappings;
        zeroRevision.front().mappingRevision = 0;
        CheckError(ValidatePlatformProviderMappings(registry, provider, policy, zeroRevision), StableIdErrors::InvalidProviderMapping);
        auto zeroEvidence = mappings;
        zeroEvidence.front().providerValueDigest = {};
        CheckError(ValidatePlatformProviderMappings(registry, provider, policy, zeroEvidence), StableIdErrors::InvalidProviderMapping);

        auto removed = Declaration(TestSalt(), PlatformServiceIdKind::Achievement, "campaign.removed");
        removed.state = PlatformStableIdState::Tombstoned;
        removed.removalProvenance = "removed-by-migration:PLS-003.2";
        const auto tombstoneRegistry = BuildPlatformStableIdRegistry(Candidate({removed}));
        REQUIRE(tombstoneRegistry.HasValue());
        const std::array tombstoneMapping{PlatformProviderMappingEvidence{.provider = provider,
                                                                          .kind = PlatformServiceIdKind::Achievement,
                                                                          .id = removed.storedId,
                                                                          .providerValueDigest = OpaqueDigest(3),
                                                                          .registryFingerprint = tombstoneRegistry.Value().Fingerprint(),
                                                                          .mappingRevision = 8}};
        CheckError(ValidatePlatformProviderMappings(tombstoneRegistry.Value(), provider, {}, tombstoneMapping),
                   StableIdErrors::InvalidProviderMapping);
    }

    TEST_CASE("A failed replacement cannot mutate or invalidate the captured registry generation", "[platform-services][stable-id]") {
        const auto active = BuildRegistry();
        auto replacement = Candidate({Declaration(TestSalt(), PlatformServiceIdKind::Achievement, "campaign.first_win")});
        replacement.entries.front().storedId.value = 1;
        CheckError(BuildPlatformStableIdRegistry(replacement), StableIdErrors::StoredIdMismatch);
        const auto retained = active.ResolveAchievement("campaign.first_win");
        REQUIRE(retained.HasValue());
        CHECK(retained.Value().value == UINT64_C(0x26f5b94170b5e592));
    }
}  // namespace Horo::PlatformServices
