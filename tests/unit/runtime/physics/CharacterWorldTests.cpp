#include "CharacterControllerRegistry.h"
#include "Horo/Physics/CharacterWorld.h"
#include "PhysicsTestUtils.h"

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <limits>
#include <thread>
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

        [[nodiscard]] CharacterWorldPreparationDescriptor WorldDescriptor() {
            return {61, PhysicsWorldId(), 91, 101};
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

        [[nodiscard]] CharacterControllerDescriptor ControllerDescriptor(const CharacterWorldDescriptor &world) {
            CharacterControllerDescriptor descriptor;
            descriptor.sceneGeneration = world.sceneGeneration;
            descriptor.characterWorld = world.identity;
            descriptor.physicsWorld = world.physicsWorld;
            descriptor.collisionProfile = Physics::CollisionProfileId::Parse("22345678-1234-4234-8234-123456789abc").Value();
            descriptor.queryChannel = Physics::PhysicsQueryChannelId::Parse("32345678-1234-4234-8234-123456789abc").Value();
            descriptor.defaultMaterial = Material();
            return descriptor;
        }

        using Physics::Test::RequireError;

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
            REQUIRE(prepared.Value()->Descriptor().sceneGeneration == owner.sceneGeneration);
            REQUIRE(prepared.Value()->Descriptor().physicsWorld == owner.physicsWorld);
            REQUIRE(prepared.Value()->Descriptor().collisionFilterGeneration == owner.collisionFilterGeneration);
            REQUIRE(prepared.Value()->Descriptor().originGeneration == owner.originGeneration);
            REQUIRE(prepared.Value()->Descriptor().identity.IsValid());
            REQUIRE(prepared.Value()->Settings().Identity() == settings.Identity());
            REQUIRE(prepared.Value()->ControllerCapacity() == 2);
            REQUIRE(prepared.Value()->ActiveControllerCount() == 0);

            auto invalid = owner;
            invalid.sceneGeneration = 0;
            RequireError(CharacterWorld::Prepare(invalid, settings), CharacterErrors::WorldInvalid);
            invalid = owner;
            invalid.collisionFilterGeneration = 0;
            RequireError(CharacterWorld::Prepare(invalid, settings), CharacterErrors::WorldInvalid);
            invalid = owner;
            invalid.physicsWorld = {};
            RequireError(CharacterWorld::Prepare(invalid, settings), CharacterErrors::WorldInvalid);
            invalid = owner;
            invalid.originGeneration = 0;
            RequireError(CharacterWorld::Prepare(invalid, settings), CharacterErrors::WorldInvalid);
        }

        TEST_CASE("Character world deterministically reuses slots without aliasing stale handles",
                  "[physics][character][world][capacity]") {
            auto world = PreparedWorld();
            const auto descriptor = ControllerDescriptor(world->Descriptor());
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
            auto descriptor = ControllerDescriptor(world->Descriptor());
            descriptor.capsule.radiusMeters = 0;
            RequireError(world->CreateController(descriptor), CharacterErrors::DescriptorInvalid);
            REQUIRE(world->ActiveControllerCount() == 0);

            descriptor = ControllerDescriptor(world->Descriptor());
            descriptor.characterWorld = WorldId(72);
            RequireError(world->CreateController(descriptor), CharacterErrors::HandleWorldMismatch);
            descriptor = ControllerDescriptor(world->Descriptor());
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
            RequireError(prepared.Value()->CreateController(ControllerDescriptor(prepared.Value()->Descriptor())),
                         CharacterErrors::CapacityExceeded);
            REQUIRE(prepared.Value()->ActiveControllerCount() == 0);
        }

        TEST_CASE("Character world activation and shutdown are explicit terminal lifecycle operations",
                  "[physics][character][world][lifecycle]") {
            auto world = PreparedWorld();
            const auto descriptor = ControllerDescriptor(world->Descriptor());
            const auto handle = world->CreateController(descriptor);
            REQUIRE(handle.HasValue());
            REQUIRE(world->Activate().HasValue());
            REQUIRE(world->State() == CharacterWorldState::Active);
            RequireError(world->Activate(), CharacterErrors::InvalidState);
            RequireError(world->CreateController(descriptor), CharacterErrors::InvalidState);
            RequireError(world->DestroyController(handle.Value()), CharacterErrors::InvalidState);

            world->Shutdown();
            REQUIRE(world->State() == CharacterWorldState::Destroyed);
            REQUIRE(world->ActiveControllerCount() == 0);
            world->Shutdown();
            RequireError(world->ControllerDescriptor(handle.Value()), CharacterErrors::InvalidState);
            RequireError(world->DestroyController(handle.Value()), CharacterErrors::InvalidState);
            RequireError(world->CreateController(descriptor), CharacterErrors::InvalidState);
            RequireError(world->Activate(), CharacterErrors::InvalidState);
        }

        TEST_CASE("Character worlds issue distinct owner generations and reject cross-world aliases",
                  "[physics][character][world][identity]") {
            auto first = PreparedWorld(1);
            auto second = PreparedWorld(1);
            REQUIRE(first->Descriptor().identity != second->Descriptor().identity);
            const auto firstHandle = first->CreateController(ControllerDescriptor(first->Descriptor()));
            const auto secondHandle = second->CreateController(ControllerDescriptor(second->Descriptor()));
            REQUIRE(firstHandle.HasValue());
            REQUIRE(secondHandle.HasValue());
            REQUIRE(firstHandle.Value().slot == secondHandle.Value().slot);
            RequireError(first->ControllerDescriptor(secondHandle.Value()), CharacterErrors::HandleWorldMismatch);
            RequireError(second->ControllerDescriptor(firstHandle.Value()), CharacterErrors::HandleWorldMismatch);
        }

        TEST_CASE("Character world rejects prepared mutation from a foreign thread without changing storage",
                  "[physics][character][world][thread]") {
            auto world = PreparedWorld(1);
            const auto descriptor = ControllerDescriptor(world->Descriptor());
            std::optional<Result<void>> activationResult;
            std::thread activation([&] {
                activationResult = world->Activate();
            });
            activation.join();
            REQUIRE(activationResult.has_value());
            RequireError(*activationResult, CharacterErrors::InvalidState);
            REQUIRE(world->State() == CharacterWorldState::Prepared);

            std::optional<Result<CharacterControllerHandle>> creationResult;
            std::thread foreign([&] {
                creationResult = world->CreateController(descriptor);
            });
            foreign.join();
            REQUIRE(creationResult.has_value());
            RequireError(*creationResult, CharacterErrors::InvalidState);
            REQUIRE(world->ActiveControllerCount() == 0);
        }

        struct TrackedRecord final {
            explicit TrackedRecord(std::uint32_t &live) noexcept : live_(&live) {
                ++*live_;
            }

            TrackedRecord(const TrackedRecord &) = delete;
            TrackedRecord &operator=(const TrackedRecord &) = delete;

            TrackedRecord(TrackedRecord &&other) noexcept : live_(std::exchange(other.live_, nullptr)) {}

            TrackedRecord &operator=(TrackedRecord &&) noexcept = delete;

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
            std::uint32_t live{};
            Registry source{61, WorldId(), {.maximumSlots = 1, .maximumGeneration = 2}};
            auto registry = std::move(source);
            RequireError(source.Acquire(TrackedRecord{live}), CharacterErrors::CapacityExceeded);
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

            Registry drainable{61, WorldId(), {.maximumSlots = 1}};
            REQUIRE(drainable.Acquire(TrackedRecord{live}).HasValue());
            REQUIRE(live == 1);
            drainable.Drain();
            REQUIRE(live == 0);
            REQUIRE(drainable.ActiveCount() == 0);
        }

    }  // namespace
}  // namespace Horo::Character
