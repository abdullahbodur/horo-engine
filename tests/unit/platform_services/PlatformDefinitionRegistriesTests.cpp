#include "Horo/PlatformServices/PlatformDefinitionRegistries.h"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <concepts>
#include <string>
#include <utility>

namespace Horo::PlatformServices {
    namespace {
        [[nodiscard]] PlatformServicesIdSalt TestSalt() {
            PlatformServicesIdSalt salt;
            std::uint8_t nextByte = 71;
            std::ranges::generate(salt.bytes, [&nextByte] {
                return static_cast<std::byte>(nextByte++);
            });
            return salt;
        }

        [[nodiscard]] PlatformStableIdDeclaration Declaration(const PlatformServiceIdKind kind, std::string key,
                                                              const PlatformStableIdState state = PlatformStableIdState::Active) {
            const auto id = DerivePlatformServiceStableId(TestSalt(), kind, key);
            REQUIRE(id.HasValue());
            return {.kind = kind,
                    .canonicalKey = std::move(key),
                    .storedId = id.Value(),
                    .state = state,
                    .removalProvenance = state == PlatformStableIdState::Tombstoned ? "removed-by-migration:PLS-003.4" : ""};
        }

        [[nodiscard]] PlatformStableIdRegistry StableRegistry(std::vector<PlatformStableIdDeclaration> entries,
                                                              std::string projectId = "project.platform-definitions") {
            auto built =
                BuildPlatformStableIdRegistry({.projectId = std::move(projectId), .salt = TestSalt(), .entries = std::move(entries)});
            REQUIRE(built.HasValue());
            return std::move(built).Value();
        }

        [[nodiscard]] StatDefinition Stat(const PlatformStableIdDeclaration &entry) {
            return {.id = StatId{entry.storedId.value},
                    .authority = ProgressionAuthorityMode::LocalProduct,
                    .valueKind = ProgressionValueKind::SignedInteger64,
                    .range = {.minimum = -1000, .maximum = 1000},
                    .mutation = StatMutationPolicy::SnapshotAtRevision,
                    .localizationKey = "stats.player_score"};
        }

        [[nodiscard]] LeaderboardDefinition Leaderboard(const PlatformStableIdDeclaration &entry,
                                                        const std::optional<StatId> source = std::nullopt) {
            return {.id = LeaderboardId{entry.storedId.value},
                    .authority = ProgressionAuthorityMode::AuthorityServer,
                    .valueKind = ProgressionValueKind::SignedInteger64,
                    .range = {.minimum = 0, .maximum = 1000},
                    .ordering = LeaderboardOrdering::HighestFirst,
                    .sourceStat = source,
                    .localizationKey = "leaderboards.ranked_score"};
        }

        [[nodiscard]] PresenceDefinition Presence(const PlatformStableIdDeclaration &entry) {
            return {.id = PresenceStatusId{entry.storedId.value},
                    .detailPolicy = PresenceDetailPolicy::Optional,
                    .maximumDetailUtf8Bytes = 128,
                    .localizationKey = "presence.in_match"};
        }

        template <typename Definition>
        [[nodiscard]] auto Candidate(const PlatformStableIdRegistry &stableIds, std::vector<Definition> definitions) {
            if constexpr (std::same_as<Definition, StatDefinition>)
                return StatDefinitionRegistryCandidate{.stableIdRegistryFingerprint = stableIds.Fingerprint(),
                                                       .definitions = std::move(definitions)};
            else if constexpr (std::same_as<Definition, LeaderboardDefinition>)
                return LeaderboardDefinitionRegistryCandidate{.stableIdRegistryFingerprint = stableIds.Fingerprint(),
                                                              .definitions = std::move(definitions)};
            else
                return PresenceDefinitionRegistryCandidate{.stableIdRegistryFingerprint = stableIds.Fingerprint(),
                                                           .definitions = std::move(definitions)};
        }

