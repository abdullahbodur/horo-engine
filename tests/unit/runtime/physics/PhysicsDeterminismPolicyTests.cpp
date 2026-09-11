#include "Horo/Physics/PhysicsDeterminismPolicy.h"
#include "Horo/Physics/PhysicsErrors.h"

#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>

namespace Horo::Physics {
    namespace {
        [[nodiscard]] PhysicsCommandOrderKey Key(const std::uint64_t tick, const PhysicsCommandTargetKind targetKind,
                                                 const std::uint64_t target, const PhysicsStructuralCommandKind commandKind,
                                                 const std::uint64_t source, const std::uint64_t sequence) {
            return {.simulationTick = tick,
                    .worldGeneration = 7,
                    .sceneGeneration = 11,
                    .targetKind = targetKind,
                    .targetIdentity = target,
                    .commandKind = commandKind,
                    .source = PhysicsCommandSourceId::Create(source).Value(),
                    .sourceSequence = sequence};
        }

        [[nodiscard]] PhysicsSeedPolicy Policy() {
            return {.revision = 3,
                    .algorithm = PhysicsSeedAlgorithm::SplitMix64V1,
                    .algorithmVersion = PhysicsSeedAlgorithmSplitMix64VersionV1,
                    .rootSeed = 0x0123456789abcdefULL,
                    .sessionSeed = 0xfedcba9876543210ULL,
                    .worldGeneration = 7};
        }

        void RequireSeedError(const PhysicsSeedPolicy &policy, const PhysicsSeedConsumption &consumption) {
            const auto result = DerivePhysicsSeed(policy, consumption);
            REQUIRE(result.HasError());
            REQUIRE(result.ErrorValue().code.Value() == PhysicsErrors::SeedPolicyInvalid.code.Value());
        }
    }  // namespace

    TEST_CASE("Physics command keys validate every canonical identity and version", "[physics][determinism][ordering]") {
        const PhysicsCommandOrderKey valid = Key(1, PhysicsCommandTargetKind::Body, 19, PhysicsStructuralCommandKind::Change, 4, 1);
        REQUIRE(ValidatePhysicsCommandOrderKey(valid).HasValue());
        REQUIRE_FALSE(PhysicsCommandSourceId{}.IsValid());
        REQUIRE(PhysicsCommandSourceId::Create(0).ErrorValue().code.Value() == PhysicsErrors::CommandOrderInvalid.code.Value());

        for (std::uint32_t invalidVersion : {0U, 2U}) {
            auto candidate = valid;
            candidate.protocolVersion = invalidVersion;
            REQUIRE(ValidatePhysicsCommandOrderKey(candidate).HasError());
        }
        auto invalid = valid;
        invalid.simulationTick = 0;
        REQUIRE(ValidatePhysicsCommandOrderKey(invalid).HasError());
        invalid = valid;
        invalid.worldGeneration = 0;
        REQUIRE(ValidatePhysicsCommandOrderKey(invalid).HasError());
        invalid = valid;
        invalid.sceneGeneration = 0;
        REQUIRE(ValidatePhysicsCommandOrderKey(invalid).HasError());
        invalid = valid;
        invalid.targetIdentity = 0;
        REQUIRE(ValidatePhysicsCommandOrderKey(invalid).HasError());
        invalid = valid;
        invalid.source = {};
        REQUIRE(ValidatePhysicsCommandOrderKey(invalid).HasError());
        invalid = valid;
        invalid.sourceSequence = 0;
        REQUIRE(ValidatePhysicsCommandOrderKey(invalid).HasError());
        invalid = valid;
        invalid.targetKind = static_cast<PhysicsCommandTargetKind>(255);
        REQUIRE(ValidatePhysicsCommandOrderKey(invalid).HasError());
        invalid = valid;
        invalid.commandKind = static_cast<PhysicsStructuralCommandKind>(255);
        REQUIRE(ValidatePhysicsCommandOrderKey(invalid).HasError());
    }

    TEST_CASE("Physics command order is canonical across insertion permutations", "[physics][determinism][ordering]") {
        const std::array expected{
            Key(1, PhysicsCommandTargetKind::World, 9, PhysicsStructuralCommandKind::Change, 2, 1),
            Key(1, PhysicsCommandTargetKind::Body, 1, PhysicsStructuralCommandKind::Destroy, 1, 2),
            Key(1, PhysicsCommandTargetKind::Body, 2, PhysicsStructuralCommandKind::Create, 1, 1),
            Key(1, PhysicsCommandTargetKind::Constraint, 1, PhysicsStructuralCommandKind::Create, 3, 1),
        };
        std::array permuted{expected[2], expected[0], expected[3], expected[1]};
        std::ranges::sort(permuted, PhysicsCommandOrderLess);
        REQUIRE(permuted == expected);
        std::ranges::reverse(permuted);
        std::ranges::sort(permuted, PhysicsCommandOrderLess);
        REQUIRE(permuted == expected);
    }

