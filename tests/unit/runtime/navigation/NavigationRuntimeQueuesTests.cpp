#include "AllocationProbe.h"
#include "Horo/Navigation/NavigationRuntimeQueues.h"
#include "navigation/NavigationRuntimeTestFixtures.h"

#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <thread>
#include <vector>

namespace Horo::Navigation {
    namespace {
        using TestSupport::Activation;
        using TestSupport::QueueDescriptor;
        using TestSupport::Request;
        using TestSupport::RequestHandle;
        using TestSupport::Topology;
        using TestSupport::World;

        [[nodiscard]] NavigationWorldReadLease ActiveLease(NavigationWorldLifecycle &lifecycle) {
            const auto activation = Activation();
            REQUIRE(lifecycle.Stage(activation, TestSupport::MakeObservedNavigationBackend()).HasValue());
            REQUIRE(lifecycle.CommitAtSafePoint(activation.scene, activation.sceneGeneration).HasValue());
            return std::move(lifecycle.Acquire(activation.world)).Value();
        }
    }  // namespace

    TEST_CASE("Navigation runtime queue preparation validates every finite bound", "[unit][navigation][queue]") {
        auto invalid = QueueDescriptor();
        invalid.commandSlots = 3;
        REQUIRE(NavigationRuntimeQueues::Create(invalid).HasError());
        invalid = QueueDescriptor();
        const auto required = NavigationRuntimeQueues::RequiredStorageBytes(invalid);
        REQUIRE(required.HasValue());
        invalid.maximumOwnedBytes = required.Value() - 1;
        REQUIRE(NavigationRuntimeQueues::Create(invalid).HasError());
        invalid.maximumOwnedBytes = required.Value();
        REQUIRE(NavigationRuntimeQueues::Create(invalid).HasValue());
        static_assert(NavigationRuntimeCommandRecordBytes == sizeof(NavigationRuntimeCommand));
        static_assert(NavigationRuntimeQueryRecordBytes == sizeof(NavigationQueuedQuery));
        static_assert(NavigationRuntimeCompletionRecordBytes == sizeof(NavigationQueuedCompletion));

        auto minimum = NavigationRuntimeQueues::Create(QueueDescriptor(2));
        REQUIRE(minimum.HasValue());
        REQUIRE_FALSE(minimum.Value().IsDrained());
    }

    TEST_CASE("Command admission is bounded ordered and retains rejected records", "[unit][navigation][queue]") {
        auto queues = std::move(NavigationRuntimeQueues::Create(QueueDescriptor(2))).Value();
        NavigationRuntimeCommand first{NavigationSubmitPathCommand{.sequence = 1, .request = Request()}};
        NavigationRuntimeCommand second{NavigationSubmitPathCommand{.sequence = 2, .request = Request()}};
        NavigationRuntimeCommand overflow{NavigationSubmitPathCommand{.sequence = 3, .request = Request()}};
        REQUIRE(queues.TryEnqueueCommand(first) == NavigationQueueEnqueueResult::Enqueued);
        REQUIRE(queues.TryEnqueueCommand(second) == NavigationQueueEnqueueResult::Enqueued);
        REQUIRE(queues.TryEnqueueCommand(overflow) == NavigationQueueEnqueueResult::Full);
        REQUIRE(std::get<NavigationSubmitPathCommand>(overflow).sequence == 3);

        REQUIRE(std::get<NavigationSubmitPathCommand>(*queues.TryDequeueCommand()).sequence == 1);
        REQUIRE(std::get<NavigationSubmitPathCommand>(*queues.TryDequeueCommand()).sequence == 2);
        REQUIRE_FALSE(queues.TryDequeueCommand().has_value());

        const auto stats = queues.Stats().commands;
        REQUIRE(stats.enqueued == 2);
        REQUIRE(stats.dequeued == 2);
        REQUIRE(stats.rejectedFull == 1);
    }

    TEST_CASE("Invalid and closed queue directions preserve caller ownership", "[unit][navigation][queue][lifecycle]") {
        auto queues = std::move(NavigationRuntimeQueues::Create(QueueDescriptor())).Value();
        NavigationRuntimeCommand invalid{NavigationSubmitPathCommand{.sequence = 0, .request = Request()}};
        REQUIRE(queues.TryEnqueueCommand(invalid) == NavigationQueueEnqueueResult::InvalidRecord);
        NavigationRuntimeCommand cancellation{NavigationCancelRequestCommand{.sequence = 3, .handle = RequestHandle(), .world = World()}};
        REQUIRE(queues.TryEnqueueCommand(cancellation) == NavigationQueueEnqueueResult::Enqueued);
        const auto transferredCancellation = queues.TryDequeueCommand();
        REQUIRE(transferredCancellation.has_value());
        REQUIRE(std::get<NavigationCancelRequestCommand>(*transferredCancellation).handle == RequestHandle());

        queues.CloseAdmission();
        queues.CloseAdmission();
        NavigationRuntimeCommand retained{NavigationSubmitPathCommand{.sequence = 4, .request = Request()}};
        REQUIRE(queues.TryEnqueueCommand(retained) == NavigationQueueEnqueueResult::Closed);
        REQUIRE(std::get<NavigationSubmitPathCommand>(retained).sequence == 4);
        REQUIRE(queues.Stats().commands.rejectedClosed == 1);
        REQUIRE_FALSE(queues.IsDrained());

        queues.CloseCompletions();
        REQUIRE(queues.IsDrained());
    }

