#include "Horo/WorldStreaming/StreamingSchedulerAdmission.h"
#include "Horo/WorldStreaming/WorldStreamingErrors.h"
#include "WorldStreamingTestUtils.h"

#include <catch2/catch_test_macros.hpp>
#include <type_traits>
#include <utility>

namespace Horo::WorldStreaming {
    namespace {
        using TestSupport::IdentityFrom;
        using TestSupport::Layer;
        using TestSupport::RequireError;
        using TestSupport::World;

        [[nodiscard]] StreamingCellOperation QueuedOperation(const std::uint64_t operation, const std::uint64_t generation = 1,
                                                             const StreamingCellOperationKind kind = StreamingCellOperationKind::Activate) {
            return StreamingCellOperation::Create(
                       {
                           .operation = IdentityFrom<StreamingCellOperationId>(operation),
                           .fence =
                               {
                                   .partition = World(),
                                   .epoch = IdentityFrom<PartitionEpoch>(1),
                                   .cell = {1, 2, 3, 0, Layer()},
                                   .generation = IdentityFrom<StreamingGeneration>(generation),
                               },
                       },
                       kind)
                .Value();
        }

        [[nodiscard]] StreamingSchedulerAdmissionLedger CreateLedger(const std::uint64_t owner = 1,
                                                                     const std::uint32_t concurrentOperations = 2,
                                                                     const std::uint64_t capacityUnits = 10) {
            auto result =
                StreamingSchedulerAdmissionLedger::Create(IdentityFrom<StreamingSchedulerLedgerId>(owner),
                                                          {.concurrentOperations = concurrentOperations, .capacityUnits = capacityUnits});
            REQUIRE(result.HasValue());
            return std::move(result).Value();
        }

        void CompleteActivation(StreamingSchedulerAdmissionLedger &ledger, const StreamingSchedulerReservation &reservation) {
            REQUIRE(ledger.Advance(reservation, StreamingCellOperationTransition::BeginPreparation).HasValue());
            REQUIRE(ledger.Advance(reservation, StreamingCellOperationTransition::BeginActivation).HasValue());
            REQUIRE(ledger.Advance(reservation, StreamingCellOperationTransition::Complete).HasValue());
        }

        void Retire(StreamingSchedulerAdmissionLedger &ledger, const StreamingSchedulerReservation &reservation,
                    const StreamingCellOperationTransition reason) {
            REQUIRE(ledger.Advance(reservation, reason).HasValue());
            REQUIRE(ledger.Advance(reservation, StreamingCellOperationTransition::AcknowledgeRetirement).HasValue());
        }

        static_assert(!std::is_copy_constructible_v<StreamingSchedulerAdmissionLedger>);
        static_assert(!std::is_copy_assignable_v<StreamingSchedulerAdmissionLedger>);

        TEST_CASE("Scheduler admission owns canonical state and exact generic capacity", "[unit][world_streaming][scheduler]") {
            auto ledger = CreateLedger();
            const auto queued = QueuedOperation(10);
            const auto reservation = ledger.TryAdmit(queued, 4).Value();

            REQUIRE(ledger.Owner() == IdentityFrom<StreamingSchedulerLedgerId>(1));
            REQUIRE(ledger.Limits() == StreamingSchedulerAdmissionLimits{.concurrentOperations = 2, .capacityUnits = 10});
            REQUIRE(ledger.ReservedCount() == 1);
            REQUIRE(ledger.ReservedCapacityUnits() == 4);
            REQUIRE(queued.State() == StreamingCellOperationState::Queued);
            REQUIRE(reservation.operation == queued.Handle());

            CompleteActivation(ledger, reservation);
            REQUIRE(ledger.Release(reservation).HasValue());
            REQUIRE(ledger.ReservedCount() == 0);
            REQUIRE(ledger.ReservedCapacityUnits() == 0);
        }

        TEST_CASE("Scheduler rejection never partially reserves or admits", "[unit][world_streaming][scheduler]") {
            RequireError(StreamingSchedulerAdmissionLedger::Create({}, {.concurrentOperations = 1, .capacityUnits = 1}),
                         WorldStreamingErrors::SchedulerAdmissionInvalid);
            RequireError(StreamingSchedulerAdmissionLedger::Create(IdentityFrom<StreamingSchedulerLedgerId>(1), {}),
                         WorldStreamingErrors::SchedulerAdmissionInvalid);
            RequireError(StreamingSchedulerAdmissionLedger::Create(IdentityFrom<StreamingSchedulerLedgerId>(1),
                                                                   {.concurrentOperations =
                                                                        StreamingSchedulerAdmissionLimits::MaximumConcurrentOperations + 1,
                                                                    .capacityUnits = 1}),
                         WorldStreamingErrors::SchedulerAdmissionInvalid);

            auto ledger = CreateLedger();
            const auto first = ledger.TryAdmit(QueuedOperation(10), 6).Value();
            const auto rejected = QueuedOperation(11);
            RequireError(ledger.TryAdmit(rejected, 5), WorldStreamingErrors::SchedulerCapacityExceeded);
            RequireError(ledger.TryAdmit(QueuedOperation(10), 1), WorldStreamingErrors::SchedulerReservationConflict);
            RequireError(ledger.TryAdmit(rejected, 0), WorldStreamingErrors::SchedulerAdmissionInvalid);
            REQUIRE(ledger.ReservedCount() == 1);
            REQUIRE(ledger.ReservedCapacityUnits() == 6);
            REQUIRE(rejected.State() == StreamingCellOperationState::Queued);

            CompleteActivation(ledger, first);
            REQUIRE(ledger.Release(first).HasValue());
        }

