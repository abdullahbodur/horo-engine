#include "Horo/WorldStreaming/StreamingCellOperation.h"
#include "Horo/WorldStreaming/WorldStreamingErrors.h"
#include "WorldStreamingTestUtils.h"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <utility>

namespace Horo::WorldStreaming {
    namespace {
        using TestSupport::IdentityFrom;
        using TestSupport::RequireError;

        constexpr auto InterruptionCases = std::to_array<std::pair<StreamingCellOperationTransition, StreamingCellOperationOutcome>>({
            {StreamingCellOperationTransition::Cancel, StreamingCellOperationOutcome::Cancelled},
            {StreamingCellOperationTransition::Fail, StreamingCellOperationOutcome::Failed},
            {StreamingCellOperationTransition::Replace, StreamingCellOperationOutcome::Replaced},
            {StreamingCellOperationTransition::Shutdown, StreamingCellOperationOutcome::Shutdown},
        });

        [[nodiscard]] StreamingCellOperationHandle Handle(const std::uint64_t operation = 7, const std::uint64_t generation = 5) {
            return {
                .operation = IdentityFrom<StreamingCellOperationId>(operation),
                .fence =
                    {
                        .partition = WorldPartitionId::Create(
                                         {0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f})
                                         .Value(),
                        .epoch = IdentityFrom<PartitionEpoch>(3),
                        .cell = {1, -2, 3, 0, StreamingLayerId::Create(2).Value()},
                        .generation = IdentityFrom<StreamingGeneration>(generation),
                    },
            };
        }

        [[nodiscard]] StreamingCellOperation Advance(StreamingCellOperation operation, const StreamingCellOperationTransition transition) {
            return operation.Advance(operation.Handle(), transition).Value();
        }

        [[nodiscard]] StreamingCellOperation Accepted(const StreamingCellOperationState state) {
            auto operation = StreamingCellOperation::Create(Handle()).Value();
            operation = Advance(std::move(operation), StreamingCellOperationTransition::Admit);
            if (state == StreamingCellOperationState::Admitted)
                return operation;
            operation = Advance(std::move(operation), StreamingCellOperationTransition::BeginPreparation);
            if (state == StreamingCellOperationState::Preparing)
                return operation;
            return Advance(std::move(operation), StreamingCellOperationTransition::BeginActivation);
        }

        TEST_CASE("Cell operation completes the explicit normal lifecycle", "[unit][world_streaming][cell_operation]") {
            auto operation = StreamingCellOperation::Create(Handle()).Value();
            REQUIRE(operation.State() == StreamingCellOperationState::Queued);
            REQUIRE(operation.Outcome() == StreamingCellOperationOutcome::None);

            operation = Advance(std::move(operation), StreamingCellOperationTransition::Admit);
            REQUIRE(operation.State() == StreamingCellOperationState::Admitted);
            operation = Advance(std::move(operation), StreamingCellOperationTransition::BeginPreparation);
            REQUIRE(operation.State() == StreamingCellOperationState::Preparing);
            operation = Advance(std::move(operation), StreamingCellOperationTransition::BeginActivation);
            REQUIRE(operation.State() == StreamingCellOperationState::Activating);
            operation = Advance(std::move(operation), StreamingCellOperationTransition::Complete);
            REQUIRE(operation.State() == StreamingCellOperationState::Terminal);
            REQUIRE(operation.Outcome() == StreamingCellOperationOutcome::Succeeded);
            REQUIRE(operation.IsTerminal());
        }

        TEST_CASE("Queued interruption terminates without a false retirement barrier", "[unit][world_streaming][cell_operation]") {
            for (const auto [transition, outcome] : InterruptionCases) {
                const auto terminal = Advance(StreamingCellOperation::Create(Handle()).Value(), transition);
                REQUIRE(terminal.State() == StreamingCellOperationState::Terminal);
                REQUIRE(terminal.Outcome() == outcome);
            }
        }

