#include "Horo/Foundation/JobSystem.h"
#include "Horo/Physics/PhysicsErrors.h"
#include "Horo/Physics/PhysicsWorld.h"
#include "PhysicsTestUtils.h"

#include <array>
#include <atomic>
#include <catch2/catch_test_macros.hpp>

namespace Horo::Physics {
    namespace {
        [[nodiscard]] PhysicsStructuralCommand Command(const std::uint64_t tick, const std::uint64_t world, const std::uint64_t scene,
                                                       const std::uint64_t target) {
            return {.order = {.simulationTick = tick,
                              .worldGeneration = world,
                              .sceneGeneration = scene,
                              .targetKind = PhysicsCommandTargetKind::Body,
                              .targetIdentity = target,
                              .commandKind = PhysicsStructuralCommandKind::Change,
                              .source = PhysicsCommandSourceId::Create(1).Value(),
                              .sourceSequence = 1}};
        }

        struct JobProbe final {
            std::atomic_uint32_t completed{};
            bool fail{};
        };

        Result<void> RunJob(void *context, const CancellationToken &) noexcept {
            auto &probe = *static_cast<JobProbe *>(context);
            probe.completed.fetch_add(1, std::memory_order_release);
            if (probe.fail)
                return Result<void>::Failure(MakeError(PhysicsErrors::InitializationFailed, "Qualification-injected job failure."));
            return Result<void>::Success();
        }
    }  // namespace

    TEST_CASE("Physics null qualification preserves world isolation through reset and unload",
              "[physics][qualification][headless][lifecycle]") {
        auto runtime = PhysicsRuntime::Create(PhysicsRuntimeMode::Null).Value();
        auto first = runtime->PrepareWorld(Test::SmallWorldSettings()).Value();
        auto second = runtime->PrepareWorld(Test::SmallWorldSettings()).Value();
        REQUIRE(first->Activate(PhysicsWorldId::Create(301).Value()).HasValue());
        REQUIRE(second->Activate(PhysicsWorldId::Create(302).Value()).HasValue());

        REQUIRE(first->Reset().HasValue());
        REQUIRE_FALSE(first->Identity().IsValid());
        REQUIRE(first->LifecycleCause() == PhysicsWorldLifecycleCause::Reset);
        REQUIRE(second->State() == PhysicsWorldState::ActiveNull);
        REQUIRE(second->Identity() == PhysicsWorldId::Create(302).Value());

        REQUIRE(first->Activate(PhysicsWorldId::Create(303).Value()).HasValue());
        REQUIRE(first->UnloadScene().HasValue());
        REQUIRE(first->UnloadScene().HasValue());
        REQUIRE(second->State() == PhysicsWorldState::ActiveNull);
        REQUIRE_FALSE(second->LastFailure().has_value());
        second->Shutdown();
        second->Shutdown();
        runtime->Shutdown();
        runtime->Shutdown();
    }

#if HORO_TEST_PHYSICS_NATIVE
    TEST_CASE("Physics canonical qualification isolates simultaneous worlds and destruction",
              "[physics][qualification][headless][lifecycle][tick]") {
        auto runtime = PhysicsRuntime::Create(PhysicsRuntimeMode::Canonical).Value();
        auto first = runtime->PrepareWorld(Test::SmallWorldSettings()).Value();
        auto second = runtime->PrepareWorld(Test::SmallWorldSettings()).Value();
        REQUIRE(first->Activate(PhysicsWorldId::Create(311).Value()).HasValue());
        REQUIRE(second->Activate(PhysicsWorldId::Create(312).Value()).HasValue());
        constexpr Duration fixedDelta = Duration::FromNanoseconds(16'666'667);

        REQUIRE(first->QueueStructuralCommand(Command(1, 311, 41, 1)).HasValue());
        REQUIRE(second->QueueStructuralCommand(Command(1, 312, 42, 2)).HasValue());
        REQUIRE(first->AdvanceFixedTick({.simulationTick = 1, .sceneGeneration = 41, .fixedDelta = fixedDelta}).HasValue());
        REQUIRE(second->AdvanceFixedTick({.simulationTick = 1, .sceneGeneration = 42, .fixedDelta = fixedDelta}).HasValue());
        REQUIRE(first->PublishedTick().completedTick == 1);
        REQUIRE(second->PublishedTick().completedTick == 1);
        REQUIRE(first->PublishedTick().appliedCommands == 1);
        REQUIRE(second->PublishedTick().appliedCommands == 1);

        REQUIRE(first->UnloadScene().HasValue());
        REQUIRE(first->State() == PhysicsWorldState::Destroyed);
        REQUIRE(second->QueueStructuralCommand(Command(2, 312, 42, 3)).HasValue());
        REQUIRE(second->AdvanceFixedTick({.simulationTick = 2, .sceneGeneration = 42, .fixedDelta = fixedDelta}).HasValue());
        REQUIRE(second->PublishedTick().completedTick == 2);
        REQUIRE(second->TickStatistics().pendingCommands == 0);
        second->Shutdown();
        runtime->Shutdown();
    }

