#include "AllocationProbe.h"
#include "Horo/Navigation/NavigationRuntimeQueues.h"
#include "navigation/NavigationRuntimeTestFixtures.h"

#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <memory>

namespace Horo::Navigation {
    namespace {
        [[nodiscard]] NavigationWorldActivationDescriptor Activation(const std::uint64_t generation) {
            return TestSupport::Activation(1, generation, generation, generation);
        }

        [[nodiscard]] NavigationPathRequest Request(const NavigationWorldActivationDescriptor &activation) {
            return TestSupport::Request(activation.world, activation.topology);
        }

        [[nodiscard]] NavRequestHandle RequestHandle(const NavigationWorldId world, const std::uint32_t generation) {
            return TestSupport::RequestHandle(world, 1, generation);
        }

        [[nodiscard]] NavigationRuntimeQueueDescriptor QueueDescriptor() {
            return TestSupport::QueueDescriptor();
        }

        [[nodiscard]] NavigationWorldActivationDescriptor ExecuteReplacementIteration(
            NavigationWorldLifecycle &lifecycle, NavigationRuntimeQueues &queues,
            const std::shared_ptr<std::atomic<std::uint32_t>> &destructions, const NavigationWorldActivationDescriptor &active,
            const std::uint32_t generation) {
            auto lease = std::move(lifecycle.Acquire(active.world)).Value();
            NavigationQueuedQuery query{
                .acceptedSequence = generation - 1U,
                .handle = RequestHandle(active.world, generation - 1U),
                .request = Request(active),
                .worldLease = lease,
            };
            REQUIRE(queues.TryEnqueueQuery(query) == NavigationQueueEnqueueResult::Enqueued);
            auto workerRecord = queues.TryDequeueQuery();
            REQUIRE(workerRecord.has_value());

            const auto replacement = Activation(generation);
            REQUIRE(lifecycle.Stage(replacement, TestSupport::MakeObservedNavigationBackend(destructions)).HasValue());
            REQUIRE(lifecycle.CommitAtSafePoint(replacement.scene, replacement.sceneGeneration).HasValue());
            REQUIRE(workerRecord->worldLease.IsRevoked());
            REQUIRE(workerRecord->worldLease.Cancellation().IsCancellationRequested());

            NavigationQueuedCompletion stale{
                .acceptedSequence = workerRecord->acceptedSequence,
                .handle = workerRecord->handle,
                .scene = active.scene,
                .sceneGeneration = active.sceneGeneration,
                .world = active.world,
                .topology = active.topology,
                .outcome = NavigationCancelled{},
            };
            REQUIRE(queues.TryEnqueueCompletion(stale) == NavigationQueueEnqueueResult::Enqueued);
            const auto drained = queues.TryDequeueCompletion();
            REQUIRE(drained.has_value());
            REQUIRE(drained->world != replacement.world);
            REQUIRE(drained->sceneGeneration != replacement.sceneGeneration);
            workerRecord.reset();
            lease = {};
            REQUIRE(lifecycle.CollectRetired() == NavigationWorldLifecycleState::Active);
            return replacement;
        }
    }  // namespace

    TEST_CASE("Navigation replacement drains stale completions before provider reclamation",
              "[unit][navigation][qualification][lifecycle]") {
        constexpr std::uint32_t replacementCount = 64;
        auto lifecycle = std::move(NavigationWorldLifecycle::Create(2)).Value();
        auto queues = std::move(NavigationRuntimeQueues::Create(QueueDescriptor())).Value();
        const auto destructions = std::make_shared<std::atomic<std::uint32_t>>(0);

        auto active = Activation(1);
        REQUIRE(lifecycle.Stage(active, TestSupport::MakeObservedNavigationBackend(destructions)).HasValue());
        REQUIRE(lifecycle.CommitAtSafePoint(active.scene, active.sceneGeneration).HasValue());

        for (std::uint32_t generation = 2; generation <= replacementCount; ++generation) {
            active = ExecuteReplacementIteration(lifecycle, queues, destructions, active, generation);
            REQUIRE(destructions->load(std::memory_order_relaxed) == generation - 1U);
        }

        lifecycle.BeginShutdown();
        REQUIRE(lifecycle.CollectRetired() == NavigationWorldLifecycleState::Closed);
        REQUIRE(destructions->load(std::memory_order_relaxed) == replacementCount);
        queues.CloseAdmission();
        queues.CloseCompletions();
        REQUIRE(queues.IsDrained());
    }

