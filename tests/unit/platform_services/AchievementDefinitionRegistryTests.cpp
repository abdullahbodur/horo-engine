#include "Horo/PlatformServices/AchievementDefinitionRegistry.h"

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
            std::uint8_t nextByte = 31;
            std::ranges::generate(salt.bytes, [&nextByte] {
                return static_cast<std::byte>(nextByte++);
            });
            return salt;
        }

        [[nodiscard]] PlatformStableIdDeclaration StableDeclaration(std::string key,
                                                                    const PlatformStableIdState state = PlatformStableIdState::Active) {
            const auto id = DerivePlatformServiceStableId(TestSalt(), PlatformServiceIdKind::Achievement, key);
            REQUIRE(id.HasValue());
            return {.kind = PlatformServiceIdKind::Achievement,
                    .canonicalKey = std::move(key),
                    .storedId = id.Value(),
                    .state = state,
                    .removalProvenance = state == PlatformStableIdState::Tombstoned ? "removed-by-migration:PLS-003.3" : ""};
        }

        [[nodiscard]] PlatformStableIdRegistry StableRegistry(std::vector<PlatformStableIdDeclaration> entries,
                                                              std::string projectId = "project.achievement-registry") {
            PlatformStableIdRegistryCandidate candidate{.projectId = std::move(projectId),
                                                        .salt = TestSalt(),
                                                        .entries = std::move(entries)};
            auto built = BuildPlatformStableIdRegistry(candidate);
            REQUIRE(built.HasValue());
            return std::move(built).Value();
        }

        [[nodiscard]] AchievementDefinition Definition(const PlatformStableIdDeclaration &stable,
                                                       const AchievementProgressKind progress = AchievementProgressKind::UnlockOnce) {
            return {.id = AchievementId{stable.storedId.value},
                    .authority = ProgressionAuthorityMode::LocalProduct,
                    .progress = {.kind = progress, .total = progress == AchievementProgressKind::UnlockOnce ? 1U : 100U},
                    .presentation = {.titleLocalizationKey = "achievements.first_win.title",
                                     .descriptionLocalizationKey = "achievements.first_win.description"}};
        }

        [[nodiscard]] AchievementDefinitionRegistryCandidate Candidate(const PlatformStableIdRegistry &stableIds,
                                                                       std::vector<AchievementDefinition> definitions) {
            return {.stableIdRegistryFingerprint = stableIds.Fingerprint(), .definitions = std::move(definitions)};
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

        void CheckEquivalentFingerprints(const auto &first, const auto &second) {
            REQUIRE(first.HasValue());
            REQUIRE(second.HasValue());
            CHECK(first.Value().Fingerprint() == second.Value().Fingerprint());
        }
    }  // namespace

    static_assert(!std::same_as<AchievementId, PlatformServiceStableIdValue>);
    static_assert(std::is_copy_constructible_v<AchievementDefinition>);

    TEST_CASE("Achievement definitions build deterministic immutable snapshots", "[platform-services][achievement-definition]") {
        const auto firstStable = StableDeclaration("campaign.first_win");
        const auto veteranStable = StableDeclaration("campaign.veteran");
        const auto stableIds = StableRegistry({veteranStable, firstStable});
        auto firstDefinition = Definition(firstStable);
        auto veteranDefinition = Definition(veteranStable, AchievementProgressKind::SetProgressMaximum);
        veteranDefinition.authority = ProgressionAuthorityMode::AuthorityServer;
        veteranDefinition.presentation.titleLocalizationKey = "achievements.veteran.title";
        veteranDefinition.presentation.descriptionLocalizationKey = "achievements.veteran.description";
        veteranDefinition.presentation.hidden = true;

        const auto first = BuildAchievementDefinitionRegistry(stableIds, Candidate(stableIds, {veteranDefinition, firstDefinition}));
        const auto second = BuildAchievementDefinitionRegistry(stableIds, Candidate(stableIds, {firstDefinition, veteranDefinition}));
        CheckEquivalentFingerprints(first, second);
        CHECK(first.Value().StableIdProjectId() == "project.achievement-registry");
        CHECK(first.Value().StableIdRegistryFingerprint() == stableIds.Fingerprint());
        REQUIRE(first.Value().Definitions().size() == 2);
        CHECK(first.Value().Definitions().front().id.value < first.Value().Definitions().back().id.value);

        const auto found = first.Value().Find(veteranDefinition.id);
        REQUIRE(found.HasValue());
        CHECK(found.Value()->authority == ProgressionAuthorityMode::AuthorityServer);
        CHECK(found.Value()->progress.total == 100);
        CHECK(found.Value()->presentation.hidden);
        CheckError(first.Value().Find({}), AchievementDefinitionErrors::UnknownIdentity);
    }

    TEST_CASE("Achievement definition fields fail with exact diagnostics", "[platform-services][achievement-definition]") {
        const auto stable = StableDeclaration("campaign.first_win");
        const auto stableIds = StableRegistry({stable});
        const auto valid = Definition(stable);

        auto invalidId = valid;
        invalidId.id = {};
        CheckFieldError(BuildAchievementDefinitionRegistry(stableIds, Candidate(stableIds, {invalidId})),
                        AchievementDefinitionErrors::InvalidDefinition, "definitions[0].id");
        auto invalidAuthority = valid;
        invalidAuthority.authority = static_cast<ProgressionAuthorityMode>(99);
        CheckFieldError(BuildAchievementDefinitionRegistry(stableIds, Candidate(stableIds, {invalidAuthority})),
                        AchievementDefinitionErrors::InvalidDefinition, "definitions[0].authority");
        auto invalidProgress = valid;
        invalidProgress.progress.total = 2;
        CheckFieldError(BuildAchievementDefinitionRegistry(stableIds, Candidate(stableIds, {invalidProgress})),
                        AchievementDefinitionErrors::InvalidDefinition, "definitions[0].progress");
        auto invalidTitle = valid;
        invalidTitle.presentation.titleLocalizationKey = "Achievements Title";
        CheckFieldError(BuildAchievementDefinitionRegistry(stableIds, Candidate(stableIds, {invalidTitle})),
                        AchievementDefinitionErrors::InvalidDefinition, "definitions[0].presentation.titleLocalizationKey");
        auto missingDescription = valid;
        missingDescription.presentation.descriptionLocalizationKey.clear();
        CheckFieldError(BuildAchievementDefinitionRegistry(stableIds, Candidate(stableIds, {missingDescription})),
                        AchievementDefinitionErrors::InvalidDefinition, "definitions[0].presentation.descriptionLocalizationKey");
    }

    TEST_CASE("Achievement progress schemas enforce portable bounded semantics", "[platform-services][achievement-definition]") {
        const auto stable = StableDeclaration("campaign.progress");
        const auto stableIds = StableRegistry({stable});
        auto definition = Definition(stable, AchievementProgressKind::SetProgressMaximum);

        definition.progress.total = 1;
        CheckFieldError(BuildAchievementDefinitionRegistry(stableIds, Candidate(stableIds, {definition})),
                        AchievementDefinitionErrors::InvalidDefinition, "definitions[0].progress");
        definition.progress.total = 101;
        CheckFieldError(BuildAchievementDefinitionRegistry(stableIds, Candidate(stableIds, {definition}), {.maximumProgressTotal = 100}),
                        AchievementDefinitionErrors::InvalidDefinition, "definitions[0].progress");
        definition.progress = {.kind = static_cast<AchievementProgressKind>(99), .total = 2};
        CheckFieldError(BuildAchievementDefinitionRegistry(stableIds, Candidate(stableIds, {definition})),
                        AchievementDefinitionErrors::InvalidDefinition, "definitions[0].progress");
    }

    TEST_CASE("Achievement registry rejects duplicate, incomplete, unknown, and tombstoned identities",
              "[platform-services][achievement-definition]") {
        const auto first = StableDeclaration("campaign.first_win");
        const auto second = StableDeclaration("campaign.second_win");
        const auto stableIds = StableRegistry({first, second});
        const auto definition = Definition(first);

        CheckFieldError(BuildAchievementDefinitionRegistry(stableIds, Candidate(stableIds, {definition, definition})),
                        AchievementDefinitionErrors::DuplicateDefinition, "definitions[1].id");
        CheckError(BuildAchievementDefinitionRegistry(stableIds, Candidate(stableIds, {definition})),
                   AchievementDefinitionErrors::IncompleteRegistry);

        const auto foreign = StableDeclaration("campaign.foreign");
        CheckFieldError(BuildAchievementDefinitionRegistry(stableIds, Candidate(stableIds, {Definition(foreign), Definition(second)})),
                        AchievementDefinitionErrors::UnknownIdentity, "definitions[0].id");

        const auto retired = StableDeclaration("campaign.retired", PlatformStableIdState::Tombstoned);
        const auto retiredIds = StableRegistry({retired});
        CheckFieldError(BuildAchievementDefinitionRegistry(retiredIds, Candidate(retiredIds, {Definition(retired)})),
                        AchievementDefinitionErrors::UnknownIdentity, "definitions[0].id");
        CHECK(BuildAchievementDefinitionRegistry(retiredIds, Candidate(retiredIds, {})).HasValue());
    }

    TEST_CASE("Achievement registry validates document version fingerprint and finite limits",
              "[platform-services][achievement-definition]") {
        const auto stable = StableDeclaration("campaign.first_win");
        const auto stableIds = StableRegistry({stable});
        auto candidate = Candidate(stableIds, {Definition(stable)});

        ++candidate.schemaVersion;
        CheckFieldError(BuildAchievementDefinitionRegistry(stableIds, candidate), AchievementDefinitionErrors::UnsupportedVersion,
                        "schemaVersion");
        candidate = Candidate(stableIds, {Definition(stable)});
        candidate.stableIdRegistryFingerprint.bytes.front() ^= 0xffU;
        CheckFieldError(BuildAchievementDefinitionRegistry(stableIds, candidate), AchievementDefinitionErrors::StaleIdentityRegistry,
                        "stableIdRegistryFingerprint");
        candidate = Candidate(stableIds, {Definition(stable)});
        CheckFieldError(BuildAchievementDefinitionRegistry(stableIds, candidate, {.maximumDefinitions = 0}),
                        AchievementDefinitionErrors::CapacityExceeded, "limits");
        CheckFieldError(BuildAchievementDefinitionRegistry(stableIds, candidate, {.maximumDefinitions = 4097}),
                        AchievementDefinitionErrors::CapacityExceeded, "limits");
        CheckFieldError(BuildAchievementDefinitionRegistry(stableIds, candidate,
                                                           {.maximumDefinitions = 1, .maximumLocalizationKeyBytes = 8}),
                        AchievementDefinitionErrors::InvalidDefinition, "definitions[0].presentation.titleLocalizationKey");
    }

    TEST_CASE("Presentation can change while published authority and progress remain immutable",
              "[platform-services][achievement-definition]") {
        const auto stable = StableDeclaration("campaign.first_win");
        const auto stableIds = StableRegistry({stable});
        const auto initial = BuildAchievementDefinitionRegistry(stableIds, Candidate(stableIds, {Definition(stable)}));
        REQUIRE(initial.HasValue());

        auto renamed = Definition(stable);
        renamed.presentation.titleLocalizationKey = "achievements.first_win.renamed_title";
        renamed.presentation.hidden = true;
        const auto replacement = BuildAchievementDefinitionRegistryReplacement(initial.Value(), stableIds, Candidate(stableIds, {renamed}));
        REQUIRE(replacement.HasValue());
        CHECK(replacement.Value().Find(renamed.id).Value()->presentation.hidden);
        CHECK_FALSE(initial.Value().Find(renamed.id).Value()->presentation.hidden);

        auto changedAuthority = renamed;
        changedAuthority.authority = ProgressionAuthorityMode::AuthorityServer;
        CheckError(BuildAchievementDefinitionRegistryReplacement(initial.Value(), stableIds, Candidate(stableIds, {changedAuthority})),
                   AchievementDefinitionErrors::ImmutableContractChanged);
        auto changedProgress = renamed;
        changedProgress.progress = {.kind = AchievementProgressKind::SetProgressMaximum, .total = 10};
        CheckError(BuildAchievementDefinitionRegistryReplacement(initial.Value(), stableIds, Candidate(stableIds, {changedProgress})),
                   AchievementDefinitionErrors::ImmutableContractChanged);
    }

    TEST_CASE("Replacement removes definitions only after the stable identity is tombstoned",
              "[platform-services][achievement-definition]") {
        const auto stable = StableDeclaration("campaign.retired");
        const auto activeIds = StableRegistry({stable});
        const auto initial = BuildAchievementDefinitionRegistry(activeIds, Candidate(activeIds, {Definition(stable)}));
        REQUIRE(initial.HasValue());

        CheckError(BuildAchievementDefinitionRegistryReplacement(initial.Value(), activeIds, Candidate(activeIds, {})),
                   AchievementDefinitionErrors::IncompleteRegistry);
        const auto retired = StableDeclaration("campaign.retired", PlatformStableIdState::Tombstoned);
        const auto retiredIds = StableRegistry({retired});
        const auto replacement = BuildAchievementDefinitionRegistryReplacement(initial.Value(), retiredIds, Candidate(retiredIds, {}));
        REQUIRE(replacement.HasValue());
        CHECK(replacement.Value().Definitions().empty());
        CHECK(initial.Value().Find(Definition(stable).id).HasValue());
    }

    TEST_CASE("Replacement cannot cross project identity namespaces", "[platform-services][achievement-definition]") {
        const auto stable = StableDeclaration("campaign.first_win");
        const auto firstProject = StableRegistry({stable});
        const auto secondProject = StableRegistry({stable}, "project.independent-fork");
        const auto initial = BuildAchievementDefinitionRegistry(firstProject, Candidate(firstProject, {Definition(stable)}));
        REQUIRE(initial.HasValue());

        CheckFieldError(BuildAchievementDefinitionRegistryReplacement(initial.Value(), secondProject,
                                                                      Candidate(secondProject, {Definition(stable)})),
                        AchievementDefinitionErrors::StaleIdentityRegistry, "stableIdRegistryFingerprint");
        CHECK(initial.Value().StableIdProjectId() == "project.achievement-registry");
    }
}  // namespace Horo::PlatformServices