    TEST_CASE("Physics canonical qualification drains failures and repeated lifecycle loops",
              "[physics][qualification][headless][lifecycle][jobs]") {
        JobSystem jobs({.workerCount = 2, .maxQueuedJobs = 4});
        auto runtime = PhysicsRuntime::Create(PhysicsRuntimeMode::Canonical, &jobs).Value();
        constexpr Duration fixedDelta = Duration::FromNanoseconds(16'666'667);

        for (std::uint64_t cycle = 0; cycle < 16; ++cycle) {
            auto world = runtime->PrepareWorld(Test::SmallWorldSettings()).Value();
            const std::uint64_t firstGeneration = 400 + cycle * 2;
            const std::uint64_t secondGeneration = firstGeneration + 1;
            REQUIRE(world->Activate(PhysicsWorldId::Create(firstGeneration).Value()).HasValue());
            REQUIRE(world->QueueStructuralCommand(Command(1, firstGeneration, 51, cycle + 1)).HasValue());
            REQUIRE(world->AdvanceFixedTick({.simulationTick = 1, .sceneGeneration = 51, .fixedDelta = fixedDelta}).HasValue());
            REQUIRE(world->Reset().HasValue());
            REQUIRE(world->PublishedTick().publicationRevision == 0);
            REQUIRE(world->TickStatistics().pendingCommands == 0);
            REQUIRE_FALSE(world->LastFailure().has_value());
            REQUIRE(world->Activate(PhysicsWorldId::Create(secondGeneration).Value()).HasValue());
            REQUIRE(world->UnloadScene().HasValue());
            REQUIRE(world->UnloadScene().HasValue());
        }

        auto failedWorld = runtime->PrepareWorld(Test::SmallWorldSettings()).Value();
        REQUIRE(failedWorld->Activate(PhysicsWorldId::Create(500).Value()).HasValue());
        JobProbe failure{.fail = true};
        const std::array batchJobs{PhysicsSolverJob{.context = &failure, .execute = RunJob}};
        const PhysicsSolverJobBatch batch{.jobs = batchJobs.data(),
                                          .jobCount = static_cast<std::uint32_t>(batchJobs.size()),
                                          .joinTimeout = Duration::FromMilliseconds(500)};
        const auto failed =
            failedWorld->AdvanceFixedTick({.simulationTick = 1, .sceneGeneration = 52, .fixedDelta = fixedDelta, .solverJobs = batch});
        REQUIRE(failed.HasError());
        REQUIRE(failure.completed.load(std::memory_order_acquire) == 1);
        REQUIRE(failedWorld->State() == PhysicsWorldState::Failed);
        REQUIRE(failedWorld->PublishedTick().publicationRevision == 0);
        REQUIRE(failedWorld->LastFailure().has_value());
        REQUIRE(failedWorld->Reset().HasValue());
        REQUIRE_FALSE(failedWorld->LastFailure().has_value());
        REQUIRE(failedWorld->UnloadScene().HasValue());

        runtime->Shutdown();
        jobs.Shutdown(ShutdownPolicy::Cancel);
    }
#endif
}  // namespace Horo::Physics
