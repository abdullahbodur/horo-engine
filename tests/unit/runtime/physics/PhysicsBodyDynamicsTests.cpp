#include "Horo/Physics/PhysicsBodyDynamics.h"
#include "Horo/Physics/PhysicsErrors.h"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <limits>
#include <utility>
#include <vector>

namespace Horo::Physics {
    namespace {
        [[nodiscard]] PhysicsWorldId World(const std::uint64_t value = 71) {
            return PhysicsWorldId::Create(value).Value();
        }

        [[nodiscard]] BodyHandle Body(const std::uint32_t index = 3, const std::uint32_t generation = 2) {
            return {World(), {index, generation}};
        }

        [[nodiscard]] PhysicsBodyDynamicsCommand Command(PhysicsBodyDynamicsPayload payload = PhysicsLinearForce{}) {
            return {.simulationTick = 12,
                    .sceneGeneration = 9,
                    .body = Body(),
                    .source = PhysicsCommandSourceId::Create(4).Value(),
                    .sourceSequence = 1,
                    .wake = PhysicsWakePolicy::WakeIfSleeping,
                    .payload = std::move(payload)};
        }

        void RequireValid(const PhysicsBodyDynamicsPayload &payload) {
            REQUIRE(ValidatePhysicsBodyDynamicsCommand(Command(payload), World(), 9, 12, PhysicsMotionType::Dynamic, PhysicsMotionSafety{})
                        .HasValue());
        }

        void RequireError(const PhysicsBodyDynamicsCommand &command, const ErrorCodeDescriptor &descriptor) {
            const auto result =
                ValidatePhysicsBodyDynamicsCommand(command, World(), 9, 12, PhysicsMotionType::Dynamic, PhysicsMotionSafety{});
            REQUIRE(result.HasError());
            REQUIRE(result.ErrorValue().code.Value() == descriptor.code.Value());
        }
    }  // namespace

    TEST_CASE("Physics dynamics accepts every typed SI input at canonical bounds", "[physics][body][dynamics]") {
        RequireValid(PhysicsLinearForce{{MaximumPhysicsForceNewtons, -MaximumPhysicsForceNewtons, 0}, Math::Vec3{1, 2, 3}});
        RequireValid(PhysicsLinearImpulse{{MaximumPhysicsImpulseNewtonSeconds, 0, 0}, std::nullopt});
        RequireValid(PhysicsTorque{{0, MaximumPhysicsTorqueNewtonMeters, 0}});
        RequireValid(PhysicsAngularImpulse{{0, 0, -MaximumPhysicsAngularImpulseNewtonMeterSeconds}});
        RequireValid(PhysicsLinearVelocityControl{{static_cast<float>(MaximumPhysicsLinearSpeed), 0, 0}, PhysicsVelocityControlMode::Set});
        RequireValid(PhysicsAngularVelocityControl{{0, MaximumPhysicsAngularSpeed, 0}, PhysicsVelocityControlMode::Add});
        RequireValid(PhysicsGravityScaleControl{MaximumPhysicsGravityScale});

        auto sleeping = Command(PhysicsLinearForce{{1, 0, 0}, std::nullopt});
        sleeping.wake = PhysicsWakePolicy::PreserveSleeping;
        REQUIRE(ValidatePhysicsBodyDynamicsCommand(sleeping, World(), 9, 12, PhysicsMotionType::Dynamic, PhysicsMotionSafety{}).HasValue());
    }

    TEST_CASE("Physics dynamics rejects incomplete identity and stale fixed-tick affinity", "[physics][body][dynamics]") {
        auto command = Command();
        command.protocolVersion = 2;
        RequireError(command, PhysicsErrors::CommandOrderInvalid);
        command = Command();
        command.sourceSequence = 0;
        RequireError(command, PhysicsErrors::CommandOrderInvalid);
        command = Command();
        command.source = {};
        RequireError(command, PhysicsErrors::CommandOrderInvalid);
        command = Command();
        command.simulationTick = 0;
        RequireError(command, PhysicsErrors::CommandOrderInvalid);
        command = Command();
        command.sceneGeneration = 0;
        RequireError(command, PhysicsErrors::CommandOrderInvalid);
        command = Command();
        command.body = {};
        RequireError(command, PhysicsErrors::HandleMalformed);
        command = Command();
        command.body.world = World(72);
        RequireError(command, PhysicsErrors::HandleWorldMismatch);
        command = Command();
        command.simulationTick = 13;
        RequireError(command, PhysicsErrors::CommandOrderInvalid);
        command = Command();
        command.sceneGeneration = 10;
        RequireError(command, PhysicsErrors::CommandOrderInvalid);
        const auto invalidWorld =
            ValidatePhysicsBodyDynamicsCommand(Command(), {}, 9, 12, PhysicsMotionType::Dynamic, PhysicsMotionSafety{});
        REQUIRE(invalidWorld.HasError());
        REQUIRE(invalidWorld.ErrorValue().code.Value() == PhysicsErrors::WorldInvalid.code.Value());
    }

    TEST_CASE("Physics dynamics requires dynamic authority and known wake policy", "[physics][body][dynamics]") {
        const auto command = Command();
        for (const PhysicsMotionType motion :
             {PhysicsMotionType::Static, PhysicsMotionType::Kinematic, static_cast<PhysicsMotionType>(255)}) {
            const auto result = ValidatePhysicsBodyDynamicsCommand(command, World(), 9, 12, motion, PhysicsMotionSafety{});
            REQUIRE(result.HasError());
            REQUIRE(result.ErrorValue().code.Value() == PhysicsErrors::OperationUnsupported.code.Value());
        }
        auto unknownWake = command;
        unknownWake.wake = static_cast<PhysicsWakePolicy>(255);
        RequireError(unknownWake, PhysicsErrors::OperationUnsupported);
    }

