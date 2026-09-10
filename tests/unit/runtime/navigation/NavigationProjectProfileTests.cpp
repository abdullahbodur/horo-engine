#include "Horo/Navigation/NavigationErrors.h"
#include "Horo/Navigation/NavigationProjectProfiles.h"
#include "navigation/NavigationTestAssertions.h"

#include <array>
#include <limits>

namespace Horo::Navigation {
    namespace {
        NavigationProjectProfileInput ProfileInput(const std::uint64_t revision = 1) {
            NavigationProjectProfileInput input{
                .id = NavigationProjectProfileId::Create(17).Value(),
                .revision = NavigationProjectProfileRevision::Create(revision).Value(),
                .capacities = {.maximumAgents = 64,
                               .maximumSurfaces = 4,
                               .maximumResidentTiles = 8,
                               .maximumConcurrentQueries = 2,
                               .maximumBytesPerResidentTile = 512,
                               .maximumResidentMemoryBytes = 4'096,
                               .maximumWorkUnitsPerTick = 2'048},
                .maximumQuery = {.query = NavigationQueryKind::Path,
                                 .quality = NavigationQualityLevel::Balanced,
                                 .limits = {.maximumNodeExpansions = 1'024,
                                            .maximumResultPoints = 128,
                                            .maximumSearchDistanceMeters = 2'000.0F}},
            };
            input.capabilities.fill(NavigationCapabilityRequirement::Optional);
            input.capabilities[static_cast<std::size_t>(NavigationCapability::GroundedQueries)] = NavigationCapabilityRequirement::Required;
            return input;
        }

        NavigationProjectProfile Profile(const std::uint64_t revision = 1) {
            return NavigationProjectProfile::Create(ProfileInput(revision)).Value();
        }

        NavigationProviderCapabilities Provider() {
            return MakeAvailablePathQueryCapabilities(9, ProfileInput().maximumQuery.limits, 2);
        }

        ResolvedNavigationProjectProfile Resolved() {
            return ResolveNavigationProjectProfile(Profile()).Value();
        }
    }  // namespace

    TEST_CASE("Navigation project profile owns deterministic typed authority", "[unit][navigation][profile][project]") {
        const auto first = Profile();
        const auto second = Profile();
        REQUIRE(first.Id() == second.Id());
        REQUIRE(first.Revision() == second.Revision());
        REQUIRE(first.Fingerprint() == second.Fingerprint());
        REQUIRE(first.Capacities() == ProfileInput().capacities);
        REQUIRE(first.MaximumQuery().quality == NavigationQualityLevel::Balanced);
        REQUIRE(first.Requirement(NavigationCapability::GroundedQueries) == NavigationCapabilityRequirement::Required);
        REQUIRE(first.Requirement(NavigationCapability::TopologyUpdates) == NavigationCapabilityRequirement::Optional);
        REQUIRE(first.Requirement(NavigationCapability::Count) == NavigationCapabilityRequirement::Count);

        auto changed = ProfileInput();
        ++changed.capacities.maximumAgents;
        REQUIRE(NavigationProjectProfile::Create(changed).Value().Fingerprint() != first.Fingerprint());
    }

    TEST_CASE("Navigation project capacities reject every zero field", "[unit][navigation][profile][limits]") {
        const std::array integralFields{
            &NavigationCapacityLimits::maximumAgents,
            &NavigationCapacityLimits::maximumSurfaces,
            &NavigationCapacityLimits::maximumResidentTiles,
            &NavigationCapacityLimits::maximumConcurrentQueries,
        };
        for (const auto field : integralFields) {
            auto input = ProfileInput();
            input.capacities.*field = 0;
            TestSupport::RequireError(NavigationProjectProfile::Create(input), NavigationErrors::ProjectProfileInvalid);
        }
        const std::array wideFields{
            &NavigationCapacityLimits::maximumBytesPerResidentTile,
            &NavigationCapacityLimits::maximumResidentMemoryBytes,
            &NavigationCapacityLimits::maximumWorkUnitsPerTick,
        };
        for (const auto field : wideFields) {
            auto input = ProfileInput();
            input.capacities.*field = 0;
            TestSupport::RequireError(NavigationProjectProfile::Create(input), NavigationErrors::ProjectProfileInvalid);
        }

        auto input = ProfileInput();
        input.id = {};
        TestSupport::RequireError(NavigationProjectProfile::Create(input), NavigationErrors::ProjectProfileInvalid);
        input = ProfileInput();
        input.revision = {};
        TestSupport::RequireError(NavigationProjectProfile::Create(input), NavigationErrors::ProjectProfileInvalid);
        input = ProfileInput();
        input.capabilities.front() = NavigationCapabilityRequirement::Count;
        TestSupport::RequireError(NavigationProjectProfile::Create(input), NavigationErrors::ProjectProfileInvalid);
    }

    TEST_CASE("Navigation project query envelopes reject malformed finite limits", "[unit][navigation][profile][query]") {
        auto input = ProfileInput();
        input.maximumQuery.query = NavigationQueryKind::Count;
        TestSupport::RequireError(NavigationProjectProfile::Create(input), NavigationErrors::ProjectProfileInvalid);
        input = ProfileInput();
        input.maximumQuery.quality = NavigationQualityLevel::Count;
        TestSupport::RequireError(NavigationProjectProfile::Create(input), NavigationErrors::ProjectProfileInvalid);
        input = ProfileInput();
        input.maximumQuery.limits.maximumNodeExpansions = 0;
        TestSupport::RequireError(NavigationProjectProfile::Create(input), NavigationErrors::ProjectProfileInvalid);
        input = ProfileInput();
        input.maximumQuery.limits.maximumResultPoints = 0;
        TestSupport::RequireError(NavigationProjectProfile::Create(input), NavigationErrors::ProjectProfileInvalid);

        for (const float value : {0.0F, -1.0F, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()}) {
            input = ProfileInput();
            input.maximumQuery.limits.maximumSearchDistanceMeters = value;
            TestSupport::RequireError(NavigationProjectProfile::Create(input), NavigationErrors::ProjectProfileInvalid);
        }
    }

    TEST_CASE("Navigation project aggregate memory and query work use checked arithmetic", "[unit][navigation][profile][overflow]") {
        auto input = ProfileInput();
        input.capacities.maximumResidentTiles = std::numeric_limits<std::uint32_t>::max();
        input.capacities.maximumBytesPerResidentTile = std::numeric_limits<std::uint64_t>::max();
        input.capacities.maximumResidentMemoryBytes = std::numeric_limits<std::uint64_t>::max();
        TestSupport::RequireError(NavigationProjectProfile::Create(input), NavigationErrors::ProjectProfileCapacityExceeded);

        input = ProfileInput();
        input.capacities.maximumWorkUnitsPerTick = 2'047;
        TestSupport::RequireError(NavigationProjectProfile::Create(input), NavigationErrors::ProjectProfileCapacityExceeded);
        input = ProfileInput();
        input.capacities.maximumResidentMemoryBytes = 4'095;
        TestSupport::RequireError(NavigationProjectProfile::Create(input), NavigationErrors::ProjectProfileCapacityExceeded);
        input = ProfileInput();
        input.capacities.maximumBytesPerResidentTile = 513;
        TestSupport::RequireError(NavigationProjectProfile::Create(input), NavigationErrors::ProjectProfileCapacityExceeded);
    }

    TEST_CASE("Navigation project replacement is monotonic and preserves the last good value", "[unit][navigation][profile][replacement]") {
        const auto previous = Profile();
        auto input = ProfileInput();
        TestSupport::RequireError(NavigationProjectProfile::Replace(previous, input), NavigationErrors::ProjectProfileStale);
        input = ProfileInput(2);
        input.id = NavigationProjectProfileId::Create(18).Value();
        TestSupport::RequireError(NavigationProjectProfile::Replace(previous, input), NavigationErrors::ProjectProfileStale);
        input = ProfileInput(2);
        input.capacities.maximumAgents = 0;
        TestSupport::RequireError(NavigationProjectProfile::Replace(previous, input), NavigationErrors::ProjectProfileInvalid);
        REQUIRE(previous.Revision().Value() == 1);
        REQUIRE(previous.Capacities().maximumAgents == 64);

        input = ProfileInput(2);
        input.capacities.maximumAgents = 65;
        const auto successor = NavigationProjectProfile::Replace(previous, input);
        REQUIRE(successor.HasValue());
        REQUIRE(successor.Value().Revision().Value() == 2);
        REQUIRE(successor.Value().Fingerprint() != previous.Fingerprint());
    }

    TEST_CASE("Developer preview capacities clamp to exact project authority", "[unit][navigation][profile][preview]") {
        const auto project = Profile();
        const auto authoritative = ResolveNavigationProjectProfile(project);
        REQUIRE(authoritative.HasValue());
        REQUIRE_FALSE(authoritative.Value().previewRevision.has_value());
        REQUIRE(authoritative.Value().capacities == project.Capacities());

        auto requested = project.Capacities();
        requested.maximumAgents = 16;
        requested.maximumResidentTiles = 100;
        const NavigationDeveloperPreviewPreference preview{
            .revision = NavigationPreviewPreferenceRevision::Create(3).Value(),
            .projectRevision = project.Revision(),
            .requestedMaximums = requested,
        };
        const auto resolved = ResolveNavigationProjectProfile(project, preview);
        REQUIRE(resolved.HasValue());
        REQUIRE(resolved.Value().capacities.maximumAgents == 16);
        REQUIRE(resolved.Value().capacities.maximumResidentTiles == project.Capacities().maximumResidentTiles);
        REQUIRE(resolved.Value().projectFingerprint == project.Fingerprint());

        auto stale = preview;
        stale.projectRevision = NavigationProjectProfileRevision::Create(2).Value();
        TestSupport::RequireError(ResolveNavigationProjectProfile(project, stale), NavigationErrors::ProjectProfileStale);
        auto invalid = preview;
        invalid.requestedMaximums.maximumWorkUnitsPerTick = 0;
        TestSupport::RequireError(ResolveNavigationProjectProfile(project, invalid), NavigationErrors::ProjectProfileInvalid);
    }

    TEST_CASE("Every resolved navigation capacity admits exact and rejects one over", "[unit][navigation][profile][admission]") {
        const auto profile = Resolved();
        const NavigationCapacityUsage exact{.agents = 64,
                                            .surfaces = 4,
                                            .residentTiles = 8,
                                            .concurrentQueries = 2,
                                            .residentMemoryBytes = 4'096,
                                            .workUnitsThisTick = 2'048};
        REQUIRE(AdmitNavigationCapacity(profile, exact).HasValue());

        auto excessive = exact;
        ++excessive.agents;
        TestSupport::RequireError(AdmitNavigationCapacity(profile, excessive), NavigationErrors::ProjectProfileCapacityExceeded);
        excessive = exact;
        ++excessive.surfaces;
        TestSupport::RequireError(AdmitNavigationCapacity(profile, excessive), NavigationErrors::ProjectProfileCapacityExceeded);
        excessive = exact;
        ++excessive.residentTiles;
        TestSupport::RequireError(AdmitNavigationCapacity(profile, excessive), NavigationErrors::ProjectProfileCapacityExceeded);
        excessive = exact;
        ++excessive.concurrentQueries;
        TestSupport::RequireError(AdmitNavigationCapacity(profile, excessive), NavigationErrors::ProjectProfileCapacityExceeded);
        excessive = exact;
        ++excessive.residentMemoryBytes;
        TestSupport::RequireError(AdmitNavigationCapacity(profile, excessive), NavigationErrors::ProjectProfileCapacityExceeded);
        excessive = exact;
        ++excessive.workUnitsThisTick;
        TestSupport::RequireError(AdmitNavigationCapacity(profile, excessive), NavigationErrors::ProjectProfileCapacityExceeded);
    }

    TEST_CASE("Provider admission rejects stale unsupported and undersized evidence before activation",
              "[unit][navigation][profile][provider]") {
        const auto profile = Profile();
        auto provider = Provider();
        REQUIRE(AdmitNavigationProjectProfile(profile, provider, 9).HasValue());
        TestSupport::RequireError(AdmitNavigationProjectProfile(profile, provider, 8), NavigationErrors::CapabilityStale);

        provider.capabilities[static_cast<std::size_t>(NavigationCapability::GroundedQueries)] = NavigationSupport::Unsupported;
        provider.querySupport[static_cast<std::size_t>(NavigationQueryKind::Path)].fill(NavigationSupport::Unsupported);
        provider.queryLimits[static_cast<std::size_t>(NavigationQueryKind::Path)].fill({});
        provider.maximumConcurrentQueries = 0;
        REQUIRE(ValidateNavigationProviderCapabilities(provider));
        TestSupport::RequireError(AdmitNavigationProjectProfile(profile, provider, 9), NavigationErrors::OperationUnsupported);

        provider = Provider();
        provider.maximumConcurrentQueries = 1;
        TestSupport::RequireError(AdmitNavigationProjectProfile(profile, provider, 9), NavigationErrors::ProjectProfileCapacityExceeded);

        provider = Provider();
        provider
            .queryLimits[static_cast<std::size_t>(NavigationQueryKind::Path)][static_cast<std::size_t>(NavigationQualityLevel::Balanced)]
            .maximumNodeExpansions = 1'023;
        TestSupport::RequireError(AdmitNavigationProjectProfile(profile, provider, 9), NavigationErrors::QueryLimitExceeded);

        provider = Provider();
        provider
            .queryLimits[static_cast<std::size_t>(NavigationQueryKind::Path)][static_cast<std::size_t>(NavigationQualityLevel::Balanced)]
            .maximumResultPoints = 127;
        TestSupport::RequireError(AdmitNavigationProjectProfile(profile, provider, 9), NavigationErrors::QueryLimitExceeded);

        provider = Provider();
        provider
            .queryLimits[static_cast<std::size_t>(NavigationQueryKind::Path)][static_cast<std::size_t>(NavigationQualityLevel::Balanced)]
            .maximumSearchDistanceMeters = 1'999.0F;
        TestSupport::RequireError(AdmitNavigationProjectProfile(profile, provider, 9), NavigationErrors::QueryLimitExceeded);
    }

    TEST_CASE("Packaged editor and headless consumers share one renderer-independent resolver", "[unit][navigation][profile][headless]") {
        const auto project = Profile();
        const std::array resolved{ResolveNavigationProjectProfile(project), ResolveNavigationProjectProfile(project),
                                  ResolveNavigationProjectProfile(project)};
        for (const auto &host : resolved) {
            REQUIRE(host.HasValue());
            REQUIRE(host.Value().projectFingerprint == project.Fingerprint());
            REQUIRE(host.Value().capacities == project.Capacities());
        }
    }
}  // namespace Horo::Navigation
