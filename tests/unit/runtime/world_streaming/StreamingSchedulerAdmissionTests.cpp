#include "Horo/WorldStreaming/StreamingSchedulerAdmission.h"
#include "Horo/WorldStreaming/WorldStreamingErrors.h"
#include "WorldStreamingTestUtils.h"

#include <catch2/catch_test_macros.hpp>

namespace Horo::WorldStreaming {
    namespace {
        using TestSupport::IdentityFrom;
        using TestSupport::Layer;
        using TestSupport::RequireError;
        using TestSupport::World;

        [[nodiscard]] StreamingCellOperation QueuedOperation(const std::uint64_t operation, const std::uint64_t generation = 1) {
            return StreamingCellOperation::Create({
                                                      .operation = IdentityFrom<StreamingCellOperationId>(operation),
                                                      .fence =
                                                          {
                                                              .partition = World(),
                                                              .epoch = IdentityFrom<PartitionEpoch>(1),
                                                              .cell = {1, 2, 3, 0, Layer()},
                                                              .generation = IdentityFrom<StreamingGeneration>(generation),
                                                          },
                                                  })
                .Value();
        }

        [[nodiscard]] StreamingCellOperation Finish(StreamingCellOperation operation) {
            operation = operation.Advance(operation.Handle(), StreamingCellOperationTransition::BeginPreparation).Value();
            operation = operation.Advance(operation.Handle(), StreamingCellOperationTransition::BeginActivation).Value();
            return operation.Advance(operation.Handle(), StreamingCellOperationTransition::Complete).Value();
        }

        [[nodiscard]] StreamingCellOperation Retire(StreamingCellOperation operation, const StreamingCellOperationTransition reason) {
            operation = operation.Advance(operation.Handle(), reason).Value();
            return operation.Advance(operation.Handle(), StreamingCellOperationTransition::AcknowledgeRetirement).Value();
        }

        TEST_CASE("Scheduler admission reserves capacity before advancing work", "[unit][world_streaming][scheduler]") {
            auto ledger = StreamingSchedulerAdmissionLedger::Create(2).Value();
            const auto queued = QueuedOperation(10);
            const auto admission = ledger.TryAdmit(queued).Value();

            REQUIRE(ledger.Capacity() == 2);
            REQUIRE(ledger.ReservedCount() == 1);
            REQUIRE(ledger.State() == StreamingSchedulerAdmissionState::Accepting);
            REQUIRE(queued.State() == StreamingCellOperationState::Queued);
            REQUIRE(admission.operation.State() == StreamingCellOperationState::Admitted);
            REQUIRE(admission.reservation.IsValid());
            REQUIRE(admission.reservation.operation == queued.Handle());

            const auto completed = Finish(admission.operation);
            REQUIRE(ledger.Release(admission.reservation, completed).HasValue());
            REQUIRE(ledger.ReservedCount() == 0);
        }

        TEST_CASE("Scheduler rejection and duplicate admission leave the ledger unchanged", "[unit][world_streaming][scheduler]") {
            RequireError(StreamingSchedulerAdmissionLedger::Create(0), WorldStreamingErrors::SchedulerAdmissionInvalid);
            auto ledger = StreamingSchedulerAdmissionLedger::Create(2).Value();
            const auto first = ledger.TryAdmit(QueuedOperation(10)).Value();

            RequireError(ledger.TryAdmit(QueuedOperation(10)), WorldStreamingErrors::SchedulerReservationConflict);
            RequireError(ledger.TryAdmit(first.operation), WorldStreamingErrors::SchedulerAdmissionInvalid);
            REQUIRE(ledger.ReservedCount() == 1);
        }