        TEST_CASE("Accepted interruption remains retiring until its exact acknowledgement",
                  "[unit][world_streaming][cell_operation][retirement]") {
            for (const auto state :
                 {StreamingCellOperationState::Admitted, StreamingCellOperationState::Preparing, StreamingCellOperationState::Activating}) {
                for (const auto [transition, outcome] : InterruptionCases) {
                    auto retiring = Advance(Accepted(state), transition);
                    REQUIRE(retiring.State() == StreamingCellOperationState::Retiring);
                    REQUIRE(retiring.Outcome() == outcome);
                    REQUIRE_FALSE(retiring.IsTerminal());
                    retiring = Advance(std::move(retiring), StreamingCellOperationTransition::AcknowledgeRetirement);
                    REQUIRE(retiring.State() == StreamingCellOperationState::Terminal);
                    REQUIRE(retiring.Outcome() == outcome);
                }
            }
        }

        TEST_CASE("Cell operation rejects invalid stale unsupported and skipped transitions without mutation",
                  "[unit][world_streaming][cell_operation]") {
            REQUIRE_FALSE(StreamingCellOperationHandle{}.IsValid());
            RequireError(StreamingCellOperation::Create({}), WorldStreamingErrors::CellOperationInvalid);

            const auto operation = StreamingCellOperation::Create(Handle()).Value();
            RequireError(operation.Advance({}, StreamingCellOperationTransition::Admit), WorldStreamingErrors::CellOperationInvalid);
            RequireError(operation.Advance(Handle(8), StreamingCellOperationTransition::Admit), WorldStreamingErrors::CellOperationStale);
            RequireError(operation.Advance(operation.Handle(), static_cast<StreamingCellOperationTransition>(255)),
                         WorldStreamingErrors::CellOperationUnsupported);
            RequireError(operation.Advance(operation.Handle(), StreamingCellOperationTransition::BeginActivation),
                         WorldStreamingErrors::CellOperationTransitionInvalid);
            REQUIRE(operation.State() == StreamingCellOperationState::Queued);
            REQUIRE(operation.Outcome() == StreamingCellOperationOutcome::None);
        }

        TEST_CASE("Retired attempt acknowledgement reclaims only its exact old operation",
                  "[unit][world_streaming][cell_operation][fence]") {
            auto oldAttempt = Advance(Accepted(StreamingCellOperationState::Preparing), StreamingCellOperationTransition::Replace);
            const auto replacement = StreamingCellOperation::Create(Handle(8, 6)).Value();

            RequireError(oldAttempt.Advance(replacement.Handle(), StreamingCellOperationTransition::AcknowledgeRetirement),
                         WorldStreamingErrors::CellOperationStale);
            const auto retired = oldAttempt.Advance(oldAttempt.Handle(), StreamingCellOperationTransition::AcknowledgeRetirement).Value();
            REQUIRE(retired.State() == StreamingCellOperationState::Terminal);
            REQUIRE(retired.Outcome() == StreamingCellOperationOutcome::Replaced);
            REQUIRE(replacement.State() == StreamingCellOperationState::Queued);
        }

        TEST_CASE("Terminal cell operations reject duplicate completions and acknowledgements", "[unit][world_streaming][cell_operation]") {
            const auto terminal = Advance(Accepted(StreamingCellOperationState::Activating), StreamingCellOperationTransition::Complete);
            RequireError(terminal.Advance(terminal.Handle(), StreamingCellOperationTransition::Complete),
                         WorldStreamingErrors::CellOperationTransitionInvalid);
            RequireError(terminal.Advance(terminal.Handle(), StreamingCellOperationTransition::AcknowledgeRetirement),
                         WorldStreamingErrors::CellOperationTransitionInvalid);
            REQUIRE(terminal.Outcome() == StreamingCellOperationOutcome::Succeeded);
        }
    }  // namespace
}  // namespace Horo::WorldStreaming
