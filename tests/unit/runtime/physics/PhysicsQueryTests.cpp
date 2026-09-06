#include "Horo/Physics/PhysicsErrors.h"
#include "Horo/Physics/PhysicsQuery.h"

#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <limits>
#include <type_traits>

namespace Horo::Physics {
    namespace {
        constexpr std::array<std::uint8_t, 16> LayerBytes{1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
        constexpr std::array<std::uint8_t, 16> ProfileBytes{2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2};
        constexpr std::array<std::uint8_t, 16> ChannelBytes{3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3};

        [[nodiscard]] PhysicsWorldId World() {
            return PhysicsWorldId::Create(17).Value();
        }

        [[nodiscard]] BodyHandle Body(const std::uint32_t index = 1, const std::uint32_t generation = 1) {
            return {World(), {index, generation}};
        }

        [[nodiscard]] ShapeHandle Shape(const std::uint32_t index = 2, const std::uint32_t generation = 1) {
            return {World(), {index, generation}};
        }

        [[nodiscard]] PhysicsQueryDescriptor RayDescriptor() {
            PhysicsQueryDescriptor descriptor;
            descriptor.world = World();
            descriptor.sceneGeneration = 9;
            descriptor.geometry = PhysicsRayQuery{{1, 2, 3}, {0, 0, -1}, 100};
            descriptor.filter.channel = PhysicsQueryChannelId::FromBytes(ChannelBytes);
            return descriptor;
        }

        [[nodiscard]] PhysicsQueryHit Hit(const float distance = 1) {
            return {.body = Body(),
                    .shape = Shape(),
                    .subshape = PhysicsShapeSubresourceId::FromValue(4),
                    .material = std::nullopt,
                    .layer = CollisionLayerId::FromBytes(LayerBytes),
                    .profile = CollisionProfileId::FromBytes(ProfileBytes),
                    .channel = PhysicsQueryChannelId::FromBytes(ChannelBytes),
                    .filterSchemaGeneration = 7,
                    .response = PhysicsQueryResponse::Block,
                    .position = {1, 0, 0},
                    .normal = Math::Vec3{0, 1, 0},
                    .distanceMeters = distance};
        }

        void RequireCode(const Result<void> &result, const ErrorCodeDescriptor &expected) {
            REQUIRE(result.HasError());
            REQUIRE(result.ErrorValue().code.Value() == expected.code.Value());
        }
    }  // namespace

    TEST_CASE("Physics filter IDs preserve canonical UUIDs and remain non-interchangeable", "[physics][query][identity]") {
        const auto channel = PhysicsQueryChannelId::Parse("03000000-0000-0000-0000-000000000003");
        REQUIRE(channel.HasValue());
        REQUIRE(channel.Value().Bytes() == ChannelBytes);
        REQUIRE(channel.Value().ToString() == "03000000-0000-0000-0000-000000000003");
        REQUIRE_FALSE(PhysicsQueryChannelId{}.IsValid());
        REQUIRE_FALSE(PhysicsQueryChannelId::Parse("03000000-0000-0000-0000-00000000000A").HasValue());
        REQUIRE_FALSE(PhysicsQueryChannelId::Parse("03000000-0000-0000-0000-0000000000-3").HasValue());
        REQUIRE_FALSE(PhysicsQueryChannelId::Parse("00000000-0000-0000-0000-000000000000").HasValue());
        static_assert(!std::is_same_v<CollisionLayerId, CollisionProfileId>);
        static_assert(!std::is_convertible_v<CollisionLayerId, PhysicsQueryChannelId>);
    }

    TEST_CASE("Physics query descriptors validate all backend-neutral geometry alternatives", "[physics][query][descriptor]") {
        auto descriptor = RayDescriptor();
        REQUIRE(ValidatePhysicsQueryDescriptor(descriptor, World(), 9).HasValue());

        descriptor.geometry = PhysicsSweepQuery{Shape(), {{1, 2, 3}, {0, 0, 0, 1}}, {1, 0, 0}, 5};
        descriptor.collection = PhysicsQueryCollection::All;
        descriptor.maximumHitCount = 16;
        REQUIRE(ValidatePhysicsQueryDescriptor(descriptor, World(), 9).HasValue());

        descriptor.geometry = PhysicsOverlapQuery{Shape(), {{1, 2, 3}, {0, 0, 0, 1}}};
        descriptor.collection = PhysicsQueryCollection::ThroughFirstBlock;
        REQUIRE(ValidatePhysicsQueryDescriptor(descriptor, World(), 9).HasValue());

        descriptor.geometry = PhysicsPointQuery{{4, 5, 6}};
        descriptor.filter.requiredLayer = CollisionLayerId::FromBytes(LayerBytes);
        descriptor.filter.requiredProfile = CollisionProfileId::FromBytes(ProfileBytes);
        descriptor.filter.excludedBody = Body();
        REQUIRE(ValidatePhysicsQueryDescriptor(descriptor, World(), 9).HasValue());
    }

