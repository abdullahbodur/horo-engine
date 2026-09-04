#include "Horo/Physics/PhysicsConstraintDescriptor.h"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <limits>
#include <type_traits>

namespace Horo::Physics {
    namespace {
        /** @brief Creates a valid structural request without registering or allocating a world/body. */
        PhysicsConstraintDescriptor MakeConstraint() {
            PhysicsConstraintDescriptor descriptor;
            descriptor.first.body = {PhysicsWorldId::Create(31).Value(), {0, 1}};
            return descriptor;
        }

        /** @brief Checks a structural rejection retains the expected stable error identity. */
        void RequireConstraintError(const PhysicsConstraintDescriptor &descriptor, const PhysicsWorldId world,
                                    const ErrorCodeDescriptor &error) {
            const auto result = ValidatePhysicsConstraintDescriptor(descriptor, world);
            REQUIRE(result.HasError());
            REQUIRE(result.ErrorValue().code.Value() == error.code.Value());
        }

        TEST_CASE("Physics constraints admit explicit body and world anchor alternatives", "[physics][constraint]") {
            auto descriptor = MakeConstraint();
            const auto world = descriptor.first.body.world;
            REQUIRE(ValidatePhysicsConstraintDescriptor(descriptor, world).HasValue());
            descriptor.first.localFrame.translation = {1, 2, 3};
            descriptor.second = PhysicsWorldAnchor{{{4, 5, 6}, {0, 0, 0, -1}}};
            REQUIRE(ValidatePhysicsConstraintDescriptor(descriptor, world).HasValue());
            REQUIRE(std::get<PhysicsWorldAnchor>(descriptor.second).frame.rotation.w == -1);
            descriptor.second = PhysicsBodyAnchor{{world, {1, 1}}, {}};
            REQUIRE(ValidatePhysicsConstraintDescriptor(descriptor, world).HasValue());
            descriptor.parameters = PhysicsDistanceConstraint{0.25F, 2.0F};
            REQUIRE(ValidatePhysicsConstraintDescriptor(descriptor, world).HasValue());
            static_assert(std::is_nothrow_copy_assignable_v<PhysicsConstraintDescriptor>);
            static_assert(std::is_nothrow_move_assignable_v<PhysicsConstraintDescriptor>);
        }

        TEST_CASE("Physics constraints reject unbound foreign and self endpoints", "[physics][constraint]") {
            auto descriptor = MakeConstraint();
            const auto world = descriptor.first.body.world;
            const auto foreign = PhysicsWorldId::Create(32).Value();
            RequireConstraintError(descriptor, {}, PhysicsErrors::WorldInvalid);
            RequireConstraintError({}, world, PhysicsErrors::HandleMalformed);
            RequireConstraintError(descriptor, foreign, PhysicsErrors::HandleWorldMismatch);
            descriptor.second = PhysicsBodyAnchor{};
            RequireConstraintError(descriptor, world, PhysicsErrors::HandleMalformed);
            descriptor.second = PhysicsBodyAnchor{{foreign, {1, 1}}, {}};
            RequireConstraintError(descriptor, world, PhysicsErrors::HandleWorldMismatch);
            descriptor.second = descriptor.first;
            RequireConstraintError(descriptor, world, PhysicsErrors::DescriptorInvalid);
        }

        TEST_CASE("Physics constraints preserve distinct generations for later registry resolution", "[physics][constraint]") {
            auto descriptor = MakeConstraint();
            const auto world = descriptor.first.body.world;
            descriptor.second = PhysicsBodyAnchor{{world, {0, 2}}, {}};
            REQUIRE(ValidatePhysicsConstraintDescriptor(descriptor, world).HasValue());
            // Structural validation cannot determine which same-index generation is resident.
            REQUIRE(std::get<PhysicsBodyAnchor>(descriptor.second).body.slot.generation == 2);
        }

        TEST_CASE("Physics constraints validate every endpoint frame without repairing rotations", "[physics][constraint]") {
            const PhysicsPose invalid{{}, {0, 0, 0, 2}};
            auto descriptor = MakeConstraint();
            const auto world = descriptor.first.body.world;
            descriptor.first.localFrame = invalid;
            RequireConstraintError(descriptor, world, PhysicsErrors::DescriptorInvalid);
            REQUIRE(descriptor.first.localFrame == invalid);
            descriptor.first.localFrame = {};
            descriptor.second = PhysicsWorldAnchor{invalid};
            RequireConstraintError(descriptor, world, PhysicsErrors::DescriptorInvalid);
            descriptor.second = PhysicsBodyAnchor{{world, {1, 1}}, invalid};
            RequireConstraintError(descriptor, world, PhysicsErrors::DescriptorInvalid);
        }

        TEST_CASE("Physics distance intervals reject non-finite negative and inverted bounds", "[physics][constraint]") {
            const auto nan = std::numeric_limits<float>::quiet_NaN();
            const auto infinity = std::numeric_limits<float>::infinity();
            const std::array<PhysicsDistanceConstraint, 6> invalid{
                {{nan, 1}, {0, nan}, {infinity, infinity}, {0, infinity}, {-1, 1}, {2, 1}}};
            auto descriptor = MakeConstraint();
            for (const auto distance : invalid) {
                descriptor.parameters = distance;
                RequireConstraintError(descriptor, descriptor.first.body.world, PhysicsErrors::DescriptorInvalid);
            }
        }

        TEST_CASE("Physics distance intervals preserve zero equal and finite extreme representation", "[physics][constraint]") {
            auto descriptor = MakeConstraint();
            for (const float distance : {0.0F, 1.0F, std::numeric_limits<float>::max()}) {
                descriptor.parameters = PhysicsDistanceConstraint{distance, distance};
                REQUIRE(ValidatePhysicsConstraintDescriptor(descriptor, descriptor.first.body.world).HasValue());
                REQUIRE(std::get<PhysicsDistanceConstraint>(descriptor.parameters).maximumMeters == distance);
            }
        }
    }  // namespace
}  // namespace Horo::Physics
