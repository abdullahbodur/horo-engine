#include "Horo/Navigation/NavigationRuntimeQueues.h"
#include "support/AllocationProbe.h"

#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <limits>
#include <memory>
#include <thread>
#include <vector>

namespace Horo::Navigation {
    namespace {
        class QueueTestBackend final : public INavigationQueryBackend {
        public:
            [[nodiscard]] NavigationProviderCapabilities Capabilities() const noexcept override {
                return {};
            }

            [[nodiscard]] Result<NavigationPath> FindPath(const NavigationPathRequest &, const CancellationToken &) const override {
                return Result<NavigationPath>::Failure(MakeError(NavigationErrors::NoNavigationData));
            }
        };

        [[nodiscard]] NavigationWorldId World(const std::uint64_t value = 7) {
            return NavigationWorldId::Create(value).Value();
        }

        [[nodiscard]] NavigationGeneration Topology(const std::uint64_t value = 9) {
            return NavigationGeneration::Create(value).Value();
        }

        [[nodiscard]] NavigationPathRequest Request(const NavigationWorldId world = World(),
                                                    const NavigationGeneration topology = Topology()) {
            return {
                .world = world,
                .topology = topology,
                .start = {1.0F, 2.0F, 3.0F},
                .destination = {4.0F, 5.0F, 6.0F},
                .requirement =
                    {
                        .query = NavigationQueryKind::Path,
                        .quality = NavigationQualityLevel::Balanced,
                        .limits = {.maximumNodeExpansions = 64, .maximumResultPoints = 16, .maximumSearchDistanceMeters = 100.0F},
                    },
            };
        }

        [[nodiscard]] NavigationRuntimeQueueDescriptor QueueDescriptor(const std::uint32_t slots = 8) {
            return {
                .commandSlots = slots,
                .querySlots = slots,
                .completionSlots = slots,
                .maximumOwnedBytes = std::numeric_limits<std::size_t>::max(),
            };
        }

        [[nodiscard]] NavRequestHandle RequestHandle(const NavigationWorldId world = World(), const std::uint32_t index = 3,
                                                     const std::uint32_t generation = 2) {
            return {.world = world, .slot = {.index = index, .generation = generation}};
        }

        [[nodiscard]] NavigationWorldActivationDescriptor Activation() {
            return {
                .scene = NavigationSceneRuntimeId::Create(11).Value(),
                .sceneGeneration = NavigationSceneGeneration::Create(12).Value(),
                .world = World(),
                .topology = Topology(),
            };
        }

        [[nodiscard]] NavigationWorldReadLease ActiveLease(NavigationWorldLifecycle &lifecycle) {
            const auto activation = Activation();
            REQUIRE(lifecycle.Stage(activation, std::make_unique<QueueTestBackend>()).HasValue());
            REQUIRE(lifecycle.CommitAtSafePoint(activation.scene, activation.sceneGeneration).HasValue());
            return std::move(lifecycle.Acquire(activation.world)).Value();
        }
    }  // namespace

    TEST_CASE("Navigation runtime queue preparation validates every finite bound", "[unit][navigation][queue]") {
        auto invalid = QueueDescriptor();
        invalid.commandSlots = 3;
        REQUIRE(NavigationRuntimeQueues::Create(invalid).HasError());
        invalid = QueueDescriptor();
        invalid.maximumOwnedBytes = 0;
        REQUIRE(NavigationRuntimeQueues::Create(invalid).HasError());

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
        NavigationRuntimeCommand command{NavigationSubmitPathCommand{.sequence = 1, .request = Request()}};
        const auto allocationsBefore = Tests::AllocationProbe::Count();
        REQUIRE(queues.TryEnqueueCommand(command) == NavigationQueueEnqueueResult::Enqueued);
        auto dequeued = queues.TryDequeueCommand();
        const auto allocationsAfter = Tests::AllocationProbe::Count();
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