        void CheckError(const auto &result, const ErrorCodeDescriptor &descriptor) {
            REQUIRE(result.HasError());
            CHECK(result.ErrorValue().code.Value() == descriptor.code.Value());
        }

        void CheckFieldError(const auto &result, const ErrorCodeDescriptor &descriptor, const std::string_view field) {
            CheckError(result, descriptor);
            REQUIRE(result.ErrorValue().diagnostics.size() == 1);
            CHECK(result.ErrorValue().diagnostics.front().location.source == field);
        }
    }  // namespace

    static_assert(!std::same_as<LeaderboardId, StatId>);
    static_assert(!std::same_as<StatId, PresenceStatusId>);
    static_assert(std::is_copy_constructible_v<LeaderboardDefinition>);

    TEST_CASE("Platform definition registries are typed deterministic immutable snapshots", "[platform-services][platform-definitions]") {
        const auto statEntry = Declaration(PlatformServiceIdKind::Stat, "stats.player_score");
        const auto boardEntry = Declaration(PlatformServiceIdKind::Leaderboard, "leaderboards.ranked_score");
        const auto presenceEntry = Declaration(PlatformServiceIdKind::PresenceStatus, "presence.in_match");
        const auto stableIds = StableRegistry({presenceEntry, boardEntry, statEntry});

        const auto stats = BuildStatDefinitionRegistry(stableIds, Candidate(stableIds, std::vector{Stat(statEntry)}));
        REQUIRE(stats.HasValue());
        const auto boards =
            BuildLeaderboardDefinitionRegistry(stableIds, stats.Value(),
                                               Candidate(stableIds,
                                                         std::vector{Leaderboard(boardEntry, StatId{statEntry.storedId.value})}));
        const auto presence = BuildPresenceDefinitionRegistry(stableIds, Candidate(stableIds, std::vector{Presence(presenceEntry)}));
        REQUIRE(boards.HasValue());
        REQUIRE(presence.HasValue());
        CHECK(stats.Value().StableIdProjectId() == "project.platform-definitions");
        CHECK(stats.Value().StableIdRegistryFingerprint() == stableIds.Fingerprint());
        CHECK(boards.Value().Find(LeaderboardId{boardEntry.storedId.value}).Value()->sourceStat == StatId{statEntry.storedId.value});
        CHECK(presence.Value().Find(PresenceStatusId{presenceEntry.storedId.value}).Value()->maximumDetailUtf8Bytes == 128);

        const auto statsAgain = BuildStatDefinitionRegistry(stableIds, Candidate(stableIds, std::vector{Stat(statEntry)}));
        REQUIRE(statsAgain.HasValue());
        CHECK(stats.Value().Fingerprint() == statsAgain.Value().Fingerprint());
        CheckError(stats.Value().Find({}), PlatformDefinitionErrors::UnknownIdentity);
    }

    TEST_CASE("Stat definitions reject malformed numeric mutation and presentation fields", "[platform-services][platform-definitions]") {
        const auto entry = Declaration(PlatformServiceIdKind::Stat, "stats.player_score");
        const auto stableIds = StableRegistry({entry});
        auto definition = Stat(entry);

        definition.valueKind = ProgressionValueKind::UnsignedInteger64;
        CheckFieldError(BuildStatDefinitionRegistry(stableIds, Candidate(stableIds, std::vector{definition})),
                        PlatformDefinitionErrors::InvalidDefinition, "definitions[0].range");
        definition = Stat(entry);
        definition.range = {.minimum = 5, .maximum = 4};
        CheckFieldError(BuildStatDefinitionRegistry(stableIds, Candidate(stableIds, std::vector{definition})),
                        PlatformDefinitionErrors::InvalidDefinition, "definitions[0].range");
        definition = Stat(entry);
        definition.mutation = static_cast<StatMutationPolicy>(99);
        CheckFieldError(BuildStatDefinitionRegistry(stableIds, Candidate(stableIds, std::vector{definition})),
                        PlatformDefinitionErrors::InvalidDefinition, "definitions[0].mutation");
        definition = Stat(entry);
        definition.localizationKey = "Invalid Key";
        CheckFieldError(BuildStatDefinitionRegistry(stableIds, Candidate(stableIds, std::vector{definition})),
                        PlatformDefinitionErrors::InvalidDefinition, "definitions[0].localizationKey");
    }

