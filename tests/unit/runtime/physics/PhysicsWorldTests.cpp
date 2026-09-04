#include "Horo/Physics/PhysicsWorld.h"

#include <catch2/catch_test_macros.hpp>
#include <thread>

namespace Horo::Physics {
    namespace {
        PhysicsWorldSettings SmallWorldSettings() {
            PhysicsWorldSettingsDescriptor descriptor;
            descriptor.world.capacity = {16, 32, 16, 4096};
            descriptor.budgets.maximumContactPairs = 32;
            descriptor.budgets.maximumContactConstraints = 16;
            descriptor.budgets.maximumInFlightPairs = 8;
            descriptor.budgets.scratchBytes = 1024 * 1024;
            return PhysicsWorldSettings::Capture(descriptor).Value();
        }
    }  // namespace

    TEST_CASE("Null Physics is explicit omitted capability and never invents simulation", "[physics][lifecycle]") {
        auto created = PhysicsRuntime::Create(PhysicsRuntimeMode::Null);
        REQUIRE(created.HasValue());
        auto runtime = std::move(created).Value();
        REQUIRE(runtime->Mode() == PhysicsRuntimeMode::Null);
        REQUIRE(runtime->State() == PhysicsRuntimeState::Ready);
        REQUIRE(runtime->Availability() == PhysicsAvailability::Omitted);
        for (std::uint8_t value = 0; value < static_cast<std::uint8_t>(PhysicsCapability::Count); ++value)
            REQUIRE(runtime->Capability(static_cast<PhysicsCapability>(value)) == PhysicsCapabilitySupport::Unsupported);
        REQUIRE(runtime->Capability(static_cast<PhysicsCapability>(255)) == PhysicsCapabilitySupport::Unknown);
        const auto settings = SmallWorldSettings();
        auto prepared = runtime->PrepareWorld(settings);
        REQUIRE(prepared.HasValue());
        auto world = std::move(prepared).Value();
        REQUIRE(world->State() == PhysicsWorldState::PreparedNull);
        REQUIRE_FALSE(world->Identity().IsValid());
        REQUIRE(world->Settings().Identity() == settings.Identity());
        REQUIRE(world->Activate({}).ErrorValue().code.Value() == PhysicsErrors::WorldInvalid.code.Value());
        REQUIRE(world->State() == PhysicsWorldState::PreparedNull);
        const auto identity = PhysicsWorldId::Create(100).Value();
        REQUIRE(world->Activate(identity).HasValue());
        REQUIRE(world->State() == PhysicsWorldState::ActiveNull);
        REQUIRE(world->Identity() == identity);
        REQUIRE(world->Activate(identity).ErrorValue().code.Value() == PhysicsErrors::InvalidState.code.Value());
        world->Shutdown();
        world->Shutdown();
        REQUIRE(world->State() == PhysicsWorldState::Destroyed);
        REQUIRE(world->Identity() == identity);
        runtime->Shutdown();
        runtime->Shutdown();
        REQUIRE(runtime->State() == PhysicsRuntimeState::Stopped);
        REQUIRE(runtime->PrepareWorld(settings).ErrorValue().code.Value() == PhysicsErrors::InvalidState.code.Value());
    }

    TEST_CASE("Physics rejects unknown compositions and closes unactivated candidates on runtime shutdown", "[physics][lifecycle]") {
        const auto unknown = PhysicsRuntime::Create(static_cast<PhysicsRuntimeMode>(255));
        REQUIRE(unknown.HasError());
        REQUIRE(unknown.ErrorValue().code.Value() == PhysicsErrors::OperationUnsupported.code.Value());
        auto runtime = std::move(PhysicsRuntime::Create(PhysicsRuntimeMode::Null).Value());
        auto candidate = std::move(runtime->PrepareWorld(SmallWorldSettings()).Value());
        runtime->Shutdown();
        REQUIRE(candidate->Activate(PhysicsWorldId::Create(101).Value()).ErrorValue().code.Value() ==
                PhysicsErrors::InvalidState.code.Value());
        runtime.reset();
        candidate->Shutdown();
        REQUIRE(candidate->State() == PhysicsWorldState::Destroyed);
    }

