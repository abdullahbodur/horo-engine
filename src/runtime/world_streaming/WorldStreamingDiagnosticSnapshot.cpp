#include "Horo/WorldStreaming/WorldStreamingDiagnosticSnapshot.h"

#include "WorldStreamingInternal.h"

#include <algorithm>
#include <array>
#include <new>
#include <tuple>

namespace Horo::WorldStreaming {
    namespace {
        /** @brief Checks the closed lifecycle vocabulary without accepting a future enum implicitly. */
        bool IsKnownLifecycle(const WorldStreamingRuntimeCompositionState state) noexcept {
            return state >= WorldStreamingRuntimeCompositionState::Active && state <= WorldStreamingRuntimeCompositionState::Closed;
        }

        /** @brief Checks the closed scheduler lifecycle vocabulary. */
        bool IsKnownQueueState(const StreamingSchedulerAdmissionState state) noexcept {
            return state >= StreamingSchedulerAdmissionState::Accepting && state <= StreamingSchedulerAdmissionState::Closed;
        }

        /** @brief Checks the failure-only subset of cell-operation terminal dispositions. */
        bool IsFailureReason(const StreamingCellOperationOutcome outcome) noexcept {
            using enum StreamingCellOperationOutcome;
            return outcome == Cancelled || outcome == Failed || outcome == Replaced || outcome == Shutdown;
        }

        /** @brief Orders exact operation handles by persistent cell key, generation and operation identity. */
        bool OperationLess(const StreamingCellOperationHandle &left, const StreamingCellOperationHandle &right) noexcept {
            const StreamingCellCanonicalLess cellLess;
            if (cellLess(left.fence.cell, right.fence.cell))
                return true;
            if (cellLess(right.fence.cell, left.fence.cell))
                return false;
            return std::tuple{left.fence.generation.Value(), left.operation.Value()} <
                   std::tuple{right.fence.generation.Value(), right.operation.Value()};
        }

        /** @brief Verifies that one fence belongs to the captured mounted authority. */
        bool MatchesOwner(const StreamingFence &fence, const StreamingRuntimeOwnerToken &owner) noexcept {
            return fence.partition == owner.partition && fence.epoch == owner.epoch;
        }

        /** @brief Verifies lifecycle/queue relationships and bounded aggregate pressure. */
        Result<void> ValidateQueue(const WorldStreamingDiagnosticSnapshotInput &input) {
            const auto &queue = input.queue;
            if (!queue.owner.IsValid() || !queue.limits.IsValid())
                return Internal::Failure<void>(WorldStreamingErrors::DiagnosticProjectionInvalid);
            if (!IsKnownLifecycle(input.lifecycle) || !IsKnownQueueState(queue.state))
                return Internal::Failure<void>(WorldStreamingErrors::DiagnosticProjectionUnsupported);
            if (queue.reservedOperations > queue.limits.concurrentOperations || queue.reservedCapacityUnits > queue.limits.capacityUnits)
                return Internal::Failure<void>(WorldStreamingErrors::DiagnosticProjectionCapacityExceeded);

            const bool activeMismatch = input.lifecycle == WorldStreamingRuntimeCompositionState::Active &&
                                        queue.state != StreamingSchedulerAdmissionState::Accepting;
            const bool cancellingMismatch = input.lifecycle == WorldStreamingRuntimeCompositionState::Cancelling &&
                                            queue.state == StreamingSchedulerAdmissionState::Accepting;
            const bool drainingMismatch = input.lifecycle == WorldStreamingRuntimeCompositionState::Draining &&
                                          queue.state == StreamingSchedulerAdmissionState::Accepting;
            const bool closedMismatch = input.lifecycle == WorldStreamingRuntimeCompositionState::Closed &&
                                        (queue.state != StreamingSchedulerAdmissionState::Closed || queue.reservedOperations != 0 ||
                                         queue.reservedCapacityUnits != 0);
            if (activeMismatch || cancellingMismatch || drainingMismatch || closedMismatch)
                return Internal::Failure<void>(WorldStreamingErrors::DiagnosticProjectionInvalid);
            return Result<void>::Success();
        }
    }  // namespace

