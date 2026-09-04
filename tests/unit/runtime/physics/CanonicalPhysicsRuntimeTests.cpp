#include "CanonicalPhysicsRuntime.h"
#include "Horo/Physics/PhysicsErrors.h"

#include <Jolt/Jolt.h>

// Jolt subsidiary headers require its root definitions first.
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/Memory.h>
#include <catch2/catch_test_macros.hpp>

namespace Horo::Physics::Detail {
    namespace {
        struct RuntimeOwner final {
            CanonicalRuntimeHandle handle;

            ~RuntimeOwner() {
                DestroyCanonicalRuntime(handle);
            }
        };

        struct WorldOwner final {
            CanonicalWorldHandle handle;

            ~WorldOwner() {
                DestroyCanonicalWorld(handle);
            }
        };

        PhysicsWorldSettings SmallSettings() {
            PhysicsWorldSettingsDescriptor descriptor;
            descriptor.world.capacity = {16, 32, 16, 4096};
            descriptor.budgets.maximumContactPairs = 32;
            descriptor.budgets.maximumContactConstraints = 16;
            descriptor.budgets.maximumInFlightPairs = 8;
            descriptor.budgets.scratchBytes = 1024 * 1024;
            return PhysicsWorldSettings::Capture(descriptor).Value();
        }

        void RequireUnownedNativeGlobals() {
            REQUIRE(JPH::Factory::sInstance == nullptr);
            REQUIRE(JPH::Allocate == nullptr);
            REQUIRE(JPH::Reallocate == nullptr);
            REQUIRE(JPH::Free == nullptr);
            REQUIRE(JPH::AlignedAllocate == nullptr);
            REQUIRE(JPH::AlignedFree == nullptr);
        }
    }  // namespace

    TEST_CASE("Canonical process initialization rolls back every completed registration stage", "[physics][native][lifecycle]") {
        for (const auto point :
             {CanonicalFailurePoint::AllocatorRegistered, CanonicalFailurePoint::FactoryCreated, CanonicalFailurePoint::TypesRegistered}) {
            RequireUnownedNativeGlobals();
            const auto failed = CreateCanonicalRuntime(point);
            REQUIRE(failed.HasError());
            REQUIRE(failed.ErrorValue().code.Value() == PhysicsErrors::InitializationFailed.code.Value());
            RequireUnownedNativeGlobals();
        }
        const auto created = CreateCanonicalRuntime();
        REQUIRE(created.HasValue());
        const RuntimeOwner runtime{created.Value()};
        REQUIRE(JPH::Factory::sInstance != nullptr);
    }

    TEST_CASE("Canonical world initialization releases scratch jobs and solver state after each failure", "[physics][native][lifecycle]") {
        const auto created = CreateCanonicalRuntime();
        REQUIRE(created.HasValue());
        const RuntimeOwner runtime{created.Value()};
        const auto settings = SmallSettings();
        for (const auto point :
             {CanonicalFailurePoint::ScratchCreated, CanonicalFailurePoint::JobsCreated, CanonicalFailurePoint::SystemInitialized}) {
            const auto failed = CreateCanonicalWorld(runtime.handle, settings, point);
            REQUIRE(failed.HasError());
            REQUIRE(failed.ErrorValue().code.Value() == PhysicsErrors::InitializationFailed.code.Value());
            REQUIRE(InspectCanonicalResources(runtime.handle) == CanonicalResourceCounts{});
        }
        const auto firstResult = CreateCanonicalWorld(runtime.handle, settings);
        REQUIRE(firstResult.HasValue());
        const WorldOwner first{firstResult.Value()};
        REQUIRE(InspectCanonicalResources(runtime.handle) == CanonicalResourceCounts{1, 1, 1, 1});
        {
            const auto secondResult = CreateCanonicalWorld(runtime.handle, settings);
            REQUIRE(secondResult.HasValue());
            const WorldOwner second{secondResult.Value()};
            REQUIRE(first.handle.value != second.handle.value);
            REQUIRE(InspectCanonicalResources(runtime.handle) == CanonicalResourceCounts{2, 2, 2, 2});
        }
        REQUIRE(InspectCanonicalResources(runtime.handle) == CanonicalResourceCounts{1, 1, 1, 1});
    }

