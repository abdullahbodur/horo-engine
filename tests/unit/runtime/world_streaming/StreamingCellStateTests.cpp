#include "Horo/WorldStreaming/StreamingCellState.h"
#include "Horo/WorldStreaming/WorldStreamingErrors.h"
#include "WorldStreamingTestUtils.h"

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <limits>
#include <type_traits>
#include <utility>

namespace Horo::WorldStreaming {
    namespace {
        using TestSupport::IdentityFrom;
        using TestSupport::Layer;
        using TestSupport::RequireError;
        using TestSupport::World;

        StreamingRuntimeOwnerToken Owner(const std::uint64_t identity = 7, const std::uint64_t epoch = 1) {
            return {.partition = World(),
                    .epoch = IdentityFrom<PartitionEpoch>(epoch),
                    .owner = IdentityFrom<StreamingRuntimeOwnerId>(identity)};
        }

        StreamingFence Fence(const std::uint64_t generation, const std::int32_t x = 1, const std::uint64_t epoch = 1) {
            return {.partition = World(),
                    .epoch = IdentityFrom<PartitionEpoch>(epoch),
                    .cell = {x, 2, 3, 0, Layer()},
                    .generation = IdentityFrom<StreamingGeneration>(generation)};
        }

        StreamingCellOperation Operation(const std::uint64_t operation, const StreamingCellOperationKind kind,
                                         const StreamingFence &fence) {
            return StreamingCellOperation::Create({.operation = IdentityFrom<StreamingCellOperationId>(operation), .fence = fence}, kind)
                .Value();
        }

        void Advance(StreamingCellOperation &operation, const StreamingCellOperationTransition transition) {
            operation = operation.Advance(operation.Handle(), transition).Value();
        }

        StreamingCellStateLedger Ledger(const std::uint32_t capacity = 4) {
            auto result = StreamingCellStateLedger::Create({.owner = Owner(), .maximumTrackedAttempts = capacity});
            REQUIRE(result.HasValue());
            return std::move(result).Value();
        }

        static_assert(!std::is_copy_constructible_v<StreamingCellStateLedger>);
        static_assert(!std::is_copy_assignable_v<StreamingCellStateLedger>);
        static_assert(static_cast<std::uint8_t>(StreamingCellState::Failed) == 5);

        TEST_CASE("Cell residency follows the canonical load activate and acknowledged retire path",
                  "[unit][world_streaming][cell_state]") {
            auto ledger = Ledger();
            const auto fence = Fence(1);
            RequireError(ledger.Resolve(Owner(), fence), WorldStreamingErrors::CellStateUnresolved);

            auto load = Operation(10, StreamingCellOperationKind::Load, fence);
            RequireError(ledger.Apply(Owner(), load), WorldStreamingErrors::CellStateUnsupported);
            Advance(load, StreamingCellOperationTransition::Admit);
            const auto admitted = ledger.Apply(Owner(), load).Value();
            REQUIRE(admitted.IsValid());
            REQUIRE(admitted.state == StreamingCellState::Loading);
            Advance(load, StreamingCellOperationTransition::BeginPreparation);
            REQUIRE(ledger.Apply(Owner(), load).Value().state == StreamingCellState::Loading);
            Advance(load, StreamingCellOperationTransition::Complete);
            REQUIRE(ledger.Apply(Owner(), load).Value().state == StreamingCellState::Resident);

            auto activate = Operation(11, StreamingCellOperationKind::Activate, fence);
            Advance(activate, StreamingCellOperationTransition::Admit);
            REQUIRE(ledger.Apply(Owner(), activate).Value().state == StreamingCellState::Resident);
            Advance(activate, StreamingCellOperationTransition::BeginPreparation);
            REQUIRE(ledger.Apply(Owner(), activate).Value().state == StreamingCellState::Resident);
            Advance(activate, StreamingCellOperationTransition::BeginActivation);
            REQUIRE(ledger.Apply(Owner(), activate).Value().state == StreamingCellState::Resident);
            Advance(activate, StreamingCellOperationTransition::Complete);
            REQUIRE(ledger.Apply(Owner(), activate).Value().state == StreamingCellState::Active);

            auto retire = Operation(12, StreamingCellOperationKind::Retire, fence);
            Advance(retire, StreamingCellOperationTransition::Admit);
            RequireError(ledger.Apply(Owner(), retire), WorldStreamingErrors::CellStateUnsupported);
            Advance(retire, StreamingCellOperationTransition::BeginRetirement);
            REQUIRE(ledger.Apply(Owner(), retire).Value().state == StreamingCellState::Evicting);
            REQUIRE(ledger.Resolve(Owner(), fence).Value().state == StreamingCellState::Evicting);
            Advance(retire, StreamingCellOperationTransition::AcknowledgeRetirement);
            REQUIRE(ledger.Apply(Owner(), retire).Value().state == StreamingCellState::Unloaded);
            REQUIRE(ledger.Resolve(Owner(), fence).Value().state == StreamingCellState::Unloaded);
        }

