#include "CharacterControllerRegistry.h"
#include "Horo/Physics/CharacterWorld.h"

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <limits>
#include <utility>

namespace Horo::Character {
    namespace {
        [[nodiscard]] CharacterWorldId WorldId(const std::uint64_t value = 71) {
            const auto result = CharacterWorldId::Create(value);
            REQUIRE(result.HasValue());
            return result.Value();
        }

        [[nodiscard]] Physics::PhysicsWorldId PhysicsWorldId(const std::uint64_t value = 81) {
            const auto result = Physics::PhysicsWorldId::Create(value);
            REQUIRE(result.HasValue());
            return result.Value();
        }

        [[nodiscard]] CharacterWorldDescriptor WorldDescriptor() {
            return {61, WorldId(), PhysicsWorldId()};
        }

        [[nodiscard]] CharacterWorldSettings Settings(const std::uint32_t maximumControllers = 2) {
            CharacterWorldSettingsDescriptor descriptor;
            descriptor.capacities.maximumControllers = maximumControllers;
            const auto result = CharacterWorldSettings::Capture(descriptor);
            REQUIRE(result.HasValue());
            return result.Value();
        }

        [[nodiscard]] Physics::PhysicsQueryMaterial Material() {
            return {
                Assets::AssetId::Parse("12345678-1234-4234-8234-123456789abc").Value(),
                3,
                Physics::PhysicsMaterialSlotId::FromValue(5),
            };
        }

        [[nodiscard]] CharacterControllerDescriptor ControllerDescriptor(const CharacterWorldDescriptor &world = WorldDescriptor()) {
            CharacterControllerDescriptor descriptor;
            descriptor.sceneGeneration = world.sceneGeneration;
            descriptor.characterWorld = world.identity;
            descriptor.physicsWorld = world.physicsWorld;
            descriptor.collisionProfile = Physics::CollisionProfileId::Parse("22345678-1234-4234-8234-123456789abc").Value();
            descriptor.queryChannel = Physics::PhysicsQueryChannelId::Parse("32345678-1234-4234-8234-123456789abc").Value();
            descriptor.defaultMaterial = Material();
            return descriptor;
        }

        template <typename ResultType> void RequireError(const ResultType &result, const ErrorCodeDescriptor &expected) {
            REQUIRE(result.HasError());
            REQUIRE(result.ErrorValue().code.Value() == expected.code.Value());
        }

        [[nodiscard]] std::unique_ptr<CharacterWorld> PreparedWorld(const std::uint32_t maximumControllers = 2) {
            const auto settings = Settings(maximumControllers);
            auto prepared = CharacterWorld::Prepare(WorldDescriptor(), settings);
            REQUIRE(prepared.HasValue());
            return std::move(prepared).Value();
        }

        TEST_CASE("Character world preparation captures exact ownership and capacity", "[physics][character][world]") {
            const auto settings = Settings(2);
            const auto owner = WorldDescriptor();
            auto prepared = CharacterWorld::Prepare(owner, settings);
            REQUIRE(prepared.HasValue());
            REQUIRE(prepared.Value()->State() == CharacterWorldState::Prepared);
            REQUIRE(prepared.Value()->Descriptor() == owner);
            REQUIRE(prepared.Value()->Settings().Identity() == settings.Identity());
            REQUIRE(prepared.Value()->ControllerCapacity() == 2);
            REQUIRE(prepared.Value()->ActiveControllerCount() == 0);

            auto invalid = owner;
            invalid.sceneGeneration = 0;
            RequireError(CharacterWorld::Prepare(invalid, settings), CharacterErrors::WorldInvalid);
            invalid = owner;
            invalid.identity = {};
            RequireError(CharacterWorld::Prepare(invalid, settings), CharacterErrors::WorldInvalid);
            invalid = owner;
            invalid.physicsWorld = {};
            RequireError(CharacterWorld::Prepare(invalid, settings), CharacterErrors::WorldInvalid);
        }

        TEST_CASE("Character world deterministically reuses slots without aliasing stale handles",
                  "[physics][character][world][capacity]") {
            auto world = PreparedWorld();
            const auto descriptor = ControllerDescriptor();
            const auto first = world->CreateController(descriptor);
            const auto second = world->CreateController(descriptor);
            REQUIRE(first.HasValue());
            REQUIRE(second.HasValue());
            REQUIRE(first.Value().slot.index == 0);
            REQUIRE(second.Value().slot.index == 1);
            REQUIRE(first.Value().slot.generation == 1);
            RequireError(world->CreateController(descriptor), CharacterErrors::CapacityExceeded);

            REQUIRE(world->DestroyController(first.Value()).HasValue());
            RequireError(world->ControllerDescriptor(first.Value()), CharacterErrors::HandleStale);
            RequireError(world->DestroyController(first.Value()), CharacterErrors::HandleStale);

            const auto replacement = world->CreateController(descriptor);
            REQUIRE(replacement.HasValue());
            REQUIRE(replacement.Value().slot.index == first.Value().slot.index);
            REQUIRE(replacement.Value().slot.generation == first.Value().slot.generation + 1);
            RequireError(world->ControllerDescriptor(first.Value()), CharacterErrors::HandleStale);
            REQUIRE(world->ControllerDescriptor(replacement.Value()).Value().physicsWorld == descriptor.physicsWorld);
        }