        TEST_CASE("Scheduler enforces operation and generic-capacity ceilings independently", "[unit][world_streaming][scheduler]") {
            auto ledger = CreateLedger(1, 2, 10);
            const auto first = ledger.TryAdmit(QueuedOperation(10), 6).Value();
            const auto second = ledger.TryAdmit(QueuedOperation(11), 4).Value();
            RequireError(ledger.TryAdmit(QueuedOperation(12), 1), WorldStreamingErrors::SchedulerCapacityExceeded);
            REQUIRE(ledger.ReservedCount() == 2);
            REQUIRE(ledger.ReservedCapacityUnits() == 10);

            CompleteActivation(ledger, first);
            CompleteActivation(ledger, second);
            REQUIRE(ledger.Release(first).HasValue());
            REQUIRE(ledger.Release(second).HasValue());
        }

        TEST_CASE("Caller-held snapshots cannot forge canonical completion", "[unit][world_streaming][scheduler][fence]") {
            auto ledger = CreateLedger();
            const auto queued = QueuedOperation(10);
            const auto reservation = ledger.TryAdmit(queued, 3).Value();
            const auto forgedTerminal = queued.Advance(queued.Handle(), StreamingCellOperationTransition::Cancel).Value();
            REQUIRE(forgedTerminal.IsTerminal());

            RequireError(ledger.Release(reservation), WorldStreamingErrors::SchedulerLifecycleUnavailable);
            Retire(ledger, reservation, StreamingCellOperationTransition::Cancel);
            REQUIRE(ledger.Release(reservation).HasValue());
        }

        TEST_CASE("Scheduler rejects cross-owner stale and malformed reservations", "[unit][world_streaming][scheduler][fence]") {
            auto firstLedger = CreateLedger(1);
            auto secondLedger = CreateLedger(2);
            const auto first = firstLedger.TryAdmit(QueuedOperation(10), 2).Value();
            const auto second = secondLedger.TryAdmit(QueuedOperation(11), 2).Value();

            RequireError(secondLedger.Advance(first, StreamingCellOperationTransition::Cancel),
                         WorldStreamingErrors::SchedulerReservationStale);
            RequireError(secondLedger.Release(first), WorldStreamingErrors::SchedulerReservationStale);
            RequireError(firstLedger.Release({}), WorldStreamingErrors::SchedulerAdmissionInvalid);
            auto forged = first;
            forged.capacityUnits += 1;
            RequireError(firstLedger.Release(forged), WorldStreamingErrors::SchedulerReservationStale);
            REQUIRE(firstLedger.ReservedCount() == 1);
            REQUIRE(secondLedger.ReservedCount() == 1);

            Retire(firstLedger, first, StreamingCellOperationTransition::Cancel);
            Retire(secondLedger, second, StreamingCellOperationTransition::Cancel);
            REQUIRE(firstLedger.Release(first).HasValue());
            REQUIRE(secondLedger.Release(second).HasValue());
        }

        TEST_CASE("Scheduler retains interrupted work through retirement acknowledgement",
                  "[unit][world_streaming][scheduler][retirement]") {
            for (const auto reason : {StreamingCellOperationTransition::Cancel, StreamingCellOperationTransition::Fail,
                                      StreamingCellOperationTransition::Replace}) {
                auto ledger = CreateLedger(1, 1, 1);
                const auto reservation = ledger.TryAdmit(QueuedOperation(10), 1).Value();
                REQUIRE(ledger.Advance(reservation, reason).HasValue());
                RequireError(ledger.Release(reservation), WorldStreamingErrors::SchedulerLifecycleUnavailable);
                REQUIRE(ledger.Advance(reservation, StreamingCellOperationTransition::AcknowledgeRetirement).HasValue());
                REQUIRE(ledger.Release(reservation).HasValue());
            }
        }

        TEST_CASE("Retirement operations use their normal admitted lifecycle", "[unit][world_streaming][scheduler][retirement]") {
            auto ledger = CreateLedger();
            const auto reservation = ledger.TryAdmit(QueuedOperation(10, 1, StreamingCellOperationKind::Retire), 2).Value();
            REQUIRE(ledger.Advance(reservation, StreamingCellOperationTransition::BeginRetirement).HasValue());
            REQUIRE(ledger.Advance(reservation, StreamingCellOperationTransition::AcknowledgeRetirement).HasValue());
            REQUIRE(ledger.Release(reservation).HasValue());
        }

        TEST_CASE("Scheduler shutdown rejects new work and closes after canonical drain", "[unit][world_streaming][scheduler][shutdown]") {
            auto empty = CreateLedger();
            empty.BeginShutdown();
            REQUIRE(empty.State() == StreamingSchedulerAdmissionState::Closed);
            RequireError(empty.TryAdmit(QueuedOperation(10), 1), WorldStreamingErrors::SchedulerLifecycleUnavailable);

            auto ledger = CreateLedger();
            const auto reservation = ledger.TryAdmit(QueuedOperation(20), 5).Value();
            ledger.BeginShutdown();
            REQUIRE(ledger.State() == StreamingSchedulerAdmissionState::Draining);
            RequireError(ledger.TryAdmit(QueuedOperation(21), 1), WorldStreamingErrors::SchedulerLifecycleUnavailable);

            Retire(ledger, reservation, StreamingCellOperationTransition::Shutdown);
            REQUIRE(ledger.Release(reservation).HasValue());
            REQUIRE(ledger.State() == StreamingSchedulerAdmissionState::Closed);
            REQUIRE(ledger.ReservedCount() == 0);
            REQUIRE(ledger.ReservedCapacityUnits() == 0);
        }
    }  // namespace
}  // namespace Horo::WorldStreaming