        TEST_CASE("Cell failure remains evicting until acknowledgement and retry requires a fresh generation",
                  "[unit][world_streaming][cell_state][lifecycle]") {
            auto ledger = Ledger();
            auto failedLoad = Operation(20, StreamingCellOperationKind::Load, Fence(1));
            Advance(failedLoad, StreamingCellOperationTransition::Admit);
            REQUIRE(ledger.Apply(Owner(), failedLoad).Value().state == StreamingCellState::Loading);
            Advance(failedLoad, StreamingCellOperationTransition::Fail);
            REQUIRE(ledger.Apply(Owner(), failedLoad).Value().state == StreamingCellState::Evicting);
            REQUIRE(ledger.Resolve(Owner(), Fence(1)).Value().state == StreamingCellState::Evicting);
            Advance(failedLoad, StreamingCellOperationTransition::AcknowledgeRetirement);
            REQUIRE(ledger.Apply(Owner(), failedLoad).Value().state == StreamingCellState::Failed);

            auto staleRetry = Operation(21, StreamingCellOperationKind::Load, Fence(1));
            Advance(staleRetry, StreamingCellOperationTransition::Admit);
            RequireError(ledger.Apply(Owner(), staleRetry), WorldStreamingErrors::CellStateStale);

            auto retry = Operation(22, StreamingCellOperationKind::Load, Fence(2));
            Advance(retry, StreamingCellOperationTransition::Admit);
            REQUIRE(ledger.Apply(Owner(), retry).Value().state == StreamingCellState::Loading);
            REQUIRE(ledger.TrackedAttemptCount() == 1);
            RequireError(ledger.Resolve(Owner(), Fence(1)), WorldStreamingErrors::CellStateStale);

            Advance(retry, StreamingCellOperationTransition::Cancel);
            REQUIRE(ledger.Apply(Owner(), retry).Value().state == StreamingCellState::Evicting);
            Advance(retry, StreamingCellOperationTransition::AcknowledgeRetirement);
            REQUIRE(ledger.Apply(Owner(), retry).Value().state == StreamingCellState::Unloaded);

            auto exhaustedLedger = Ledger();
            auto exhausted = Operation(23, StreamingCellOperationKind::Load, Fence(std::numeric_limits<std::uint64_t>::max(), 4));
            Advance(exhausted, StreamingCellOperationTransition::Admit);
            REQUIRE(exhaustedLedger.Apply(Owner(), exhausted).HasValue());
            Advance(exhausted, StreamingCellOperationTransition::Fail);
            REQUIRE(exhaustedLedger.Apply(Owner(), exhausted).Value().state == StreamingCellState::Evicting);
            Advance(exhausted, StreamingCellOperationTransition::AcknowledgeRetirement);
            REQUIRE(exhaustedLedger.Apply(Owner(), exhausted).Value().state == StreamingCellState::Failed);
            auto wrapped = Operation(24, StreamingCellOperationKind::Load, Fence(1, 4));
            Advance(wrapped, StreamingCellOperationTransition::Admit);
            RequireError(exhaustedLedger.Apply(Owner(), wrapped), WorldStreamingErrors::CellStateStale);
        }

        TEST_CASE("Old retirement remains independently fenced while a newer generation progresses",
                  "[unit][world_streaming][cell_state][replacement]") {
            auto ledger = Ledger(2);
            auto oldLoad = Operation(30, StreamingCellOperationKind::Load, Fence(1));
            Advance(oldLoad, StreamingCellOperationTransition::Admit);
            REQUIRE(ledger.Apply(Owner(), oldLoad).HasValue());
            Advance(oldLoad, StreamingCellOperationTransition::Replace);
            REQUIRE(ledger.Apply(Owner(), oldLoad).Value().state == StreamingCellState::Evicting);

            auto currentLoad = Operation(31, StreamingCellOperationKind::Load, Fence(2));
            Advance(currentLoad, StreamingCellOperationTransition::Admit);
            REQUIRE(ledger.Apply(Owner(), currentLoad).Value().state == StreamingCellState::Loading);
            REQUIRE(ledger.TrackedAttemptCount() == 2);

            Advance(oldLoad, StreamingCellOperationTransition::AcknowledgeRetirement);
            REQUIRE(ledger.Apply(Owner(), oldLoad).Value().state == StreamingCellState::Unloaded);
            REQUIRE(ledger.TrackedAttemptCount() == 1);
            REQUIRE(ledger.Resolve(Owner(), Fence(2)).Value().state == StreamingCellState::Loading);
            RequireError(ledger.Resolve(Owner(), Fence(1)), WorldStreamingErrors::CellStateStale);
            RequireError(ledger.Apply(Owner(), oldLoad), WorldStreamingErrors::CellStateStale);
        }