        TEST_CASE("Character world rejects malformed foreign and over-budget controller creation transactionally",
                  "[physics][character][world]") {
            auto world = PreparedWorld();
            auto descriptor = ControllerDescriptor();
            descriptor.capsule.radiusMeters = 0;
            RequireError(world->CreateController(descriptor), CharacterErrors::DescriptorInvalid);
            REQUIRE(world->ActiveControllerCount() == 0);

            descriptor = ControllerDescriptor();
            descriptor.characterWorld = WorldId(72);
            RequireError(world->CreateController(descriptor), CharacterErrors::HandleWorldMismatch);
            descriptor = ControllerDescriptor();
            descriptor.physicsWorld = PhysicsWorldId(82);
            RequireError(world->CreateController(descriptor), CharacterErrors::HandleWorldMismatch);
            REQUIRE(world->ActiveControllerCount() == 0);

            CharacterWorldSettingsDescriptor settingsDescriptor;
            settingsDescriptor.capacities.maximumControllers = 1;
            settingsDescriptor.work.maximumContactsPerMovement = 1;
            const auto settings = CharacterWorldSettings::Capture(settingsDescriptor);
            REQUIRE(settings.HasValue());
            auto prepared = CharacterWorld::Prepare(WorldDescriptor(), settings.Value());
            REQUIRE(prepared.HasValue());
            RequireError(prepared.Value()->CreateController(ControllerDescriptor()), CharacterErrors::CapacityExceeded);
            REQUIRE(prepared.Value()->ActiveControllerCount() == 0);
        }

        TEST_CASE("Character world activation and shutdown are explicit terminal lifecycle operations",
                  "[physics][character][world][lifecycle]") {
            auto world = PreparedWorld();
            const auto handle = world->CreateController(ControllerDescriptor());
            REQUIRE(handle.HasValue());
            REQUIRE(world->Activate().HasValue());
            REQUIRE(world->State() == CharacterWorldState::Active);
            RequireError(world->Activate(), CharacterErrors::InvalidState);
            REQUIRE(world->CreateController(ControllerDescriptor()).HasValue());

            world->Shutdown();
            REQUIRE(world->State() == CharacterWorldState::Destroyed);
            REQUIRE(world->ActiveControllerCount() == 0);
            world->Shutdown();
            RequireError(world->ControllerDescriptor(handle.Value()), CharacterErrors::InvalidState);
            RequireError(world->DestroyController(handle.Value()), CharacterErrors::InvalidState);
            RequireError(world->CreateController(ControllerDescriptor()), CharacterErrors::InvalidState);
            RequireError(world->Activate(), CharacterErrors::InvalidState);
        }

        struct TrackedRecord final {
            explicit TrackedRecord(std::uint32_t &live) noexcept : live_(&live) {
                ++*live_;
            }

            TrackedRecord(const TrackedRecord &) = delete;
            TrackedRecord &operator=(const TrackedRecord &) = delete;

            TrackedRecord(TrackedRecord &&other) noexcept : live_(std::exchange(other.live_, nullptr)) {}

            TrackedRecord &operator=(TrackedRecord &&other) noexcept {
                if (this != &other) {
                    Release();
                    live_ = std::exchange(other.live_, nullptr);
                }
                return *this;
            }

            ~TrackedRecord() noexcept {
                Release();
            }

        private:
            void Release() noexcept {
                if (live_ != nullptr)
                    --*live_;
                live_ = nullptr;
            }

            std::uint32_t *live_{};
        };

        TEST_CASE("Character registry drains owned records and retires non-wrapping generations",
                  "[physics][character][world][lifecycle]") {
            using Registry = Detail::CharacterControllerRegistry<TrackedRecord>;
            auto created = Registry::Create(61, WorldId(), {.maximumSlots = 1, .maximumGeneration = 2});
            REQUIRE(created.HasValue());
            auto registry = std::move(created).Value();
            std::uint32_t live{};
            const auto first = registry.Acquire(TrackedRecord{live});
            REQUIRE(first.HasValue());
            REQUIRE(live == 1);
            REQUIRE(registry.Remove(first.Value()).HasValue());
            REQUIRE(live == 0);
            const auto last = registry.Acquire(TrackedRecord{live});
            REQUIRE(last.HasValue());
            REQUIRE(last.Value().slot.generation == 2);
            REQUIRE(registry.Remove(last.Value()).HasValue());
            REQUIRE(live == 0);
            RequireError(registry.Acquire(TrackedRecord{live}), CharacterErrors::GenerationExhausted);
            REQUIRE(live == 0);

            auto drainableResult = Registry::Create(61, WorldId(), {.maximumSlots = 1});
            REQUIRE(drainableResult.HasValue());
            auto drainable = std::move(drainableResult).Value();
            REQUIRE(drainable.Acquire(TrackedRecord{live}).HasValue());
            REQUIRE(live == 1);
            drainable.Drain();
            REQUIRE(live == 0);
            REQUIRE(drainable.ActiveCount() == 0);
        }

        TEST_CASE("Character registry validates malformed preparation bounds", "[physics][character][world][capacity]") {
            using Registry = Detail::CharacterControllerRegistry<std::uint32_t>;
            RequireError(Registry::Create(0, WorldId(), {.maximumSlots = 1}), CharacterErrors::WorldInvalid);
            RequireError(Registry::Create(61, {}, {.maximumSlots = 1}), CharacterErrors::WorldInvalid);
            RequireError(Registry::Create(61, WorldId(), {.maximumSlots = 0}), CharacterErrors::DescriptorInvalid);
            RequireError(Registry::Create(61, WorldId(), {.maximumSlots = CharacterWorldSettingLimits::MaximumControllers + 1}),
                         CharacterErrors::CapacityExceeded);
            RequireError(Registry::Create(61, WorldId(), {.maximumSlots = 1, .maximumGeneration = 0}), CharacterErrors::DescriptorInvalid);
        }
    }  // namespace
}  // namespace Horo::Character
