#include "Horo/WorldStreaming/WorldStreamingDiagnosticSnapshot.h"
#include "Horo/WorldStreaming/WorldStreamingErrors.h"
#include "WorldStreamingTestUtils.h"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstdint>
#include <vector>

namespace Horo::WorldStreaming {
    namespace {
        using TestSupport::IdentityFrom;
        using TestSupport::Layer;
        using TestSupport::RequireError;
        using TestSupport::World;

        StreamingRuntimeOwnerToken RuntimeOwner(const std::uint64_t owner = 7, const std::uint64_t epoch = 3) {
            return {.partition = World(),
                    .epoch = IdentityFrom<PartitionEpoch>(epoch),
                    .owner = IdentityFrom<StreamingRuntimeOwnerId>(owner)};
        }

        StreamingFence Fence(const std::uint64_t generation, const std::int32_t x = 1, const std::uint64_t epoch = 3) {
            return {.partition = World(),
                    .epoch = IdentityFrom<PartitionEpoch>(epoch),
                    .cell = {.x = x, .y = 2, .z = 3, .lod = 0, .layer = Layer()},
                    .generation = IdentityFrom<StreamingGeneration>(generation)};
        }

        StreamingCellOperationHandle Operation(const std::uint64_t operation, const std::uint64_t generation, const std::int32_t x = 1,
                                               const std::uint64_t epoch = 3) {
            return {.operation = IdentityFrom<StreamingCellOperationId>(operation), .fence = Fence(generation, x, epoch)};
        }

        StreamingSourceDesiredState Source(const std::uint64_t id, const std::uint64_t revision = 1, const std::uint64_t epoch = 3) {
            const StreamingSourceDescriptor descriptor{
                .id = IdentityFrom<StreamingSourceId>(id),
                .owner = {.partition = World(), .epoch = IdentityFrom<PartitionEpoch>(epoch), .slot = 1, .generation = 1},
                .intent = StreamingSourceIntent::Camera,
                .priority = StreamingSourcePriority::Create(1.0F).Value(),
                .revision = IdentityFrom<StreamingSourceRevision>(revision),
            };
            return StreamingSourceDesiredState::Create(descriptor, StreamingDesiredResidency::Activated, StreamingRetention::Releasable)
                .Value();
        }

        StreamingBudgetPolicy Policy(const std::uint64_t revision = 1) {
            std::array<StreamingBudgetLimit, StreamingBudgetDimensionCount> limits{};
            for (std::size_t index = 0; index < limits.size(); ++index) {
                limits[index] = {.dimension = static_cast<StreamingBudgetDimension>(index), .softTarget = 50, .hardLimit = 100};
            }
            return StreamingBudgetPolicy::Create(IdentityFrom<StreamingBudgetPolicyRevision>(revision), limits, std::chrono::seconds{1})
                .Value();
        }

        StreamingBudgetSample Sample(const StreamingBudgetPolicy &policy, const std::uint64_t revision = 1) {
            std::array<StreamingBudgetAmount, StreamingBudgetDimensionCount> usage{};
            for (std::size_t index = 0; index < usage.size(); ++index)
                usage[index] = {.dimension = static_cast<StreamingBudgetDimension>(index), .value = index};
            return StreamingBudgetSample::Create(policy, IdentityFrom<StreamingBudgetSampleRevision>(revision), std::chrono::nanoseconds{0},
                                                 std::chrono::nanoseconds{10}, StreamingBudgetAmounts::Create(usage).Value())
                .Value();
        }

        WorldStreamingDiagnosticSnapshotInput Input() {
            return {
                .owner = RuntimeOwner(),
                .revision = IdentityFrom<WorldStreamingDiagnosticRevision>(1),
                .lifecycle = WorldStreamingRuntimeCompositionState::Active,
                .limits = {.sources = 4, .cells = 4, .failures = 4},
                .queue = {.owner = IdentityFrom<StreamingSchedulerLedgerId>(8),
                          .limits = {.concurrentOperations = 4, .capacityUnits = 100},
                          .state = StreamingSchedulerAdmissionState::Accepting,
                          .reservedOperations = 2,
                          .reservedCapacityUnits = 30},
            };
        }

