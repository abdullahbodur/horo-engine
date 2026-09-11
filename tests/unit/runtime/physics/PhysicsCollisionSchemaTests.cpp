#include "Horo/Physics/PhysicsCollisionSchema.h"
#include "Horo/Physics/PhysicsErrors.h"

#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>

namespace Horo::Physics {
    namespace {
        template <typename Id> Id MakeId(const std::uint8_t value) {
            std::array<std::uint8_t, 16> bytes{};
            bytes.back() = value;
            return Id::FromBytes(bytes);
        }

        ProjectCollisionSchema ValidSchema() {
            const auto layerA = MakeId<CollisionLayerId>(1);
            const auto layerB = MakeId<CollisionLayerId>(2);
            const auto channel = MakeId<PhysicsQueryChannelId>(3);
            const auto profile = MakeId<CollisionProfileId>(4);
            return {.defaultProfile = profile,
                    .layers = {{layerB}, {layerA}},
                    .pairs = {{layerB, layerB, SimulationPairResponse::Block},
                              {layerB, layerA, SimulationPairResponse::Overlap},
                              {layerA, layerA, SimulationPairResponse::Block}},
                    .queryChannels = {{channel}},
                    .profiles = {{profile, layerA, true, true, true, {{channel, CollisionQueryResponse::Block}}}}};
        }

        void RequireDescriptorError(const Result<NormalizedCollisionSchema> &result) {
            REQUIRE(result.HasError());
            REQUIRE(result.ErrorValue().code.Value() == PhysicsErrors::DescriptorInvalid.code.Value());
        }

        void RequireCapacityError(const Result<NormalizedCollisionSchema> &result) {
            REQUIRE(result.HasError());
            REQUIRE(result.ErrorValue().code.Value() == PhysicsErrors::CapacityExceeded.code.Value());
        }
    }  // namespace

    TEST_CASE("Collision schemas normalize ordering and resolve immutable policy", "[physics][collision-schema]") {
        auto first = NormalizedCollisionSchema::Create(ValidSchema());
        REQUIRE(first.HasValue());
        REQUIRE(first.Value().Layers().front().id == MakeId<CollisionLayerId>(1));
        REQUIRE(first.Value().Pairs().size() == 3);
        REQUIRE(first.Value().ResolvePair(MakeId<CollisionLayerId>(1), MakeId<CollisionLayerId>(2)).Value() ==
                SimulationPairResponse::Overlap);
        REQUIRE(first.Value().ResolveQuery(MakeId<CollisionProfileId>(4), MakeId<PhysicsQueryChannelId>(3)).Value() ==
                CollisionQueryResponse::Block);

        auto reordered = ValidSchema();
        std::ranges::reverse(reordered.pairs);
        std::ranges::reverse(reordered.layers);
        auto second = NormalizedCollisionSchema::Create(reordered);
        REQUIRE(second.HasValue());
        REQUIRE(second.Value().SemanticFingerprint() == first.Value().SemanticFingerprint());
    }

    TEST_CASE("Collision schema activation rejects incomplete duplicate and missing references", "[physics][collision-schema]") {
        auto incomplete = ValidSchema();
        incomplete.pairs.pop_back();
        RequireDescriptorError(NormalizedCollisionSchema::Create(incomplete));

        auto duplicate = ValidSchema();
        duplicate.pairs.push_back(duplicate.pairs.front());
        RequireDescriptorError(NormalizedCollisionSchema::Create(duplicate));

        auto missingLayer = ValidSchema();
        missingLayer.profiles.front().layer = MakeId<CollisionLayerId>(9);
        RequireDescriptorError(NormalizedCollisionSchema::Create(missingLayer));

        auto incompleteProfile = ValidSchema();
        incompleteProfile.profiles.front().queryResponses.clear();
        RequireDescriptorError(NormalizedCollisionSchema::Create(incompleteProfile));
    }

    TEST_CASE("Collision schema activation rejects cross-type IDs and illegal participation", "[physics][collision-schema]") {
        auto crossType = ValidSchema();
        crossType.queryChannels.front().id = MakeId<PhysicsQueryChannelId>(1);
        RequireDescriptorError(NormalizedCollisionSchema::Create(crossType));

        auto overlap = ValidSchema();
        overlap.layers.front().admitsOverlap = false;
        RequireDescriptorError(NormalizedCollisionSchema::Create(overlap));

        auto lifecycle = ValidSchema();
        lifecycle.profiles.front().simulationEnabled = false;
        RequireDescriptorError(NormalizedCollisionSchema::Create(lifecycle));
    }

    TEST_CASE("Collision schema activation rejects unsupported metadata and excessive tables", "[physics][collision-schema]") {
        auto version = ValidSchema();
        version.schemaVersion = 2;
        RequireDescriptorError(NormalizedCollisionSchema::Create(version));

        auto features = ValidSchema();
        features.requiredFeatureBits = 1;
        RequireDescriptorError(NormalizedCollisionSchema::Create(features));

        auto pairs = ValidSchema();
        pairs.pairs.resize(2'081);
        RequireCapacityError(NormalizedCollisionSchema::Create(pairs));

        auto responses = ValidSchema();
        responses.profiles.front().queryResponses.resize(65);
        RequireCapacityError(NormalizedCollisionSchema::Create(responses));
    }
}  // namespace Horo::Physics
