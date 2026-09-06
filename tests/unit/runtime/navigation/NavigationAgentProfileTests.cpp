#include "Horo/Navigation/NavigationAgentProfiles.h"
#include "Horo/Navigation/NavigationErrors.h"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <limits>
#include <utility>

namespace Horo::Navigation {
    namespace {
        NavigationAgentProfileId ProfileId(const std::uint64_t value) {
            return NavigationAgentProfileId::Create(value).Value();
        }

        NavigationAgentProfileDescriptor Profile(const std::uint64_t id, std::string displayName) {
            return {.id = ProfileId(id), .displayName = std::move(displayName), .buildGeometry = {}};
        }

        template <typename T> void RequireError(const Result<T> &result, const ErrorCodeDescriptor &expected) {
            REQUIRE(result.HasError());
            REQUIRE(result.ErrorValue().domain.Value() == expected.domain.Value());
            REQUIRE(result.ErrorValue().code.Value() == expected.code.Value());
        }
    }  // namespace

    TEST_CASE("Grounded agent profile dimensions reject non-finite and negative values", "[unit][navigation][profile]") {
        const std::array fields{
            &NavigationAgentBuildGeometry::radiusMeters,
            &NavigationAgentBuildGeometry::heightMeters,
            &NavigationAgentBuildGeometry::maxSlopeDegrees,
            &NavigationAgentBuildGeometry::stepHeightMeters,
            &NavigationAgentBuildGeometry::cellSizeMeters,
            &NavigationAgentBuildGeometry::cellHeightMeters,
            &NavigationAgentBuildGeometry::minimumRegionSizeMeters,
        };
        const std::array invalidFiniteValues{std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(),
                                             std::numeric_limits<float>::quiet_NaN()};
        for (const auto field : fields) {
            for (const float invalid : invalidFiniteValues) {
                auto profile = Profile(1, "Human");
                profile.buildGeometry.*field = invalid;
                RequireError(ValidateNavigationAgentProfile(profile), NavigationErrors::AgentProfileInvalid);
            }
            auto profile = Profile(1, "Human");
            profile.buildGeometry.*field = -0.01F;
            RequireError(ValidateNavigationAgentProfile(profile), NavigationErrors::AgentProfileInvalid);
        }

        auto profile = Profile(1, "Human");
        profile.buildGeometry.maxSlopeDegrees = 0.0F;
        profile.buildGeometry.stepHeightMeters = 0.0F;
        profile.buildGeometry.minimumRegionSizeMeters = 0.0F;
        REQUIRE(ValidateNavigationAgentProfile(profile).HasValue());
    }

    TEST_CASE("Grounded agent profiles validate identity name and geometry relationships before baking", "[unit][navigation][profile]") {
        auto profile = Profile(1, "Human");
        profile.id = {};
        RequireError(ValidateNavigationAgentProfile(profile), NavigationErrors::AgentProfileInvalid);
        profile = Profile(1, " \t");
        RequireError(ValidateNavigationAgentProfile(profile), NavigationErrors::AgentProfileInvalid);
        profile = Profile(1, "Human");
        profile.buildGeometry.radiusMeters = 0.0F;
        RequireError(ValidateNavigationAgentProfile(profile), NavigationErrors::AgentProfileInvalid);
        profile = Profile(1, "Human");
        profile.buildGeometry.heightMeters = 2.0F * profile.buildGeometry.radiusMeters - 0.01F;
        RequireError(ValidateNavigationAgentProfile(profile), NavigationErrors::AgentProfileInvalid);
        profile = Profile(1, "Human");
        profile.buildGeometry.maxSlopeDegrees = 90.0F;
        RequireError(ValidateNavigationAgentProfile(profile), NavigationErrors::AgentProfileInvalid);
        profile = Profile(1, "Human");
        profile.buildGeometry.stepHeightMeters = profile.buildGeometry.heightMeters;
        RequireError(ValidateNavigationAgentProfile(profile), NavigationErrors::AgentProfileInvalid);
        profile = Profile(1, "Human");
        profile.buildGeometry.cellSizeMeters = 0.0F;
        RequireError(ValidateNavigationAgentProfile(profile), NavigationErrors::AgentProfileInvalid);
        profile = Profile(1, "Human");
        profile.buildGeometry.cellHeightMeters = 0.0F;
        RequireError(ValidateNavigationAgentProfile(profile), NavigationErrors::AgentProfileInvalid);
        profile = Profile(1, "Human");
        profile.buildGeometry.minimumRegionSizeMeters = -0.01F;
        RequireError(ValidateNavigationAgentProfile(profile), NavigationErrors::AgentProfileInvalid);
    }

    TEST_CASE("Rename preserves profile identity and duplication creates an independent build partition", "[unit][navigation][profile]") {
        const auto original = Profile(10, "Human");
        const auto renamed = RenameNavigationAgentProfile(original, "Localized Human");
        REQUIRE(renamed.HasValue());
        REQUIRE(renamed.Value().id == original.id);
        REQUIRE(renamed.Value().buildGeometry.radiusMeters == original.buildGeometry.radiusMeters);

        auto duplicateResult = DuplicateNavigationAgentProfile(original, ProfileId(20), "Large Creature");
        REQUIRE(duplicateResult.HasValue());
        auto duplicate = std::move(duplicateResult).Value();
        REQUIRE(duplicate.id != original.id);
        duplicate.buildGeometry.radiusMeters = 0.9F;
        duplicate.buildGeometry.heightMeters = 2.4F;
        REQUIRE(duplicate.buildGeometry.radiusMeters != original.buildGeometry.radiusMeters);
        REQUIRE(ValidateNavigationAgentProfile(original).HasValue());
        REQUIRE(ValidateNavigationAgentProfile(duplicate).HasValue());

        RequireError(DuplicateNavigationAgentProfile(original, original.id, "Copy"), NavigationErrors::AgentProfileInvalid);
        RequireError(DuplicateNavigationAgentProfile(original, {}, "Copy"), NavigationErrors::AgentProfileInvalid);
        RequireError(RenameNavigationAgentProfile(original, ""), NavigationErrors::AgentProfileInvalid);
    }

    TEST_CASE("Profile deletion requires explicit deterministic reference repair", "[unit][navigation][profile]") {
        const std::array references{ProfileId(1), ProfileId(2), ProfileId(1), ProfileId(3)};
        RequireError(RepairNavigationAgentProfileReferences(references, ProfileId(1)), NavigationErrors::AgentProfileReferenced);

        const auto repaired = RepairNavigationAgentProfileReferences(references, ProfileId(1), ProfileId(4));
        REQUIRE(repaired.HasValue());
        REQUIRE(repaired.Value().repairedCount == 2);
        REQUIRE(repaired.Value().references == std::vector{ProfileId(4), ProfileId(2), ProfileId(4), ProfileId(3)});

        const auto unreferenced = RepairNavigationAgentProfileReferences(references, ProfileId(5));
        REQUIRE(unreferenced.HasValue());
        REQUIRE(unreferenced.Value().repairedCount == 0);
        REQUIRE(unreferenced.Value().references == std::vector<NavigationAgentProfileId>{references.begin(), references.end()});
        RequireError(RepairNavigationAgentProfileReferences(references, {}, ProfileId(4)), NavigationErrors::AgentProfileInvalid);
        RequireError(RepairNavigationAgentProfileReferences(references, ProfileId(1), ProfileId(1)), NavigationErrors::AgentProfileInvalid);
    }
}  // namespace Horo::Navigation
