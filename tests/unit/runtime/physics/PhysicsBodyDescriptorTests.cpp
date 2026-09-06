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

        /** @brief Creates portable static intent without runtime identity or world pose. */
        PhysicsAuthoredBodyDescriptor MakeAuthoredBody() {
            return {};
        }

        /** @brief Creates one well-formed query snapshot for representation validation. */
        PhysicsBodyState MakeBodyState() {
            PhysicsBodyState state;
            state.body = {PhysicsWorldId::Create(41).Value(), {0, 1}};
            return state;
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

        TEST_CASE("Physics authored body intent validates each motion and typed mass alternative", "[physics][body]") {
            auto authored = MakeAuthoredBody();
            REQUIRE(authored.initialLinearVelocity == Math::Vec3{});
            REQUIRE(authored.initialAngularVelocity == Math::Vec3{});
            REQUIRE(ValidatePhysicsAuthoredBodyDescriptor(authored).HasValue());
            authored.motion = PhysicsMotionType::Kinematic;
            authored.initialLinearVelocity = {1, 2, 3};
            authored.initialAngularVelocity = {0, 1, 0};
            REQUIRE(ValidatePhysicsAuthoredBodyDescriptor(authored).HasValue());
            authored.motion = PhysicsMotionType::Dynamic;
            authored.mass = PhysicsMass{2};
            REQUIRE(ValidatePhysicsAuthoredBodyDescriptor(authored).HasValue());
            authored.mass = PhysicsDensity{1'200};
            REQUIRE(ValidatePhysicsAuthoredBodyDescriptor(authored).HasValue());
        }

        TEST_CASE("Physics authored body intent rejects inconsistent policy without runtime identity", "[physics][body]") {
            auto authored = MakeAuthoredBody();
            authored.mass = PhysicsMass{};
            REQUIRE(ValidatePhysicsAuthoredBodyDescriptor(authored).HasError());
            authored.motion = PhysicsMotionType::Dynamic;
            authored.mass = PhysicsNoMass{};
            REQUIRE(ValidatePhysicsAuthoredBodyDescriptor(authored).HasError());
            authored.mass = PhysicsMass{std::numeric_limits<float>::quiet_NaN()};
            REQUIRE(ValidatePhysicsAuthoredBodyDescriptor(authored).HasError());
            authored.mass = PhysicsMass{};
            authored.initialLinearVelocity.x = std::numeric_limits<float>::infinity();
            REQUIRE(ValidatePhysicsAuthoredBodyDescriptor(authored).HasError());
            authored.initialLinearVelocity = {};
            authored.motion = static_cast<PhysicsMotionType>(255);
            const auto unsupported = ValidatePhysicsAuthoredBodyDescriptor(authored);
            REQUIRE(unsupported.HasError());
            REQUIRE(unsupported.ErrorValue().code.Value() == PhysicsErrors::OperationUnsupported.code.Value());
        }

        TEST_CASE("Physics body resolution preserves authored intent and leaves every input unchanged", "[physics][body]") {
            PhysicsAuthoredBodyDescriptor authored{PhysicsMotionType::Dynamic, PhysicsDensity{1'200}, {3, 4, 0}, {0, 2, 0}};
            const ShapeHandle shape{PhysicsWorldId::Create(41).Value(), {7, 3}};
            const PhysicsPose pose{{1, 2, 3}, Math::Quaternion::FromAxisAngle({0, 1, 0}, Math::Pi / 2)};
            const auto result = ResolvePhysicsBodyDescriptor(authored, shape, pose, shape.world);
            REQUIRE(result.HasValue());
            REQUIRE(result.Value().shape == shape);
            REQUIRE(result.Value().pose == pose);
            REQUIRE(result.Value().motion == PhysicsMotionType::Dynamic);
            REQUIRE(std::get<PhysicsDensity>(result.Value().mass).kilogramsPerCubicMeter == 1'200);
            REQUIRE(result.Value().linearVelocity == Math::Vec3{3, 4, 0});
            REQUIRE(result.Value().angularVelocity == Math::Vec3{0, 2, 0});
            REQUIRE(std::get<PhysicsDensity>(authored.mass).kilogramsPerCubicMeter == 1'200);
            REQUIRE(authored.initialLinearVelocity == Math::Vec3{3, 4, 0});
            REQUIRE(shape.slot.index == 7);
            REQUIRE(shape.slot.generation == 3);
            REQUIRE(pose.translation == Math::Vec3{1, 2, 3});
        }

        TEST_CASE("Physics body resolution reports authored handle and pose failures without partial output", "[physics][body]") {
            auto authored = MakeAuthoredBody();
            const ShapeHandle shape{PhysicsWorldId::Create(41).Value(), {7, 3}};
            authored.initialLinearVelocity.x = 1;
            REQUIRE(ResolvePhysicsBodyDescriptor(authored, shape, {}, shape.world).HasError());
            authored.initialLinearVelocity = {};
            const auto foreign = ResolvePhysicsBodyDescriptor(authored, shape, {}, PhysicsWorldId::Create(42).Value());
            REQUIRE(foreign.HasError());
            REQUIRE(foreign.ErrorValue().code.Value() == PhysicsErrors::HandleWorldMismatch.code.Value());
            auto pose = PhysicsPose{};
            pose.rotation.w = 2;
            REQUIRE(ResolvePhysicsBodyDescriptor(authored, shape, pose, shape.world).HasError());
            REQUIRE(pose.rotation.w == 2);
        }

        TEST_CASE("Physics body state is query evidence independent of authored speed admission", "[physics][body]") {
            auto state = MakeBodyState();
            REQUIRE(state.linearVelocity == Math::Vec3{});
            REQUIRE(state.angularVelocity == Math::Vec3{});
            state.pose.translation = {1, 2, 3};
            state.linearVelocity = {600, 0, 0};
            state.angularVelocity = {0, 3, 0};
            REQUIRE(ValidatePhysicsBodyState(state, state.body.world).HasValue());
            state.activity = PhysicsBodyActivity::Sleeping;
            REQUIRE(ValidatePhysicsBodyState(state, state.body.world).HasValue());
            REQUIRE(state.linearVelocity.x == 600);
        }

        TEST_CASE("Physics body state rejects malformed foreign and non-finite observations", "[physics][body]") {
            auto state = MakeBodyState();
            const auto foreign = ValidatePhysicsBodyState(state, PhysicsWorldId::Create(42).Value());
            REQUIRE(foreign.HasError());
            REQUIRE(foreign.ErrorValue().code.Value() == PhysicsErrors::HandleWorldMismatch.code.Value());
            state.body.slot.generation = 0;
            REQUIRE(ValidatePhysicsBodyState(state, state.body.world).HasError());
            state = MakeBodyState();
            state.pose.translation.y = std::numeric_limits<float>::quiet_NaN();
            REQUIRE(ValidatePhysicsBodyState(state, state.body.world).HasError());
            state = MakeBodyState();
            state.angularVelocity.z = std::numeric_limits<float>::infinity();
            REQUIRE(ValidatePhysicsBodyState(state, state.body.world).HasError());
            state.angularVelocity = {};
            state.activity = static_cast<PhysicsBodyActivity>(255);
            const auto unsupported = ValidatePhysicsBodyState(state, state.body.world);
            REQUIRE(unsupported.HasError());
            REQUIRE(unsupported.ErrorValue().code.Value() == PhysicsErrors::OperationUnsupported.code.Value());
        }
    }  // namespace
}  // namespace Horo::Physics