        Result<WorldStreamingDiagnosticSnapshot> CreateSnapshot(const WorldStreamingDiagnosticSnapshotInput &input,
                                                                const StreamingBudgetPolicy &policy, const StreamingBudgetSample &sample,
                                                                const std::span<const StreamingSourceDesiredState> sources = {},
                                                                const std::span<const StreamingCellStateRecord> cells = {},
                                                                const std::span<const StreamingDiagnosticFailureRecord> failures = {}) {
            return WorldStreamingDiagnosticSnapshot::Create(input, policy, sample, sources, cells, failures);
        }

        TEST_CASE("World Streaming diagnostics own and canonically order complete authority facts",
                  "[unit][world_streaming][diagnostics]") {
            auto input = Input();
            const auto policy = Policy();
            const auto sample = Sample(policy);
            std::vector sources{Source(20), Source(10)};
            std::vector cells{StreamingCellStateRecord{Operation(22, 2, 4), StreamingCellState::Failed},
                              StreamingCellStateRecord{Operation(11, 1, -2), StreamingCellState::Active}};
            std::vector failures{StreamingDiagnosticFailureRecord{cells.front().operation, StreamingCellOperationOutcome::Failed}};

            const auto snapshotResult = CreateSnapshot(input, policy, sample, sources, cells, failures);
            REQUIRE(snapshotResult.HasValue());
            const auto snapshot = snapshotResult.Value();
            REQUIRE(snapshot.Owner() == input.owner);
            REQUIRE(snapshot.Revision() == input.revision);
            REQUIRE(snapshot.Lifecycle() == WorldStreamingRuntimeCompositionState::Active);
            REQUIRE(snapshot.Queue().reservedOperations == 2);
            REQUIRE(snapshot.BudgetPolicy().Revision() == policy.Revision());
            REQUIRE(snapshot.BudgetSample().Revision() == sample.Revision());
            REQUIRE(snapshot.Sources().size() == 2);
            REQUIRE(snapshot.Sources()[0].Source().id.Value() == 10);
            REQUIRE(snapshot.Cells().size() == 2);
            REQUIRE(snapshot.Cells()[0].operation.fence.cell.x == -2);
            REQUIRE(snapshot.Failures().front().reason == StreamingCellOperationOutcome::Failed);

            sources.clear();
            cells.clear();
            failures.clear();
            REQUIRE(snapshot.Sources().size() == 2);
            REQUIRE(snapshot.Cells().size() == 2);
            REQUIRE(snapshot.Failures().size() == 1);
        }

        TEST_CASE("World Streaming diagnostics preserve valid empty and closed headless snapshots",
                  "[unit][world_streaming][diagnostics][shutdown]") {
            auto input = Input();
            input.lifecycle = WorldStreamingRuntimeCompositionState::Closed;
            input.queue.state = StreamingSchedulerAdmissionState::Closed;
            input.queue.reservedOperations = 0;
            input.queue.reservedCapacityUnits = 0;
            const auto policy = Policy();
            const auto result = CreateSnapshot(input, policy, Sample(policy));
            REQUIRE(result.HasValue());
            REQUIRE(result.Value().Sources().empty());
            REQUIRE(result.Value().Cells().empty());
            REQUIRE(result.Value().Failures().empty());
            REQUIRE(result.Value().Lifecycle() == WorldStreamingRuntimeCompositionState::Closed);
        }

        TEST_CASE("World Streaming diagnostics reject invalid and unsupported aggregate facts transactionally",
                  "[unit][world_streaming][diagnostics]") {
            const auto policy = Policy();
            const auto sample = Sample(policy);
            auto input = Input();
            input.limits.sources = 0;
            RequireError(CreateSnapshot(input, policy, sample), WorldStreamingErrors::DiagnosticProjectionInvalid);

            input = Input();
            input.lifecycle = static_cast<WorldStreamingRuntimeCompositionState>(255);
            RequireError(CreateSnapshot(input, policy, sample), WorldStreamingErrors::DiagnosticProjectionUnsupported);

            input = Input();
            input.queue.reservedOperations = input.queue.limits.concurrentOperations + 1;
            RequireError(CreateSnapshot(input, policy, sample), WorldStreamingErrors::DiagnosticProjectionCapacityExceeded);

            input = Input();
            input.queue.state = StreamingSchedulerAdmissionState::Draining;
            RequireError(CreateSnapshot(input, policy, sample), WorldStreamingErrors::DiagnosticProjectionInvalid);

            input = Input();
            const std::array cells{StreamingCellStateRecord{Operation(1, 1), static_cast<StreamingCellState>(255)}};
            RequireError(CreateSnapshot(input, policy, sample, {}, cells), WorldStreamingErrors::DiagnosticProjectionUnsupported);
            const std::array validCells{StreamingCellStateRecord{Operation(1, 1), StreamingCellState::Failed}};
            const std::array badReason{StreamingDiagnosticFailureRecord{validCells[0].operation, StreamingCellOperationOutcome::Succeeded}};
            RequireError(CreateSnapshot(input, policy, sample, {}, validCells, badReason),
                         WorldStreamingErrors::DiagnosticProjectionUnsupported);
        }