    TEST_CASE("Physics command order uses exact command-kind and source tie-breaks for one target", "[physics][determinism][ordering]") {
        const std::array expected{
            Key(3, PhysicsCommandTargetKind::Body, 42, PhysicsStructuralCommandKind::Create, 9, 1),
            Key(3, PhysicsCommandTargetKind::Body, 42, PhysicsStructuralCommandKind::Change, 1, 2),
            Key(3, PhysicsCommandTargetKind::Body, 42, PhysicsStructuralCommandKind::Change, 2, 1),
            Key(3, PhysicsCommandTargetKind::Body, 42, PhysicsStructuralCommandKind::Destroy, 1, 1),
        };
        std::array permuted{expected[3], expected[2], expected[0], expected[1]};
        std::ranges::sort(permuted, PhysicsCommandOrderLess);
        REQUIRE(permuted == expected);
    }

    TEST_CASE("Physics seed derivation is exact repeatable and domain separated", "[physics][determinism][seed]") {
        const PhysicsSeedPolicy policy = Policy();
        const PhysicsSeedConsumption consumption{.stream = PhysicsRandomStreamId::Create(5).Value(), .ownerIdentity = 17, .sequence = 1};
        const std::uint64_t baseline = DerivePhysicsSeed(policy, consumption).Value();
        REQUIRE(baseline == 0x4891196aed21440fULL);
        REQUIRE(DerivePhysicsSeed(policy, consumption).Value() == baseline);

        auto changedPolicy = policy;
        changedPolicy.rootSeed++;
        REQUIRE(DerivePhysicsSeed(changedPolicy, consumption).Value() != baseline);
        changedPolicy = policy;
        changedPolicy.sessionSeed++;
        REQUIRE(DerivePhysicsSeed(changedPolicy, consumption).Value() != baseline);
        changedPolicy = policy;
        changedPolicy.worldGeneration++;
        REQUIRE(DerivePhysicsSeed(changedPolicy, consumption).Value() != baseline);
        changedPolicy = policy;
        changedPolicy.revision++;
        REQUIRE(DerivePhysicsSeed(changedPolicy, consumption).Value() != baseline);

        auto changedConsumption = consumption;
        changedConsumption.stream = PhysicsRandomStreamId::Create(6).Value();
        REQUIRE(DerivePhysicsSeed(policy, changedConsumption).Value() != baseline);
        changedConsumption = consumption;
        changedConsumption.ownerIdentity++;
        REQUIRE(DerivePhysicsSeed(policy, changedConsumption).Value() != baseline);
        changedConsumption = consumption;
        changedConsumption.sequence++;
        REQUIRE(DerivePhysicsSeed(policy, changedConsumption).Value() != baseline);
    }

    TEST_CASE("Physics seed policy rejects unsupported or ownerless consumption", "[physics][determinism][seed]") {
        const PhysicsSeedConsumption valid{.stream = PhysicsRandomStreamId::Create(2).Value(), .ownerIdentity = 3, .sequence = 1};
        auto invalid = Policy();
        invalid.contractVersion = 0;
        RequireSeedError(invalid, valid);
        invalid = Policy();
        invalid.revision = 0;
        RequireSeedError(invalid, valid);
        invalid = Policy();
        invalid.algorithm = static_cast<PhysicsSeedAlgorithm>(255);
        RequireSeedError(invalid, valid);
        invalid = Policy();
        invalid.algorithmVersion = 0;
        RequireSeedError(invalid, valid);
        invalid = Policy();
        invalid.worldGeneration = 0;
        RequireSeedError(invalid, valid);

        auto consumption = valid;
        consumption.stream = {};
        RequireSeedError(Policy(), consumption);
        consumption = valid;
        consumption.ownerIdentity = 0;
        RequireSeedError(Policy(), consumption);
        consumption = valid;
        consumption.sequence = 0;
        RequireSeedError(Policy(), consumption);
        REQUIRE(PhysicsRandomStreamId::Create(0).ErrorValue().code.Value() == PhysicsErrors::SeedPolicyInvalid.code.Value());
    }

    TEST_CASE("Physics policy encoding uses one fixed field order", "[physics][determinism][fingerprint]") {
        const auto encoded = EncodePhysicsDeterminismPolicy(Policy()).Value();
        constexpr PhysicsDeterminismPolicyEncoding expected{
            0x01, 0x00, 0x00, 0x00,  // encoding version
            0x01, 0x00, 0x00, 0x00,  // command ordering protocol
            0x01, 0x00, 0x00, 0x00,  // seed policy contract
            0x00, 0x00, 0x00, 0x00,  // SplitMix64V1 algorithm identity
            0x01, 0x00, 0x00, 0x00,  // algorithm version
            0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xef, 0xcd, 0xab, 0x89, 0x67, 0x45, 0x23, 0x01,
            0x10, 0x32, 0x54, 0x76, 0x98, 0xba, 0xdc, 0xfe, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        };
        REQUIRE(encoded == expected);
        REQUIRE(EncodePhysicsDeterminismPolicy({}).ErrorValue().code.Value() == PhysicsErrors::SeedPolicyInvalid.code.Value());
    }
}  // namespace Horo::Physics