    TEST_CASE("Leaderboard cross references require compatible stat kind and covering range", "[platform-services][platform-definitions]") {
        const auto statEntry = Declaration(PlatformServiceIdKind::Stat, "stats.player_score");
        const auto boardEntry = Declaration(PlatformServiceIdKind::Leaderboard, "leaderboards.ranked_score");
        const auto stableIds = StableRegistry({statEntry, boardEntry});
        const auto stats = BuildStatDefinitionRegistry(stableIds, Candidate(stableIds, std::vector{Stat(statEntry)}));
        REQUIRE(stats.HasValue());

        auto definition = Leaderboard(boardEntry, StatId{statEntry.storedId.value});
        definition.valueKind = ProgressionValueKind::UnsignedInteger64;
        CheckFieldError(BuildLeaderboardDefinitionRegistry(stableIds, stats.Value(), Candidate(stableIds, std::vector{definition})),
                        PlatformDefinitionErrors::InvalidCrossReference, "definitions[0].sourceStat");
        definition = Leaderboard(boardEntry, StatId{statEntry.storedId.value});
        definition.range.maximum = 1001;
        CheckFieldError(BuildLeaderboardDefinitionRegistry(stableIds, stats.Value(), Candidate(stableIds, std::vector{definition})),
                        PlatformDefinitionErrors::InvalidCrossReference, "definitions[0].sourceStat");
        definition = Leaderboard(boardEntry, StatId{999});
        CheckFieldError(BuildLeaderboardDefinitionRegistry(stableIds, stats.Value(), Candidate(stableIds, std::vector{definition})),
                        PlatformDefinitionErrors::InvalidCrossReference, "definitions[0].sourceStat");
    }

    TEST_CASE("Presence definitions enforce detail policy and finite byte bounds", "[platform-services][platform-definitions]") {
        const auto entry = Declaration(PlatformServiceIdKind::PresenceStatus, "presence.in_match");
        const auto stableIds = StableRegistry({entry});
        auto definition = Presence(entry);

        definition.detailPolicy = PresenceDetailPolicy::Forbidden;
        CheckFieldError(BuildPresenceDefinitionRegistry(stableIds, Candidate(stableIds, std::vector{definition})),
                        PlatformDefinitionErrors::InvalidDefinition, "definitions[0].maximumDetailUtf8Bytes");
        definition = Presence(entry);
        definition.maximumDetailUtf8Bytes = 0;
        CheckFieldError(BuildPresenceDefinitionRegistry(stableIds, Candidate(stableIds, std::vector{definition})),
                        PlatformDefinitionErrors::InvalidDefinition, "definitions[0].maximumDetailUtf8Bytes");
        definition.maximumDetailUtf8Bytes = 129;
        CheckFieldError(BuildPresenceDefinitionRegistry(stableIds, Candidate(stableIds, std::vector{definition}),
                                                        {.maximumPresenceDetailBytes = 128}),
                        PlatformDefinitionErrors::InvalidDefinition, "definitions[0].maximumDetailUtf8Bytes");
    }