    TEST_CASE("Physics query descriptor validation rejects stale malformed and unbounded requests", "[physics][query][descriptor]") {
        auto descriptor = RayDescriptor();
        RequireCode(ValidatePhysicsQueryDescriptor(descriptor, PhysicsWorldId::Create(18).Value(), 9), PhysicsErrors::HandleWorldMismatch);
        RequireCode(ValidatePhysicsQueryDescriptor(descriptor, World(), 10), PhysicsErrors::QuerySnapshotStale);

        descriptor.filter.channel = {};
        RequireCode(ValidatePhysicsQueryDescriptor(descriptor, World(), 9), PhysicsErrors::DescriptorInvalid);
        descriptor.filter.channel = PhysicsQueryChannelId::FromBytes(ChannelBytes);
        descriptor.maximumHitCount = MaximumPhysicsQueryHits + 1;
        RequireCode(ValidatePhysicsQueryDescriptor(descriptor, World(), 9), PhysicsErrors::CapacityExceeded);
        descriptor.maximumHitCount = 2;
        RequireCode(ValidatePhysicsQueryDescriptor(descriptor, World(), 9), PhysicsErrors::DescriptorInvalid);
        descriptor.maximumHitCount = 1;
        descriptor.ordering = static_cast<PhysicsQueryOrdering>(255);
        RequireCode(ValidatePhysicsQueryDescriptor(descriptor, World(), 9), PhysicsErrors::OperationUnsupported);

        descriptor.ordering = PhysicsQueryOrdering::ClosestFirst;
        descriptor.geometry = PhysicsRayQuery{{}, {0, 0, 2}, 1};
        RequireCode(ValidatePhysicsQueryDescriptor(descriptor, World(), 9), PhysicsErrors::DescriptorInvalid);
        descriptor.geometry = PhysicsPointQuery{{std::numeric_limits<float>::quiet_NaN(), 0, 0}};
        RequireCode(ValidatePhysicsQueryDescriptor(descriptor, World(), 9), PhysicsErrors::DescriptorInvalid);
        descriptor.geometry = PhysicsOverlapQuery{{PhysicsWorldId::Create(18).Value(), {2, 1}}, {}};
        RequireCode(ValidatePhysicsQueryDescriptor(descriptor, World(), 9), PhysicsErrors::HandleWorldMismatch);
    }

    TEST_CASE("Physics query hits validate stable identity material and subshape evidence", "[physics][query][hit]") {
        const auto descriptor = RayDescriptor();
        auto hit = Hit();
        REQUIRE(ValidatePhysicsQueryHit(hit, descriptor).HasValue());

        hit.material = PhysicsQueryMaterial{Assets::AssetId::Parse("b972cfcb-5cce-4c32-b3b5-a9c238057b83").Value(), 3,
                                            PhysicsMaterialSlotId::FromValue(8)};
        REQUIRE(ValidatePhysicsQueryHit(hit, descriptor).HasValue());
        hit.material->assetGeneration = 0;
        RequireCode(ValidatePhysicsQueryHit(hit, descriptor), PhysicsErrors::DescriptorInvalid);
        hit.material.reset();
        hit.distanceMeters = 101;
        RequireCode(ValidatePhysicsQueryHit(hit, descriptor), PhysicsErrors::DescriptorInvalid);
        hit.distanceMeters = 1;
        hit.subshape = PhysicsShapeSubresourceId{};
        RequireCode(ValidatePhysicsQueryHit(hit, descriptor), PhysicsErrors::DescriptorInvalid);
        hit.subshape.reset();
        hit.filterSchemaGeneration = 0;
        RequireCode(ValidatePhysicsQueryHit(hit, descriptor), PhysicsErrors::QuerySnapshotStale);
        hit.filterSchemaGeneration = 7;
        hit.body.world = PhysicsWorldId::Create(18).Value();
        RequireCode(ValidatePhysicsQueryHit(hit, descriptor), PhysicsErrors::HandleWorldMismatch);
    }

    TEST_CASE("Physics query hit ordering ignores native traversal and uses stable public evidence", "[physics][query][ordering]") {
        std::array hits{Hit(4), Hit(1), Hit(1), Hit(1)};
        hits[2].response = PhysicsQueryResponse::Overlap;
        hits[3].body = Body(0, 1);
        std::ranges::sort(hits, PhysicsQueryHitLess);
        REQUIRE(hits[0].distanceMeters == 1);
        REQUIRE(hits[0].response == PhysicsQueryResponse::Block);
        REQUIRE(hits[0].body.slot.index == 0);
        REQUIRE(hits[1].response == PhysicsQueryResponse::Block);
        REQUIRE(hits[2].response == PhysicsQueryResponse::Overlap);
        REQUIRE(hits[3].distanceMeters == 4);
        REQUIRE_FALSE(PhysicsQueryHitLess(hits[1], hits[1]));
    }

    TEST_CASE("Physics query result metadata cannot exceed the admitted bound or omit schema evidence", "[physics][query][result]") {
        auto descriptor = RayDescriptor();
        REQUIRE(ValidatePhysicsQueryResult({.hitCount = 1, .filterSchemaGeneration = 7}, descriptor).HasValue());
        RequireCode(ValidatePhysicsQueryResult({.hitCount = 2, .filterSchemaGeneration = 7}, descriptor), PhysicsErrors::CapacityExceeded);
        RequireCode(ValidatePhysicsQueryResult({.hitCount = 1}, descriptor), PhysicsErrors::QuerySnapshotStale);
    }
}  // namespace Horo::Physics