        TEST_CASE("Scheduler capacity rejection never partially admits an operation", "[unit][world_streaming][scheduler]") {
            auto ledger = StreamingSchedulerAdmissionLedger::Create(1).Value();
            const auto first = ledger.TryAdmit(QueuedOperation(10)).Value();
            const auto rejected = QueuedOperation(11);

            RequireError(ledger.TryAdmit(rejected), WorldStreamingErrors::SchedulerCapacityExceeded);
            REQUIRE(ledger.ReservedCount() == 1);
            REQUIRE(rejected.State() == StreamingCellOperationState::Queued);

            REQUIRE(ledger.Release(first.reservation, Finish(first.operation)).HasValue());
            REQUIRE(ledger.TryAdmit(rejected).HasValue());
        }

        TEST_CASE("Scheduler retains cancelled failed and replaced reservations until retirement acknowledgement",
                  "[unit][world_streaming][scheduler][retirement]") {
            for (const auto reason : {StreamingCellOperationTransition::Cancel, StreamingCellOperationTransition::Fail,
                                      StreamingCellOperationTransition::Replace}) {
                auto ledger = StreamingSchedulerAdmissionLedger::Create(1).Value();
                const auto admission = ledger.TryAdmit(QueuedOperation(10)).Value();
                const auto retiring = admission.operation.Advance(admission.operation.Handle(), reason).Value();

                RequireError(ledger.Release(admission.reservation, retiring), WorldStreamingErrors::SchedulerLifecycleUnavailable);
                REQUIRE(ledger.ReservedCount() == 1);
                REQUIRE(ledger.Release(admission.reservation, Retire(admission.operation, reason)).HasValue());
                REQUIRE(ledger.ReservedCount() == 0);
            }
        }

        TEST_CASE("Scheduler rejects malformed and stale releases without changing reservations",
                  "[unit][world_streaming][scheduler][fence]") {
            auto ledger = StreamingSchedulerAdmissionLedger::Create(2).Value();
            const auto first = ledger.TryAdmit(QueuedOperation(10)).Value();
            const auto second = ledger.TryAdmit(QueuedOperation(11, 2)).Value();
            const auto completed = Finish(first.operation);

            RequireError(ledger.Release({}, completed), WorldStreamingErrors::SchedulerAdmissionInvalid);
            RequireError(ledger.Release(second.reservation, completed), WorldStreamingErrors::SchedulerReservationStale);
            auto forged = first.reservation;
            forged.operation = second.reservation.operation;
            RequireError(ledger.Release(forged, completed), WorldStreamingErrors::SchedulerAdmissionInvalid);
            REQUIRE(ledger.ReservedCount() == 2);
        }

        TEST_CASE("Scheduler shutdown closes admission and drains exact retained operations",
                  "[unit][world_streaming][scheduler][shutdown]") {
            auto empty = StreamingSchedulerAdmissionLedger::Create(1).Value();
            empty.BeginShutdown();
            REQUIRE(empty.State() == StreamingSchedulerAdmissionState::Closed);
            RequireError(empty.TryAdmit(QueuedOperation(10)), WorldStreamingErrors::SchedulerLifecycleUnavailable);

            auto ledger = StreamingSchedulerAdmissionLedger::Create(1).Value();
            const auto admission = ledger.TryAdmit(QueuedOperation(20)).Value();
            ledger.BeginShutdown();
            REQUIRE(ledger.State() == StreamingSchedulerAdmissionState::Draining);
            RequireError(ledger.TryAdmit(QueuedOperation(21)), WorldStreamingErrors::SchedulerLifecycleUnavailable);

            const auto terminal = Retire(admission.operation, StreamingCellOperationTransition::Shutdown);
            REQUIRE(ledger.Release(admission.reservation, terminal).HasValue());
            REQUIRE(ledger.State() == StreamingSchedulerAdmissionState::Closed);
            REQUIRE(ledger.ReservedCount() == 0);
            ledger.BeginShutdown();
            REQUIRE(ledger.State() == StreamingSchedulerAdmissionState::Closed);
        }
    }  // namespace
}  // namespace Horo::WorldStreaming