    TEST_CASE("Definition registries reject duplicate incomplete wrong-kind and tombstoned identities",
              "[platform-services][platform-definitions]") {
        const auto first = Declaration(PlatformServiceIdKind::Stat, "stats.first");
        const auto second = Declaration(PlatformServiceIdKind::Stat, "stats.second");
        const auto stableIds = StableRegistry({first, second});
        const auto definition = Stat(first);
        CheckFieldError(BuildStatDefinitionRegistry(stableIds, Candidate(stableIds, std::vector{definition, definition})),
                        PlatformDefinitionErrors::DuplicateDefinition, "definitions[1].id");
        CheckError(BuildStatDefinitionRegistry(stableIds, Candidate(stableIds, std::vector{definition})),
                   PlatformDefinitionErrors::IncompleteRegistry);

        const auto wrongKind = Declaration(PlatformServiceIdKind::Leaderboard, "boards.wrong");
        auto wrongDefinition = definition;
        wrongDefinition.id = StatId{wrongKind.storedId.value};
        CheckFieldError(BuildStatDefinitionRegistry(stableIds, Candidate(stableIds, std::vector{wrongDefinition, Stat(second)})),
                        PlatformDefinitionErrors::UnknownIdentity, "definitions[0].id");

        const auto retired = Declaration(PlatformServiceIdKind::Stat, "stats.retired", PlatformStableIdState::Tombstoned);
        const auto retiredIds = StableRegistry({retired});
        CHECK(BuildStatDefinitionRegistry(retiredIds, Candidate(retiredIds, std::vector<StatDefinition>{})).HasValue());
        CheckFieldError(BuildStatDefinitionRegistry(retiredIds, Candidate(retiredIds, std::vector{Stat(retired)})),
                        PlatformDefinitionErrors::UnknownIdentity, "definitions[0].id");
    }

    TEST_CASE("Definition documents enforce schema fingerprint project and hard limits", "[platform-services][platform-definitions]") {
        const auto entry = Declaration(PlatformServiceIdKind::Stat, "stats.player_score");
        const auto stableIds = StableRegistry({entry});
        auto candidate = Candidate(stableIds, std::vector{Stat(entry)});
        ++candidate.schemaVersion;
        CheckFieldError(BuildStatDefinitionRegistry(stableIds, candidate), PlatformDefinitionErrors::UnsupportedVersion, "schemaVersion");
        candidate = Candidate(stableIds, std::vector{Stat(entry)});
        candidate.stableIdRegistryFingerprint.bytes.front() ^= 0xffU;
        CheckFieldError(BuildStatDefinitionRegistry(stableIds, candidate), PlatformDefinitionErrors::StaleIdentityRegistry,
                        "stableIdRegistryFingerprint");
        candidate = Candidate(stableIds, std::vector{Stat(entry)});
        CheckFieldError(BuildStatDefinitionRegistry(stableIds, candidate, {.maximumDefinitions = 0}),
                        PlatformDefinitionErrors::CapacityExceeded, "limits");
        CheckFieldError(BuildStatDefinitionRegistry(stableIds, candidate, {.maximumDefinitions = 4097}),
                        PlatformDefinitionErrors::CapacityExceeded, "limits");
    }

    TEST_CASE("Compatible replacement changes presentation but not durable semantics", "[platform-services][platform-definitions]") {
        const auto entry = Declaration(PlatformServiceIdKind::Stat, "stats.player_score");
        const auto stableIds = StableRegistry({entry});
        const auto initial = BuildStatDefinitionRegistry(stableIds, Candidate(stableIds, std::vector{Stat(entry)}));
        REQUIRE(initial.HasValue());

        auto presented = Stat(entry);
        presented.localizationKey = "stats.renamed_score";
        presented.hidden = true;
        const auto replacement =
            BuildStatDefinitionRegistryReplacement(initial.Value(), stableIds, Candidate(stableIds, std::vector{presented}));
        REQUIRE(replacement.HasValue());
        CHECK(replacement.Value().Find(presented.id).Value()->hidden);
        CHECK_FALSE(initial.Value().Find(presented.id).Value()->hidden);

        presented.mutation = StatMutationPolicy::AddOnce;
        CheckError(BuildStatDefinitionRegistryReplacement(initial.Value(), stableIds, Candidate(stableIds, std::vector{presented})),
                   PlatformDefinitionErrors::ImmutableContractChanged);
    }