    /** @copydoc WorldStreamingDiagnosticLimits::IsValid */
    bool WorldStreamingDiagnosticLimits::IsValid() const noexcept {
        return sources > 0 && sources <= MaximumSources && cells > 0 && cells <= MaximumCells && failures > 0 &&
               failures <= MaximumFailures;
    }

    /** @copydoc WorldStreamingDiagnosticSnapshot::Create */
    Result<WorldStreamingDiagnosticSnapshot> WorldStreamingDiagnosticSnapshot::Create(
        const WorldStreamingDiagnosticSnapshotInput &input, const StreamingBudgetPolicy &policy, const StreamingBudgetSample &sample,
        const std::span<const StreamingSourceDesiredState> sources, const std::span<const StreamingCellStateRecord> cells,
        const std::span<const StreamingDiagnosticFailureRecord> failures) {
        if (!input.owner.IsValid() || !input.revision.IsValid() || !input.limits.IsValid())
            return Internal::Failure<WorldStreamingDiagnosticSnapshot>(WorldStreamingErrors::DiagnosticProjectionInvalid);
        if (const auto queue = ValidateQueue(input); queue.HasError())
            return Result<WorldStreamingDiagnosticSnapshot>::Failure(queue.ErrorValue());
        if (sample.PolicyRevision() != policy.Revision())
            return Internal::Failure<WorldStreamingDiagnosticSnapshot>(WorldStreamingErrors::DiagnosticProjectionStale);
        if (sources.size() > input.limits.sources || cells.size() > input.limits.cells || failures.size() > input.limits.failures)
            return Internal::Failure<WorldStreamingDiagnosticSnapshot>(WorldStreamingErrors::DiagnosticProjectionCapacityExceeded);

        for (const auto &source : sources) {
            const auto &descriptor = source.Source();
            if (!descriptor.IsValid())
                return Internal::Failure<WorldStreamingDiagnosticSnapshot>(WorldStreamingErrors::DiagnosticProjectionInvalid);
            if (descriptor.owner.partition != input.owner.partition || descriptor.owner.epoch != input.owner.epoch)
                return Internal::Failure<WorldStreamingDiagnosticSnapshot>(WorldStreamingErrors::DiagnosticProjectionStale);
        }
        for (const auto &cell : cells) {
            if (!cell.IsValid())
                return Internal::Failure<WorldStreamingDiagnosticSnapshot>(WorldStreamingErrors::DiagnosticProjectionUnsupported);
            if (!MatchesOwner(cell.operation.fence, input.owner))
                return Internal::Failure<WorldStreamingDiagnosticSnapshot>(WorldStreamingErrors::DiagnosticProjectionStale);
        }
        for (const auto &failure : failures) {
            if (!failure.operation.IsValid())
                return Internal::Failure<WorldStreamingDiagnosticSnapshot>(WorldStreamingErrors::DiagnosticProjectionInvalid);
            if (!IsFailureReason(failure.reason))
                return Internal::Failure<WorldStreamingDiagnosticSnapshot>(WorldStreamingErrors::DiagnosticProjectionUnsupported);
            if (!MatchesOwner(failure.operation.fence, input.owner))
                return Internal::Failure<WorldStreamingDiagnosticSnapshot>(WorldStreamingErrors::DiagnosticProjectionStale);
            const auto matchingCell = std::find_if(cells.begin(), cells.end(), [&failure](const StreamingCellStateRecord &cell) {
                return cell.operation == failure.operation;
            });
            if (matchingCell == cells.end())
                return Internal::Failure<WorldStreamingDiagnosticSnapshot>(WorldStreamingErrors::DiagnosticProjectionStale);
        }

        try {
            std::vector<StreamingSourceDesiredState> ownedSources(sources.begin(), sources.end());
            std::vector<StreamingCellStateRecord> ownedCells(cells.begin(), cells.end());
            std::vector<StreamingDiagnosticFailureRecord> ownedFailures(failures.begin(), failures.end());

            std::ranges::sort(ownedSources, {}, [](const StreamingSourceDesiredState &source) {
                return source.Source().id.Value();
            });
            std::ranges::sort(ownedCells, [](const StreamingCellStateRecord &left, const StreamingCellStateRecord &right) {
                return OperationLess(left.operation, right.operation);
            });
            std::ranges::sort(ownedFailures,
                              [](const StreamingDiagnosticFailureRecord &left, const StreamingDiagnosticFailureRecord &right) {
                return OperationLess(left.operation, right.operation);
            });

            if (std::adjacent_find(ownedSources.begin(), ownedSources.end(),
                                   [](const auto &left, const auto &right) {
                return left.Source().id == right.Source().id;
            }) != ownedSources.end() ||
                std::adjacent_find(ownedCells.begin(), ownedCells.end(),
                                   [](const auto &left, const auto &right) {
                return left.operation.fence == right.operation.fence;
            }) != ownedCells.end() ||
                std::adjacent_find(ownedFailures.begin(), ownedFailures.end(), [](const auto &left, const auto &right) {
                return left.operation == right.operation;
            }) != ownedFailures.end())
                return Internal::Failure<WorldStreamingDiagnosticSnapshot>(WorldStreamingErrors::DiagnosticProjectionIdentityConflict);

            return Result<WorldStreamingDiagnosticSnapshot>::Success(
                WorldStreamingDiagnosticSnapshot{input, policy, sample, std::move(ownedSources), std::move(ownedCells),
                                                 std::move(ownedFailures)});
        } catch (const std::bad_alloc &) {
            return Internal::Failure<WorldStreamingDiagnosticSnapshot>(WorldStreamingErrors::DiagnosticProjectionCapacityExceeded);
        }
    }

