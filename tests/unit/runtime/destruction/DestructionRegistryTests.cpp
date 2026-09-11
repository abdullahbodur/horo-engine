#include "Horo/Destruction/DestructionRegistry.h"
#include "support/AllocationProbe.h"

#include <array>
#include <catch2/catch_test_macros.hpp>

namespace Horo::Destruction {
    namespace {
        template <typename T> T RegistryId(const std::uint64_t value) {
            return T::Create(value).Value();
        }

        FractureArtifactContentIdentity RegistryContent(const std::uint64_t revision = 1) {
            const std::array<std::uint8_t, 16> bytes{0x42};
            const Sha256Digest digest{.bytes = {static_cast<std::uint8_t>(revision)}};
            return FractureArtifactContentIdentity::Create(FractureAssetId::Create(Assets::AssetId::FromBytes(bytes)).Value(),
                                                           RegistryId<FractureContentRevision>(revision), digest)
                .Value();
        }

        DestructionRegistryRecord RegistryRecord(const std::uint64_t destructible, const std::uint64_t generation = 1,
                                                 const DestructionStatePhase phase = DestructionStatePhase::Intact,
                                                 const std::uint64_t world = 9) {
            constexpr std::uint32_t features =
                DestructionFeatureBit<DestructionFeature::PreCookedFracture> | DestructionFeatureBit<DestructionFeature::CookedSupport>;
            constexpr std::uint32_t commands = DestructionCommandCapabilityBit<DestructionCommandCapability::Damage> |
                                               DestructionCommandCapabilityBit<DestructionCommandCapability::Impact>;
            const auto limits = GetDestructionTierProfile(DestructionFeatureTier::Baseline).Value().limits;
            return {.state = {.target = {RegistryId<DestructionWorldId>(world), RegistryId<DestructibleId>(destructible),
                                         RegistryId<DestructionGeneration>(generation)},
                              .content = RegistryContent(),
                              .configurationRevision = RegistryId<DestructionConfigurationRevision>(3),
                              .effectiveFeatures = {.bits = features},
                              .revision = RegistryId<DestructionStateRevision>(7),
                              .phase = phase,
                              .health = phase == DestructionStatePhase::Destroyed ? 0.0F : 50.0F},
                    .capabilityRevision = RegistryId<DestructionCapabilityRevision>(5),
                    .commandCapabilities = {.bits = commands},
                    .limits = limits};
        }

        template <typename T> void RegistryErrorIs(const Result<T> &result, const ErrorCodeDescriptor &descriptor) {
            REQUIRE(result.HasError());
            CHECK(result.ErrorValue().code.Value() == descriptor.code.Value());
        }
    }  // namespace

    TEST_CASE("Registry snapshots provide stable lookup query and capability values", "[destruction][registry]") {
        auto registry = DestructionRegistry::Create().Value();
        REQUIRE(registry.Register(RegistryRecord(30, 1, DestructionStatePhase::Damaged)).HasValue());
        REQUIRE(registry.Register(RegistryRecord(10)).HasValue());
        const auto snapshot = registry.Snapshot().Value();

        REQUIRE(snapshot.Records().size() == 2);
        CHECK(snapshot.Records().front().state.target.destructible == RegistryId<DestructibleId>(10));
        CHECK(snapshot.Find(RegistryRecord(30).state.target).Value().state.phase == DestructionStatePhase::Damaged);

        const DestructionQuery query{.world = RegistryId<DestructionWorldId>(9),
                                     .phase = DestructionPhaseFilter::Damaged,
                                     .requiredFeatures = {.bits = DestructionFeatureBit<DestructionFeature::CookedSupport>},
                                     .maximumResults = 4};
        const auto queried = snapshot.Query(query).Value();
        REQUIRE(queried.Records().size() == 1);
        CHECK(queried.Records().front().state.target.destructible == RegistryId<DestructibleId>(30));
        CHECK_FALSE(queried.HasMore());
        CHECK(queried.Revision() == snapshot.Revision());

        const auto capabilities = snapshot.Capabilities(RegistryRecord(10).state.target).Value();
        CHECK(capabilities.RegistryRevision() == snapshot.Revision());
        CHECK(capabilities.StateRevision() == RegistryId<DestructionStateRevision>(7));
        CHECK(capabilities.CapabilityRevision() == RegistryId<DestructionCapabilityRevision>(5));
        CHECK(capabilities.Features().Contains(DestructionFeature::PreCookedFracture));
        CHECK(capabilities.Commands().Contains(DestructionCommandCapability::Impact));
        CHECK(capabilities.Limits().maximumChunksPerDestructible == 64);

        const auto replacement = RegistryRecord(10, 2, DestructionStatePhase::Damaged);
        REQUIRE(registry.Replace(RegistryRecord(10).state.target, replacement).HasValue());
        const auto replaced = registry.Snapshot().Value();
        REQUIRE(replaced.Records().size() == 2);
        CHECK(replaced.Records()[0].state.target == replacement.state.target);
        CHECK(replaced.Records()[1].state.target.destructible == RegistryId<DestructibleId>(30));
    }