    TEST_CASE("Physics preparation and activation reject a foreign owner thread", "[physics][lifecycle]") {
        auto runtime = std::move(PhysicsRuntime::Create(PhysicsRuntimeMode::Null).Value());
        const auto settings = SmallWorldSettings();
        auto candidate = std::move(runtime->PrepareWorld(settings).Value());
        bool rejectedPreparation = false;
        bool rejectedActivation = false;
        std::thread foreign([&] {
            const auto prepared = runtime->PrepareWorld(settings);
            rejectedPreparation = prepared.HasError() && prepared.ErrorValue().code.Value() == PhysicsErrors::InvalidState.code.Value();
            const auto activated = candidate->Activate(PhysicsWorldId::Create(102).Value());
            rejectedActivation = activated.HasError() && activated.ErrorValue().code.Value() == PhysicsErrors::InvalidState.code.Value();
        });
        foreign.join();
        REQUIRE(rejectedPreparation);
        REQUIRE(rejectedActivation);
        REQUIRE(candidate->State() == PhysicsWorldState::PreparedNull);
    }

    TEST_CASE("Physics rejects duplicate active world identity without changing either world", "[physics][lifecycle]") {
        auto runtime = PhysicsRuntime::Create(PhysicsRuntimeMode::Null).Value();
        const auto settings = SmallWorldSettings();
        auto first = runtime->PrepareWorld(settings).Value();
        auto second = runtime->PrepareWorld(settings).Value();
        const auto identity = PhysicsWorldId::Create(105).Value();
        REQUIRE(first->Activate(identity).HasValue());
        const auto duplicate = second->Activate(identity);
        REQUIRE(duplicate.HasError());
        REQUIRE(duplicate.ErrorValue().code.Value() == PhysicsErrors::WorldInvalid.code.Value());
        REQUIRE(first->State() == PhysicsWorldState::ActiveNull);
        REQUIRE(second->State() == PhysicsWorldState::PreparedNull);
        REQUIRE_FALSE(second->Identity().IsValid());
    }

    TEST_CASE("Canonical Physics composition is explicit and world ownership survives owner shutdown", "[physics][lifecycle]") {
        auto created = PhysicsRuntime::Create(PhysicsRuntimeMode::Canonical);
#if HORO_TEST_PHYSICS_NATIVE
        REQUIRE(created.HasValue());
        auto runtime = std::move(created).Value();
        REQUIRE(runtime->Availability() == PhysicsAvailability::Available);
        REQUIRE(runtime->Capability(PhysicsCapability::WorldCreation) == PhysicsCapabilitySupport::Available);
        REQUIRE(runtime->Capability(PhysicsCapability::RigidBodies) == PhysicsCapabilitySupport::Unsupported);
        const auto duplicate = PhysicsRuntime::Create(PhysicsRuntimeMode::Canonical);
        REQUIRE(duplicate.HasError());
        REQUIRE(duplicate.ErrorValue().code.Value() == PhysicsErrors::InvalidState.code.Value());
        auto first = std::move(runtime->PrepareWorld(SmallWorldSettings()).Value());
        auto second = std::move(runtime->PrepareWorld(SmallWorldSettings()).Value());
        REQUIRE(first->State() == PhysicsWorldState::PreparedSolver);
        REQUIRE(second->State() == PhysicsWorldState::PreparedSolver);
        REQUIRE(first->Activate(PhysicsWorldId::Create(103).Value()).HasValue());
        REQUIRE(second->Activate(PhysicsWorldId::Create(104).Value()).HasValue());
        REQUIRE(first->Identity() != second->Identity());
        first->Shutdown();
        REQUIRE(second->State() == PhysicsWorldState::ActiveSolver);
        runtime->Shutdown();
        REQUIRE(runtime->Availability() == PhysicsAvailability::Unavailable);
        REQUIRE(runtime->Capability(PhysicsCapability::WorldCreation) == PhysicsCapabilitySupport::Unavailable);
        runtime.reset();
        REQUIRE(PhysicsRuntime::Create(PhysicsRuntimeMode::Canonical).HasError());
        second->Shutdown();
        REQUIRE(PhysicsRuntime::Create(PhysicsRuntimeMode::Canonical).HasValue());
#else
        REQUIRE(created.HasError());
        REQUIRE(created.ErrorValue().code.Value() == PhysicsErrors::CapabilityUnavailable.code.Value());
#endif
    }
}  // namespace Horo::Physics