    TEST_CASE("Replacement removal requires a retained tombstone and matching project namespace",
              "[platform-services][platform-definitions]") {
        const auto entry = Declaration(PlatformServiceIdKind::PresenceStatus, "presence.retired");
        const auto activeIds = StableRegistry({entry});
        const auto initial = BuildPresenceDefinitionRegistry(activeIds, Candidate(activeIds, std::vector{Presence(entry)}));
        REQUIRE(initial.HasValue());
        CheckError(BuildPresenceDefinitionRegistryReplacement(initial.Value(), activeIds,
                                                              Candidate(activeIds, std::vector<PresenceDefinition>{})),
                   PlatformDefinitionErrors::IncompleteRegistry);

        const auto retired = Declaration(PlatformServiceIdKind::PresenceStatus, "presence.retired", PlatformStableIdState::Tombstoned);
        const auto retiredIds = StableRegistry({retired});
        CHECK(BuildPresenceDefinitionRegistryReplacement(initial.Value(), retiredIds,
                                                         Candidate(retiredIds, std::vector<PresenceDefinition>{}))
                  .HasValue());

        const auto foreignIds = StableRegistry({entry}, "project.foreign");
        CheckError(BuildPresenceDefinitionRegistryReplacement(initial.Value(), foreignIds,
                                                              Candidate(foreignIds, std::vector{Presence(entry)})),
                   PlatformDefinitionErrors::StaleIdentityRegistry);
    }

    TEST_CASE("Canonical ordering and fingerprints cover every durable definition field", "[platform-services][platform-definitions]") {
        const auto first = Declaration(PlatformServiceIdKind::Stat, "stats.first");
        const auto second = Declaration(PlatformServiceIdKind::Stat, "stats.second");
        const auto stableIds = StableRegistry({first, second});
        auto firstDefinition = Stat(first);
        firstDefinition.valueKind = ProgressionValueKind::UnsignedInteger64;
        firstDefinition.range = {.minimum = 0, .maximum = 500};
        firstDefinition.mutation = StatMutationPolicy::SetMaximum;
        const auto registry = BuildStatDefinitionRegistry(stableIds, Candidate(stableIds, std::vector{Stat(second), firstDefinition}));
        REQUIRE(registry.HasValue());
        REQUIRE(registry.Value().Definitions().size() == 2);
        CHECK(registry.Value().Definitions()[0].id.value < registry.Value().Definitions()[1].id.value);
        CHECK(registry.Value().Find(firstDefinition.id).Value()->valueKind == ProgressionValueKind::UnsignedInteger64);

        firstDefinition.mutation = StatMutationPolicy::SetMinimum;
        const auto changed = BuildStatDefinitionRegistry(stableIds, Candidate(stableIds, std::vector{Stat(second), firstDefinition}));
        REQUIRE(changed.HasValue());
        CHECK(registry.Value().Fingerprint() != changed.Value().Fingerprint());
    }