    TEST_CASE("Queries pin the exact world and completions retain exact publication fences", "[unit][navigation][queue][lifecycle]") {
        auto lifecycle = std::move(NavigationWorldLifecycle::Create(2)).Value();
        auto lease = ActiveLease(lifecycle);
        auto queues = std::move(NavigationRuntimeQueues::Create(QueueDescriptor())).Value();
        NavigationQueuedQuery query{
            .acceptedSequence = 6,
            .handle = RequestHandle(),
            .request = Request(),
            .worldLease = lease,
        };
        REQUIRE(queues.TryEnqueueQuery(query) == NavigationQueueEnqueueResult::Enqueued);
        auto transferred = queues.TryDequeueQuery();
        REQUIRE(transferred.has_value());
        REQUIRE(transferred->worldLease.Descriptor() == Activation());

        const auto activation = Activation();
        NavigationQueuedCompletion completion{
            .acceptedSequence = transferred->acceptedSequence,
            .handle = transferred->handle,
            .scene = activation.scene,
            .sceneGeneration = activation.sceneGeneration,
            .world = activation.world,
            .topology = activation.topology,
            .outcome = NavigationCancelled{},
        };
        REQUIRE(queues.TryEnqueueCompletion(completion) == NavigationQueueEnqueueResult::Enqueued);
        auto received = queues.TryDequeueCompletion();
        REQUIRE(received.has_value());
        REQUIRE(received->world == activation.world);
        REQUIRE(received->topology == activation.topology);
        REQUIRE(GetNavigationOutcomeKind(received->outcome) == NavigationOutcomeKind::Cancelled);

        auto foreign = NavigationQueuedCompletion{
            .acceptedSequence = 7,
            .handle = RequestHandle(),
            .scene = activation.scene,
            .sceneGeneration = activation.sceneGeneration,
            .world = World(88),
            .topology = activation.topology,
            .outcome = NavigationCancelled{},
        };
        REQUIRE(queues.TryEnqueueCompletion(foreign) == NavigationQueueEnqueueResult::InvalidRecord);

        lifecycle.BeginShutdown();
        REQUIRE(transferred->worldLease.IsRevoked());
        REQUIRE(transferred->worldLease.Cancellation().IsCancellationRequested());
    }

    TEST_CASE("Queue operations allocate no storage after preparation", "[unit][navigation][queue][allocation]") {
        auto queues = std::move(NavigationRuntimeQueues::Create(QueueDescriptor())).Value();
        NavigationRuntimeCommand command{NavigationCancelRequestCommand{.sequence = 1, .handle = RequestHandle(), .world = World()}};
        const auto allocationsBefore = Tests::AllocationProbe::Count();
        const auto enqueueResult = queues.TryEnqueueCommand(command);
        auto dequeued = queues.TryDequeueCommand();
        const auto allocationsAfter = Tests::AllocationProbe::Count();
        REQUIRE(enqueueResult == NavigationQueueEnqueueResult::Enqueued);
        REQUIRE(dequeued.has_value());
        REQUIRE(allocationsAfter == allocationsBefore);
    }

    TEST_CASE("Concurrent producers publish unique records without blocking or loss", "[unit][navigation][queue][concurrency]") {
        auto queues = std::move(NavigationRuntimeQueues::Create(QueueDescriptor(64))).Value();
        std::atomic<std::uint32_t> accepted{};
        std::vector<std::thread> producers;
        producers.reserve(4);
        for (std::uint64_t producer = 0; producer < 4; ++producer) {
            producers.emplace_back([producer, &queues, &accepted] {
                for (std::uint64_t index = 0; index < 8; ++index) {
                    NavigationRuntimeCommand command{
                        NavigationSubmitPathCommand{.sequence = producer * 8 + index + 1, .request = Request()}};
                    if (queues.TryEnqueueCommand(command) == NavigationQueueEnqueueResult::Enqueued)
                        accepted.fetch_add(1);
                }
            });
        }
        for (auto &producer : producers)
            producer.join();

        std::uint32_t consumed{};
        while (queues.TryDequeueCommand())
            ++consumed;
        REQUIRE(accepted.load() == 32);
        REQUIRE(consumed == 32);
    }
}  // namespace Horo::Navigation