    TEST_CASE("Canonical world preflight rejects missing runtime and unimplemented containment before allocation",
              "[physics][native][lifecycle]") {
        const auto settings = SmallSettings();
        REQUIRE(CreateCanonicalWorld({}, settings).ErrorValue().code.Value() == PhysicsErrors::InvalidState.code.Value());
        REQUIRE(InspectCanonicalResources({}) == CanonicalResourceCounts{});
        const auto created = CreateCanonicalRuntime();
        REQUIRE(created.HasValue());
        const RuntimeOwner runtime{created.Value()};
        auto descriptor = settings.Values();
        descriptor.nonFinitePolicy = PhysicsNonFinitePolicy::QuarantineBody;
        const auto quarantine = PhysicsWorldSettings::Capture(descriptor);
        REQUIRE(quarantine.HasValue());
        const auto rejected = CreateCanonicalWorld(runtime.handle, quarantine.Value());
        REQUIRE(rejected.HasError());
        REQUIRE(rejected.ErrorValue().code.Value() == PhysicsErrors::OperationUnsupported.code.Value());
        REQUIRE(InspectCanonicalResources(runtime.handle) == CanonicalResourceCounts{});
    }

    TEST_CASE("Canonical worlds reject zero-body capacity before allocation and accept one-body capacity", "[physics][native][lifecycle]") {
        const auto created = CreateCanonicalRuntime();
        REQUIRE(created.HasValue());
        const RuntimeOwner runtime{created.Value()};
        auto descriptor = SmallSettings().Values();
        descriptor.world.capacity.maximumBodies = 0;
        const auto settings = PhysicsWorldSettings::Capture(descriptor);
        REQUIRE(settings.HasValue());
        const auto rejected = CreateCanonicalWorld(runtime.handle, settings.Value());
        REQUIRE(rejected.HasError());
        REQUIRE(rejected.ErrorValue().code.Value() == PhysicsErrors::OperationUnsupported.code.Value());
        REQUIRE(InspectCanonicalResources(runtime.handle) == CanonicalResourceCounts{});
        descriptor.world.capacity.maximumBodies = 1;
        const auto minimal = PhysicsWorldSettings::Capture(descriptor);
        REQUIRE(minimal.HasValue());
        {
            const auto prepared = CreateCanonicalWorld(runtime.handle, minimal.Value());
            REQUIRE(prepared.HasValue());
            const WorldOwner world{prepared.Value()};
            REQUIRE(InspectCanonicalResources(runtime.handle) == CanonicalResourceCounts{1, 1, 1, 1});
        }
        REQUIRE(InspectCanonicalResources(runtime.handle) == CanonicalResourceCounts{});
    }

    TEST_CASE("Canonical allocation hooks preserve platform allocation and alignment semantics", "[physics][native][lifecycle]") {
        const auto created = CreateCanonicalRuntime();
        REQUIRE(created.HasValue());
        const RuntimeOwner runtime{created.Value()};
        void *memory = JPH::Allocate(16);
        REQUIRE(memory != nullptr);
        memory = JPH::Reallocate(memory, 16, 32);
        REQUIRE(memory != nullptr);
        JPH::Free(memory);
        void *aligned = JPH::AlignedAllocate(64, 32);
        REQUIRE(aligned != nullptr);
        REQUIRE(reinterpret_cast<std::uintptr_t>(aligned) % 32 == 0);
        JPH::AlignedFree(aligned);
        void *empty = JPH::Allocate(0);
        REQUIRE(empty != nullptr);
        JPH::Free(empty);
        REQUIRE(CreateCanonicalRuntime().ErrorValue().code.Value() == PhysicsErrors::InvalidState.code.Value());
    }
}  // namespace Horo::Physics::Detail