    TEST_CASE("Unknown enums and bounded document fields fail before publication", "[platform-services][platform-definitions]") {
        const auto statEntry = Declaration(PlatformServiceIdKind::Stat, "stats.score");
        const auto boardEntry = Declaration(PlatformServiceIdKind::Leaderboard, "leaderboards.score");
        const auto presenceEntry = Declaration(PlatformServiceIdKind::PresenceStatus, "presence.playing");
        const auto stableIds = StableRegistry({statEntry, boardEntry, presenceEntry});
        auto stat = Stat(statEntry);
        stat.authority = static_cast<ProgressionAuthorityMode>(99);
        CheckFieldError(BuildStatDefinitionRegistry(stableIds, Candidate(stableIds, std::vector{stat})),
                        PlatformDefinitionErrors::InvalidDefinition, "definitions[0].authority");

        const auto stats = BuildStatDefinitionRegistry(stableIds, Candidate(stableIds, std::vector{Stat(statEntry)}));
        REQUIRE(stats.HasValue());
        auto board = Leaderboard(boardEntry);
        board.ordering = static_cast<LeaderboardOrdering>(99);
        CheckFieldError(BuildLeaderboardDefinitionRegistry(stableIds, stats.Value(), Candidate(stableIds, std::vector{board})),
                        PlatformDefinitionErrors::InvalidDefinition, "definitions[0].semantics");
        board = Leaderboard(boardEntry);
        board.localizationKey.clear();
        CheckFieldError(BuildLeaderboardDefinitionRegistry(stableIds, stats.Value(), Candidate(stableIds, std::vector{board})),
                        PlatformDefinitionErrors::InvalidDefinition, "definitions[0].localizationKey");

        auto presence = Presence(presenceEntry);
        presence.detailPolicy = static_cast<PresenceDetailPolicy>(99);
        CheckFieldError(BuildPresenceDefinitionRegistry(stableIds, Candidate(stableIds, std::vector{presence})),
                        PlatformDefinitionErrors::InvalidDefinition, "definitions[0].maximumDetailUtf8Bytes");
        presence = Presence(presenceEntry);
        presence.localizationKey = ".invalid";
        CheckFieldError(BuildPresenceDefinitionRegistry(stableIds, Candidate(stableIds, std::vector{presence})),
                        PlatformDefinitionErrors::InvalidDefinition, "definitions[0].localizationKey");

        auto overCapacity = Candidate(stableIds, std::vector{Stat(statEntry), Stat(statEntry)});
        CheckFieldError(BuildStatDefinitionRegistry(stableIds, overCapacity, {.maximumDefinitions = 1}),
                        PlatformDefinitionErrors::CapacityExceeded, "definitions");
    }

    TEST_CASE("Leaderboard and presence replacement preserve semantic contracts transactionally",
              "[platform-services][platform-definitions]") {
        const auto statEntry = Declaration(PlatformServiceIdKind::Stat, "stats.score");
        const auto boardEntry = Declaration(PlatformServiceIdKind::Leaderboard, "leaderboards.score");
        const auto presenceEntry = Declaration(PlatformServiceIdKind::PresenceStatus, "presence.playing");
        const auto stableIds = StableRegistry({statEntry, boardEntry, presenceEntry});
        const auto stats = BuildStatDefinitionRegistry(stableIds, Candidate(stableIds, std::vector{Stat(statEntry)}));
        REQUIRE(stats.HasValue());
        const auto boards =
            BuildLeaderboardDefinitionRegistry(stableIds, stats.Value(), Candidate(stableIds, std::vector{Leaderboard(boardEntry)}));
        const auto presence = BuildPresenceDefinitionRegistry(stableIds, Candidate(stableIds, std::vector{Presence(presenceEntry)}));
        REQUIRE(boards.HasValue());
        REQUIRE(presence.HasValue());

        auto changedBoard = Leaderboard(boardEntry);
        changedBoard.ordering = LeaderboardOrdering::LowestFirst;
        CheckError(BuildLeaderboardDefinitionRegistryReplacement(boards.Value(), stableIds, stats.Value(),
                                                                 Candidate(stableIds, std::vector{changedBoard})),
                   PlatformDefinitionErrors::ImmutableContractChanged);
        auto changedPresence = Presence(presenceEntry);
        changedPresence.maximumDetailUtf8Bytes = 64;
        CheckError(BuildPresenceDefinitionRegistryReplacement(presence.Value(), stableIds,
                                                              Candidate(stableIds, std::vector{changedPresence})),
                   PlatformDefinitionErrors::ImmutableContractChanged);

        const auto foreignStatEntry = Declaration(PlatformServiceIdKind::Stat, "stats.foreign");
        const auto foreignIds = StableRegistry({foreignStatEntry}, "project.foreign");
        const auto foreignStats = BuildStatDefinitionRegistry(foreignIds, Candidate(foreignIds, std::vector{Stat(foreignStatEntry)}));
        REQUIRE(foreignStats.HasValue());
        CheckFieldError(BuildLeaderboardDefinitionRegistry(stableIds, foreignStats.Value(),
                                                           Candidate(stableIds, std::vector{Leaderboard(boardEntry)})),
                        PlatformDefinitionErrors::InvalidCrossReference, "statRegistry");
    }
}  // namespace Horo::PlatformServices