    TEST_CASE("Registry rejects malformed duplicate and over-capacity input atomically", "[destruction][registry][boundary]") {
        RegistryErrorIs(DestructionRegistry::Create({.maximumEntries = 0, .maximumQueryResults = 1}), DestructionErrors::RegistryInvalid);
        auto registry = DestructionRegistry::Create({.maximumEntries = 2, .maximumQueryResults = 1}).Value();
        auto malformed = RegistryRecord(10);
        malformed.capabilityRevision = {};
        RegistryErrorIs(registry.Register(malformed), DestructionErrors::RegistryInvalid);
        REQUIRE(registry.Register(RegistryRecord(10)).HasValue());
        RegistryErrorIs(registry.Register(RegistryRecord(10)), DestructionErrors::RegistryDuplicate);
        REQUIRE(registry.Register(RegistryRecord(20)).HasValue());
        RegistryErrorIs(registry.Register(RegistryRecord(30)), DestructionErrors::RegistryCapacityExceeded);

        const auto snapshot = registry.Snapshot().Value();
        RegistryErrorIs(snapshot.Query({.world = RegistryId<DestructionWorldId>(9), .maximumResults = 0}),
                        DestructionErrors::RegistryCapacityExceeded);
        RegistryErrorIs(snapshot.Query({.world = RegistryId<DestructionWorldId>(9), .maximumResults = 2}),
                        DestructionErrors::RegistryCapacityExceeded);
        RegistryErrorIs(snapshot.Query({.world = RegistryId<DestructionWorldId>(9),
                                        .phase = static_cast<DestructionPhaseFilter>(255),
                                        .maximumResults = 1}),
                        DestructionErrors::RegistryInvalid);
    }

    TEST_CASE("Bounded queries report truncation without partial implicit fallback", "[destruction][registry][query]") {
        auto registry = DestructionRegistry::Create({.maximumEntries = 4, .maximumQueryResults = 2}).Value();
        REQUIRE(registry.Register(RegistryRecord(3)).HasValue());
        REQUIRE(registry.Register(RegistryRecord(1)).HasValue());
        REQUIRE(registry.Register(RegistryRecord(2)).HasValue());
        const auto snapshot = registry.Snapshot().Value();
        const auto result = snapshot.Query({.world = RegistryId<DestructionWorldId>(9), .maximumResults = 2}).Value();
        REQUIRE(result.Records().size() == 2);
        CHECK(result.HasMore());
        CHECK(result.Records()[0].state.target.destructible == RegistryId<DestructibleId>(1));
        CHECK(result.Records()[1].state.target.destructible == RegistryId<DestructibleId>(2));

        REQUIRE(registry.Remove(RegistryRecord(2).state.target).HasValue());
        REQUIRE(registry.Register(RegistryRecord(4)).HasValue());
        const auto reordered = registry.Snapshot().Value();
        REQUIRE(reordered.Records().size() == 3);
        CHECK(reordered.Records()[0].state.target.destructible == RegistryId<DestructibleId>(1));
        CHECK(reordered.Records()[1].state.target.destructible == RegistryId<DestructibleId>(3));
        CHECK(reordered.Records()[2].state.target.destructible == RegistryId<DestructibleId>(4));

        const auto unavailable =
            snapshot
                .Query({.world = RegistryId<DestructionWorldId>(9),
                        .requiredFeatures = {.bits = DestructionFeatureBit<DestructionFeature::RuntimeGeometryGeneration>},
                        .maximumResults = 2})
                .Value();
        CHECK(unavailable.Records().empty());
        CHECK_FALSE(unavailable.HasMore());
    }

    TEST_CASE("Replacement fences stale generations and retained snapshots survive shutdown", "[destruction][registry][lifecycle]") {
        auto registry = DestructionRegistry::Create().Value();
        const auto first = RegistryRecord(10);
        REQUIRE(registry.Register(first).HasValue());
        const auto retained = registry.Snapshot().Value();
        const auto replacement = RegistryRecord(10, 2, DestructionStatePhase::Damaged);
        REQUIRE(registry.Replace(first.state.target, replacement).HasValue());
        const auto current = registry.Snapshot().Value();
        RegistryErrorIs(current.Find(first.state.target), DestructionErrors::StaleGeneration);
        RegistryErrorIs(current.Capabilities(first.state.target), DestructionErrors::StaleGeneration);
        CHECK(current.Find(replacement.state.target).HasValue());
        RegistryErrorIs(registry.Replace(replacement.state.target, RegistryRecord(10, 4)), DestructionErrors::StaleGeneration);

        registry.BeginShutdown();
        registry.BeginShutdown();
        CHECK(registry.IsShutdown());
        RegistryErrorIs(registry.Register(RegistryRecord(20)), DestructionErrors::ShutdownInProgress);
        RegistryErrorIs(registry.Remove(replacement.state.target), DestructionErrors::ShutdownInProgress);
        RegistryErrorIs(registry.Snapshot(), DestructionErrors::ShutdownInProgress);
        CHECK(retained.Find(first.state.target).HasValue());
    }

    TEST_CASE("Registry mutation snapshot query and capability paths allocate nothing", "[destruction][registry][allocation]") {
        auto registry = DestructionRegistry::Create().Value();
        const auto before = Tests::AllocationProbe::Count();
        REQUIRE(registry.Register(RegistryRecord(10)).HasValue());
        const auto snapshot = registry.Snapshot();
        REQUIRE(snapshot.HasValue());
        REQUIRE(snapshot.Value().Query({.world = RegistryId<DestructionWorldId>(9), .maximumResults = 1}).HasValue());
        REQUIRE(snapshot.Value().Capabilities(RegistryRecord(10).state.target).HasValue());
        CHECK(Tests::AllocationProbe::Count() == before);
    }
}  // namespace Horo::Destruction