    TEST_CASE("Navigation queue measured storage matches the admitted allocation-free capacity",
              "[unit][navigation][qualification][allocation]") {
        auto descriptor = QueueDescriptor();
        const auto required = NavigationRuntimeQueues::RequiredStorageBytes(descriptor);
        REQUIRE(required.HasValue());
        descriptor.maximumOwnedBytes = required.Value();
        auto queues = std::move(NavigationRuntimeQueues::Create(descriptor)).Value();

        const auto activation = Activation(1);
        auto lifecycle = std::move(NavigationWorldLifecycle::Create(1)).Value();
        const auto destructions = std::make_shared<std::atomic<std::uint32_t>>(0);
        REQUIRE(lifecycle.Stage(activation, TestSupport::MakeObservedNavigationBackend(destructions)).HasValue());
        REQUIRE(lifecycle.CommitAtSafePoint(activation.scene, activation.sceneGeneration).HasValue());
        const auto lease = std::move(lifecycle.Acquire(activation.world)).Value();

        const std::size_t allocationsBefore = Tests::AllocationProbe::Count();
        for (std::uint32_t sequence = 1; sequence <= descriptor.commandSlots; ++sequence) {
            NavigationRuntimeCommand command{NavigationSubmitPathCommand{.sequence = sequence, .request = Request(activation)}};
            REQUIRE(queues.TryEnqueueCommand(command) == NavigationQueueEnqueueResult::Enqueued);
        }
        for (std::uint32_t sequence = 1; sequence <= descriptor.commandSlots; ++sequence)
            REQUIRE(queues.TryDequeueCommand().has_value());
        for (std::uint32_t sequence = 1; sequence <= descriptor.querySlots; ++sequence) {
            NavigationQueuedQuery query{
                .acceptedSequence = sequence,
                .handle = RequestHandle(activation.world, sequence),
                .request = Request(activation),
                .worldLease = lease,
            };
            REQUIRE(queues.TryEnqueueQuery(query) == NavigationQueueEnqueueResult::Enqueued);
        }
        for (std::uint32_t sequence = 1; sequence <= descriptor.querySlots; ++sequence)
            REQUIRE(queues.TryDequeueQuery().has_value());
        for (std::uint32_t sequence = 1; sequence <= descriptor.completionSlots; ++sequence) {
            NavigationQueuedCompletion completion{
                .acceptedSequence = sequence,
                .handle = RequestHandle(activation.world, sequence),
                .scene = activation.scene,
                .sceneGeneration = activation.sceneGeneration,
                .world = activation.world,
                .topology = activation.topology,
                .outcome = NavigationCancelled{},
            };
            REQUIRE(queues.TryEnqueueCompletion(completion) == NavigationQueueEnqueueResult::Enqueued);
        }
        for (std::uint32_t sequence = 1; sequence <= descriptor.completionSlots; ++sequence)
            REQUIRE(queues.TryDequeueCompletion().has_value());
        REQUIRE(Tests::AllocationProbe::Count() == allocationsBefore);
        REQUIRE(queues.Stats().commands.enqueued == descriptor.commandSlots);
        REQUIRE(queues.Stats().queries.enqueued == descriptor.querySlots);
        REQUIRE(queues.Stats().completions.enqueued == descriptor.completionSlots);
    }
}  // namespace Horo::Navigation