        TEST_CASE("Cell ledger rejects malformed stale illegal and over-capacity input transactionally",
                  "[unit][world_streaming][cell_state]") {
            RequireError(StreamingCellStateLedger::Create({.owner = {}, .maximumTrackedAttempts = 1}),
                         WorldStreamingErrors::CellStateInvalid);
            RequireError(StreamingCellStateLedger::Create({.owner = Owner(), .maximumTrackedAttempts = 0}),
                         WorldStreamingErrors::CellStateInvalid);
            RequireError(StreamingCellStateLedger::Create(
                             {.owner = Owner(), .maximumTrackedAttempts = StreamingCellStateLedgerConfig::MaximumTrackedAttempts + 1}),
                         WorldStreamingErrors::CellStateInvalid);

            auto ledger = Ledger(1);
            auto first = Operation(40, StreamingCellOperationKind::Load, Fence(1));
            Advance(first, StreamingCellOperationTransition::Admit);
            REQUIRE(ledger.Apply(Owner(), first).HasValue());

            auto second = Operation(41, StreamingCellOperationKind::Load, Fence(1, 9));
            Advance(second, StreamingCellOperationTransition::Admit);
            RequireError(ledger.Apply(Owner(), second), WorldStreamingErrors::CellStateCapacityExceeded);
            REQUIRE(ledger.TrackedAttemptCount() == 1);
            RequireError(ledger.Apply(Owner(8), first), WorldStreamingErrors::CellStateStale);

            auto foreignEpoch = Operation(42, StreamingCellOperationKind::Load, Fence(1, 1, 2));
            Advance(foreignEpoch, StreamingCellOperationTransition::Admit);
            RequireError(ledger.Apply(Owner(), foreignEpoch), WorldStreamingErrors::CellStateStale);

            Advance(first, StreamingCellOperationTransition::BeginPreparation);
            Advance(first, StreamingCellOperationTransition::Complete);
            REQUIRE(ledger.Apply(Owner(), first).Value().state == StreamingCellState::Resident);
            auto earlierSnapshot = Operation(40, StreamingCellOperationKind::Load, Fence(1));
            Advance(earlierSnapshot, StreamingCellOperationTransition::Admit);
            RequireError(ledger.Apply(Owner(), earlierSnapshot), WorldStreamingErrors::CellStateTransitionInvalid);
        }

        TEST_CASE("Cell ledger shutdown admits only exact retirement progress and move closes the old owner",
                  "[unit][world_streaming][cell_state][shutdown]") {
            auto ledger = Ledger(2);
            auto load = Operation(50, StreamingCellOperationKind::Load, Fence(1));
            Advance(load, StreamingCellOperationTransition::Admit);
            REQUIRE(ledger.Apply(Owner(), load).HasValue());
            REQUIRE(ledger.BeginShutdown(Owner()).HasValue());
            REQUIRE(ledger.Lifecycle() == StreamingCellStateLedgerLifecycle::Draining);

            auto newLoad = Operation(51, StreamingCellOperationKind::Load, Fence(1, 9));
            Advance(newLoad, StreamingCellOperationTransition::Admit);
            RequireError(ledger.Apply(Owner(), newLoad), WorldStreamingErrors::CellStateLifecycleUnavailable);

            Advance(load, StreamingCellOperationTransition::Shutdown);
            REQUIRE(ledger.Apply(Owner(), load).Value().state == StreamingCellState::Evicting);
            Advance(load, StreamingCellOperationTransition::AcknowledgeRetirement);
            REQUIRE(ledger.Apply(Owner(), load).Value().state == StreamingCellState::Unloaded);
            REQUIRE(ledger.Lifecycle() == StreamingCellStateLedgerLifecycle::Closed);
            REQUIRE(ledger.BeginShutdown(Owner()).HasValue());

            auto moved = std::move(ledger);
            REQUIRE(moved.Lifecycle() == StreamingCellStateLedgerLifecycle::Closed);
            REQUIRE(ledger.Lifecycle() == StreamingCellStateLedgerLifecycle::Closed);
            RequireError(ledger.Apply(Owner(), newLoad), WorldStreamingErrors::CellStateLifecycleUnavailable);
        }
    }  // namespace
}  // namespace Horo::WorldStreaming
