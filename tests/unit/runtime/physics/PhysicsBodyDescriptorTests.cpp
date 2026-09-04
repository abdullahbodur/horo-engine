#include "Horo/Physics/PhysicsBodyDescriptor.h"

#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <limits>

namespace Horo::Physics {
    namespace {
        /** @brief Creates a structurally bound body without a native world or shape registry. */
        PhysicsBodyDescriptor MakeBody() {
            PhysicsBodyDescriptor descriptor;
            descriptor.shape = {PhysicsWorldId::Create(41).Value(), {0, 1}};
            return descriptor;
        }

        /** @brief Checks stable rejection identity without interpreting native codes. */
        void RequireBodyError(const PhysicsBodyDescriptor &descriptor, const ErrorCodeDescriptor &error) {
            const auto result = ValidatePhysicsBodyDescriptor(descriptor, descriptor.shape.world);
            REQUIRE(result.HasError());
            REQUIRE(result.ErrorValue().code.Value() == error.code.Value());
        }

        TEST_CASE("Physics body requests preserve explicit motion and mass alternatives", "[physics][body]") {
            auto descriptor = MakeBody();
            REQUIRE(ValidatePhysicsBodyDescriptor(descriptor, descriptor.shape.world).HasValue());
            descriptor.motion = PhysicsMotionType::Kinematic;
            descriptor.linearVelocity = {1, 2, 3};
            descriptor.angularVelocity = {0, 1, 0};
            REQUIRE(ValidatePhysicsBodyDescriptor(descriptor, descriptor.shape.world).HasValue());
            descriptor.motion = PhysicsMotionType::Dynamic;
            descriptor.mass = PhysicsMass{2};
            REQUIRE(ValidatePhysicsBodyDescriptor(descriptor, descriptor.shape.world).HasValue());
            descriptor.mass = PhysicsDensity{1'200};
            REQUIRE(ValidatePhysicsBodyDescriptor(descriptor, descriptor.shape.world).HasValue());
            REQUIRE(std::get<PhysicsDensity>(descriptor.mass).kilogramsPerCubicMeter == 1'200);
        }

        TEST_CASE("Physics body validation rejects unbound foreign and malformed shape references", "[physics][body]") {
            auto descriptor = MakeBody();
            const auto world = descriptor.shape.world;
            REQUIRE(ValidatePhysicsBodyDescriptor(descriptor, {}).HasError());
            const auto foreign = ValidatePhysicsBodyDescriptor(descriptor, PhysicsWorldId::Create(42).Value());
            REQUIRE(foreign.HasError());
            REQUIRE(foreign.ErrorValue().code.Value() == PhysicsErrors::HandleWorldMismatch.code.Value());
            descriptor.shape.slot.generation = 0;
            RequireBodyError(descriptor, PhysicsErrors::HandleMalformed);
            const auto empty = ValidatePhysicsBodyDescriptor({}, world);
            REQUIRE(empty.HasError());
            REQUIRE(empty.ErrorValue().code.Value() == PhysicsErrors::HandleMalformed.code.Value());
        }

        TEST_CASE("Physics body validation does not repair pose or guess unknown motion", "[physics][body]") {
            auto descriptor = MakeBody();
            descriptor.pose.rotation.w = 2;
            RequireBodyError(descriptor, PhysicsErrors::DescriptorInvalid);
            REQUIRE(descriptor.pose.rotation.w == 2);
            descriptor.pose = {};
            descriptor.motion = static_cast<PhysicsMotionType>(255);
            RequireBodyError(descriptor, PhysicsErrors::OperationUnsupported);
        }

        TEST_CASE("Physics mass policy cannot silently change transform authority", "[physics][body]") {
            auto descriptor = MakeBody();
            descriptor.mass = PhysicsMass{};
            RequireBodyError(descriptor, PhysicsErrors::DescriptorInvalid);
            descriptor.motion = PhysicsMotionType::Kinematic;
            descriptor.mass = PhysicsDensity{};
            RequireBodyError(descriptor, PhysicsErrors::DescriptorInvalid);
            descriptor.motion = PhysicsMotionType::Dynamic;
            descriptor.mass = PhysicsNoMass{};
            RequireBodyError(descriptor, PhysicsErrors::DescriptorInvalid);
        }

        TEST_CASE("Physics dynamic mass and density enforce finite closed profile bounds", "[physics][body]") {
            auto descriptor = MakeBody();
            descriptor.motion = PhysicsMotionType::Dynamic;
            for (const float invalid : {0.0F, -1.0F, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()}) {
                descriptor.mass = PhysicsMass{invalid};
                RequireBodyError(descriptor, PhysicsErrors::DescriptorInvalid);
                descriptor.mass = PhysicsDensity{invalid};
                RequireBodyError(descriptor, PhysicsErrors::DescriptorInvalid);
            }
            descriptor.mass = PhysicsMass{std::nextafter(MinimumPhysicsMassKilograms, 0.0F)};
            RequireBodyError(descriptor, PhysicsErrors::DescriptorInvalid);
            descriptor.mass = PhysicsDensity{std::nextafter(MinimumPhysicsDensity, 0.0F)};
            RequireBodyError(descriptor, PhysicsErrors::DescriptorInvalid);
            descriptor.mass = PhysicsMass{std::nextafter(MaximumPhysicsMassKilograms, std::numeric_limits<float>::infinity())};
            RequireBodyError(descriptor, PhysicsErrors::DescriptorInvalid);
            descriptor.mass = PhysicsDensity{std::nextafter(MaximumPhysicsDensity, std::numeric_limits<float>::infinity())};
            RequireBodyError(descriptor, PhysicsErrors::DescriptorInvalid);
        }

        TEST_CASE("Physics dynamic profile endpoints are preserved exactly", "[physics][body]") {
            auto descriptor = MakeBody();
            descriptor.motion = PhysicsMotionType::Dynamic;
            for (const float mass : {MinimumPhysicsMassKilograms, MaximumPhysicsMassKilograms}) {
                descriptor.mass = PhysicsMass{mass};
                REQUIRE(ValidatePhysicsBodyDescriptor(descriptor, descriptor.shape.world).HasValue());
                REQUIRE(std::get<PhysicsMass>(descriptor.mass).kilograms == mass);
            }
            for (const float density : {MinimumPhysicsDensity, MaximumPhysicsDensity}) {
                descriptor.mass = PhysicsDensity{density};
                REQUIRE(ValidatePhysicsBodyDescriptor(descriptor, descriptor.shape.world).HasValue());
                REQUIRE(std::get<PhysicsDensity>(descriptor.mass).kilogramsPerCubicMeter == density);
            }
        }

        TEST_CASE("Physics static bodies reject either initial velocity", "[physics][body]") {
            auto descriptor = MakeBody();
            descriptor.linearVelocity.x = 1;
            RequireBodyError(descriptor, PhysicsErrors::DescriptorInvalid);
            descriptor.linearVelocity = {};
            descriptor.angularVelocity.y = 1;
            RequireBodyError(descriptor, PhysicsErrors::DescriptorInvalid);
        }

        TEST_CASE("Physics moving bodies validate speed magnitude and reject non-finite velocity", "[physics][body]") {
            auto descriptor = MakeBody();
            descriptor.motion = PhysicsMotionType::Kinematic;
            descriptor.linearVelocity = {300, 400, 0};
            REQUIRE(ValidatePhysicsBodyDescriptor(descriptor, descriptor.shape.world).HasValue());
            descriptor.linearVelocity.z = 1;
            RequireBodyError(descriptor, PhysicsErrors::DescriptorInvalid);
            descriptor.linearVelocity = {std::numeric_limits<float>::max(), 0, 0};
            RequireBodyError(descriptor, PhysicsErrors::DescriptorInvalid);
            descriptor.linearVelocity = {std::numeric_limits<float>::quiet_NaN(), 0, 0};
            RequireBodyError(descriptor, PhysicsErrors::DescriptorInvalid);
            descriptor.linearVelocity = {};
            descriptor.angularVelocity = {0, std::numeric_limits<float>::infinity(), 0};
            RequireBodyError(descriptor, PhysicsErrors::DescriptorInvalid);
        }
    }  // namespace
}  // namespace Horo::Physics