    TEST_CASE("Physics dynamics rejects non-finite and out-of-profile payloads", "[physics][body][dynamics]") {
        const float nan = std::numeric_limits<float>::quiet_NaN();
        const float infinity = std::numeric_limits<float>::infinity();
        RequireError(Command(PhysicsLinearForce{{infinity, 0, 0}, std::nullopt}), PhysicsErrors::DescriptorInvalid);
        RequireError(Command(PhysicsLinearForce{{MaximumPhysicsForceNewtons * 2.0F, 0, 0}, std::nullopt}),
                     PhysicsErrors::DescriptorInvalid);
        RequireError(Command(PhysicsLinearForce{{0, 0, 0}, Math::Vec3{0, nan, 0}}), PhysicsErrors::DescriptorInvalid);
        REQUIRE(ValidatePhysicsBodyDynamicsCommand(Command(PhysicsLinearForce{{}, Math::Vec3{11, 0, 0}}), World(), 9, 12,
                                                   PhysicsMotionType::Dynamic, PhysicsMotionSafety{}, 10)
                    .HasError());
        RequireError(Command(PhysicsLinearImpulse{{MaximumPhysicsImpulseNewtonSeconds * 2.0F, 0, 0}, std::nullopt}),
                     PhysicsErrors::DescriptorInvalid);
        RequireError(Command(PhysicsLinearImpulse{{}, Math::Vec3{0, 0, infinity}}), PhysicsErrors::DescriptorInvalid);
        RequireError(Command(PhysicsTorque{{0, 0, infinity}}), PhysicsErrors::DescriptorInvalid);
        RequireError(Command(PhysicsTorque{{MaximumPhysicsTorqueNewtonMeters * 2.0F, 0, 0}}), PhysicsErrors::DescriptorInvalid);
        RequireError(Command(PhysicsAngularImpulse{{MaximumPhysicsAngularImpulseNewtonMeterSeconds * 2.0F, 0, 0}}),
                     PhysicsErrors::DescriptorInvalid);
        RequireError(Command(PhysicsGravityScaleControl{-0.01F}), PhysicsErrors::DescriptorInvalid);
        RequireError(Command(PhysicsGravityScaleControl{nan}), PhysicsErrors::DescriptorInvalid);
        RequireError(Command(PhysicsGravityScaleControl{MaximumPhysicsGravityScale + 1.0F}), PhysicsErrors::DescriptorInvalid);

        auto unknownVelocity = Command(PhysicsLinearVelocityControl{{}, static_cast<PhysicsVelocityControlMode>(255)});
        RequireError(unknownVelocity, PhysicsErrors::OperationUnsupported);
        auto unknownAngularVelocity = Command(PhysicsAngularVelocityControl{{}, static_cast<PhysicsVelocityControlMode>(255)});
        RequireError(unknownAngularVelocity, PhysicsErrors::OperationUnsupported);
        RequireError(Command(PhysicsLinearVelocityControl{{nan, 0, 0}, PhysicsVelocityControlMode::Add}), PhysicsErrors::DescriptorInvalid);
        auto excessiveVelocity =
            Command(PhysicsAngularVelocityControl{{0, MaximumPhysicsAngularSpeed + 1.0F, 0}, PhysicsVelocityControlMode::Set});
        RequireError(excessiveVelocity, PhysicsErrors::DescriptorInvalid);
    }

    TEST_CASE("Physics dynamics enforces body-specific velocity ceilings", "[physics][body][dynamics]") {
        PhysicsMotionSafety safety;
        safety.maximumLinearSpeed = 10;
        safety.maximumAngularSpeed = 5;
        REQUIRE(ValidatePhysicsBodyDynamicsCommand(Command(PhysicsLinearVelocityControl{{6, 8, 0}}), World(), 9, 12,
                                                   PhysicsMotionType::Dynamic, safety)
                    .HasValue());
        REQUIRE(ValidatePhysicsBodyDynamicsCommand(Command(PhysicsLinearVelocityControl{{10, 10, 0}}), World(), 9, 12,
                                                   PhysicsMotionType::Dynamic, safety)
                    .HasError());
        REQUIRE(ValidatePhysicsBodyDynamicsCommand(Command(PhysicsAngularVelocityControl{{0, 0, 6}}), World(), 9, 12,
                                                   PhysicsMotionType::Dynamic, safety)
                    .HasError());
        safety.maximumLinearSpeed = std::numeric_limits<float>::infinity();
        REQUIRE(ValidatePhysicsBodyDynamicsCommand(Command(), World(), 9, 12, PhysicsMotionType::Dynamic, safety).HasError());
        REQUIRE(
            ValidatePhysicsBodyDynamicsCommand(Command(), World(), 9, 12, PhysicsMotionType::Dynamic, PhysicsMotionSafety{}, 0).HasError());
    }

    TEST_CASE("Physics dynamics ordering ignores producer insertion order", "[physics][body][dynamics][determinism]") {
        auto first = Command();
        first.body = Body(1, 2);
        first.sourceSequence = 2;
        auto second = Command();
        second.body = Body(1, 1);
        auto third = Command();
        third.body = Body(1, 2);
        third.sourceSequence = 1;
        std::vector commands{first, second, third};
        std::ranges::sort(commands, PhysicsBodyDynamicsOrderLess);
        REQUIRE(commands[0].body.slot.generation == 1);
        REQUIRE(commands[1].sourceSequence == 1);
        REQUIRE(commands[2].sourceSequence == 2);
    }
}  // namespace Horo::Physics