    /** @copydoc WorldStreamingDiagnosticSnapshot::WorldStreamingDiagnosticSnapshot */
    WorldStreamingDiagnosticSnapshot::WorldStreamingDiagnosticSnapshot(WorldStreamingDiagnosticSnapshotInput input,
                                                                       StreamingBudgetPolicy policy, StreamingBudgetSample sample,
                                                                       std::vector<StreamingSourceDesiredState> sources,
                                                                       std::vector<StreamingCellStateRecord> cells,
                                                                       std::vector<StreamingDiagnosticFailureRecord> failures) noexcept
        : input_(std::move(input)), policy_(std::move(policy)), sample_(std::move(sample)), sources_(std::move(sources)),
          cells_(std::move(cells)), failures_(std::move(failures)) {}

    /** @copydoc WorldStreamingDiagnosticSnapshot::Owner */
    const StreamingRuntimeOwnerToken &WorldStreamingDiagnosticSnapshot::Owner() const noexcept {
        return input_.owner;
    }

    /** @copydoc WorldStreamingDiagnosticSnapshot::Revision */
    WorldStreamingDiagnosticRevision WorldStreamingDiagnosticSnapshot::Revision() const noexcept {
        return input_.revision;
    }

    /** @copydoc WorldStreamingDiagnosticSnapshot::Lifecycle */
    WorldStreamingRuntimeCompositionState WorldStreamingDiagnosticSnapshot::Lifecycle() const noexcept {
        return input_.lifecycle;
    }

    /** @copydoc WorldStreamingDiagnosticSnapshot::Queue */
    const StreamingDiagnosticQueueSnapshot &WorldStreamingDiagnosticSnapshot::Queue() const noexcept {
        return input_.queue;
    }

    /** @copydoc WorldStreamingDiagnosticSnapshot::BudgetPolicy */
    const StreamingBudgetPolicy &WorldStreamingDiagnosticSnapshot::BudgetPolicy() const noexcept {
        return policy_;
    }

    /** @copydoc WorldStreamingDiagnosticSnapshot::BudgetSample */
    const StreamingBudgetSample &WorldStreamingDiagnosticSnapshot::BudgetSample() const noexcept {
        return sample_;
    }

    /** @copydoc WorldStreamingDiagnosticSnapshot::Sources */
    std::span<const StreamingSourceDesiredState> WorldStreamingDiagnosticSnapshot::Sources() const noexcept {
        return sources_;
    }

    /** @copydoc WorldStreamingDiagnosticSnapshot::Cells */
    std::span<const StreamingCellStateRecord> WorldStreamingDiagnosticSnapshot::Cells() const noexcept {
        return cells_;
    }

    /** @copydoc WorldStreamingDiagnosticSnapshot::Failures */
    std::span<const StreamingDiagnosticFailureRecord> WorldStreamingDiagnosticSnapshot::Failures() const noexcept {
        return failures_;
    }
}  // namespace Horo::WorldStreaming