        TEST_CASE("World Streaming diagnostics reject stale partition and budget facts", "[unit][world_streaming][diagnostics][stale]") {
            const auto policy = Policy();
            const auto sample = Sample(policy);
            const auto input = Input();
            const std::array staleSources{Source(1, 1, 4)};
            RequireError(CreateSnapshot(input, policy, sample, staleSources), WorldStreamingErrors::DiagnosticProjectionStale);

            const std::array staleCells{StreamingCellStateRecord{Operation(2, 1, 1, 4), StreamingCellState::Loading}};
            RequireError(CreateSnapshot(input, policy, sample, {}, staleCells), WorldStreamingErrors::DiagnosticProjectionStale);

            const auto otherPolicy = Policy(2);
            RequireError(CreateSnapshot(input, otherPolicy, sample), WorldStreamingErrors::DiagnosticProjectionStale);

            const std::array cells{StreamingCellStateRecord{Operation(3, 1), StreamingCellState::Failed}};
            const std::array orphanFailure{StreamingDiagnosticFailureRecord{Operation(4, 2), StreamingCellOperationOutcome::Failed}};
            RequireError(CreateSnapshot(input, policy, sample, {}, cells, orphanFailure), WorldStreamingErrors::DiagnosticProjectionStale);
        }

        TEST_CASE("World Streaming diagnostic capacity accepts its exact boundary and rejects one more row",
                  "[unit][world_streaming][diagnostics][capacity]") {
            auto input = Input();
            input.limits = {.sources = 2, .cells = 2, .failures = 2};
            const auto policy = Policy();
            const auto sample = Sample(policy);
            const std::array exactSources{Source(1), Source(2)};
            const std::array exactCells{StreamingCellStateRecord{Operation(1, 1, 1), StreamingCellState::Failed},
                                        StreamingCellStateRecord{Operation(2, 1, 2), StreamingCellState::Failed}};
            const std::array exactFailures{StreamingDiagnosticFailureRecord{exactCells[0].operation, StreamingCellOperationOutcome::Failed},
                                           StreamingDiagnosticFailureRecord{exactCells[1].operation,
                                                                            StreamingCellOperationOutcome::Cancelled}};
            REQUIRE(CreateSnapshot(input, policy, sample, exactSources, exactCells, exactFailures).HasValue());

            const std::array tooManySources{Source(1), Source(2), Source(3)};
            RequireError(CreateSnapshot(input, policy, sample, tooManySources), WorldStreamingErrors::DiagnosticProjectionCapacityExceeded);
        }

        TEST_CASE("World Streaming diagnostics reject duplicate source cell and failure identities",
                  "[unit][world_streaming][diagnostics][identity]") {
            const auto input = Input();
            const auto policy = Policy();
            const auto sample = Sample(policy);
            const std::array duplicateSources{Source(1, 1), Source(1, 2)};
            RequireError(CreateSnapshot(input, policy, sample, duplicateSources),
                         WorldStreamingErrors::DiagnosticProjectionIdentityConflict);

            const std::array duplicateCells{StreamingCellStateRecord{Operation(1, 1), StreamingCellState::Loading},
                                            StreamingCellStateRecord{Operation(2, 1), StreamingCellState::Resident}};
            RequireError(CreateSnapshot(input, policy, sample, {}, duplicateCells),
                         WorldStreamingErrors::DiagnosticProjectionIdentityConflict);

            const std::array cells{StreamingCellStateRecord{Operation(1, 1), StreamingCellState::Failed}};
            const std::array duplicateFailures{StreamingDiagnosticFailureRecord{cells[0].operation, StreamingCellOperationOutcome::Failed},
                                               StreamingDiagnosticFailureRecord{cells[0].operation,
                                                                                StreamingCellOperationOutcome::Shutdown}};
            RequireError(CreateSnapshot(input, policy, sample, {}, cells, duplicateFailures),
                         WorldStreamingErrors::DiagnosticProjectionIdentityConflict);
        }
    }  // namespace
}  // namespace Horo::WorldStreaming
