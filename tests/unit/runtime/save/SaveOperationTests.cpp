#include "Horo/Runtime/Save/SaveErrors.h"
#include "Horo/Runtime/Save/SaveOperation.h"

#include <atomic>
#include <barrier>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <thread>

namespace {
    using namespace Horo;
    using namespace Horo::Runtime;

    SaveOperationController Operation(const SaveOperationKind kind = SaveOperationKind::Save, const OperationId id = 41,
                                      const std::size_t callbackCapacity = 4,
                                      const std::optional<std::chrono::steady_clock::time_point> deadline = std::nullopt,
                                      const CancellationToken parent = {}) {
        auto created = CreateSaveOperation({.operation = id,
                                            .kind = kind,
                                            .maximumCompletionCallbacks = callbackCapacity,
                                            .deadline = deadline,
                                            .parentCancellation = parent});
        REQUIRE(created.HasValue());
        return std::move(created).Value();
    }

    SaveOperationSnapshot Snapshot(const SaveOperationHandle &handle) {
        const auto snapshot = handle.Snapshot();
        REQUIRE(snapshot.has_value());
        return *snapshot;
    }

    void RequireError(const std::optional<Error> &error, const ErrorCodeDescriptor &descriptor) {
        REQUIRE(error.has_value());
        CHECK(error->domain.Value() == descriptor.domain.Value());
        CHECK(error->code.Value() == descriptor.code.Value());
    }
}  // namespace

TEST_CASE("Save operation admission uses application operation identities", "[unit][runtime][save][operation]") {
    for (const auto descriptor :
         {SaveOperationDescriptor{.maximumCompletionCallbacks = 1}, SaveOperationDescriptor{.operation = 1},
          SaveOperationDescriptor{.operation = 1, .kind = static_cast<SaveOperationKind>(255), .maximumCompletionCallbacks = 1},
          SaveOperationDescriptor{.operation = 1, .maximumCompletionCallbacks = MaximumSaveOperationCompletionCallbacks + 1}}) {
        const auto created = CreateSaveOperation(descriptor);
        REQUIRE(created.HasError());
        CHECK(created.ErrorValue().code.Value() == SaveErrors::OperationInvalid.code.Value());
    }

    auto controller = Operation(SaveOperationKind::Load, 73);
    const SaveOperationHandle handle = controller.Handle();
    CHECK(handle.IsValid());
    CHECK(handle.Id() == 73);
    const auto initial = Snapshot(handle);
    CHECK(initial.kind == SaveOperationKind::Load);
    CHECK(initial.state == SaveOperationState::Queued);
    CHECK(initial.stage == SaveOperationStage::Queued);
    CHECK(initial.cancellable);
    CHECK_FALSE(initial.IsTerminal());

    const SaveOperationHandle invalid;
    CHECK_FALSE(invalid.IsValid());
    CHECK(invalid.Id() == 0);
    CHECK_FALSE(invalid.Snapshot().has_value());
    CHECK(invalid.RequestCancellation() == SaveCancellationRequestResult::InvalidHandle);
    REQUIRE(invalid
                .OnCompletion([](const SaveOperationSnapshot &) {
    }).HasError());
    REQUIRE(handle.OnCompletion({}).HasError());
}

TEST_CASE("Save operation publishes exact typed progress without regression", "[unit][runtime][save][operation]") {
    auto controller = Operation();
    const auto handle = controller.Handle();
    CHECK(controller.PublishProgress(SaveOperationStage::CapturingSnapshot, {2, 5}) == SaveOperationTransitionResult::Applied);
    auto progress = Snapshot(handle);
    CHECK(progress.state == SaveOperationState::Running);
    CHECK(progress.stage == SaveOperationStage::CapturingSnapshot);
    CHECK(progress.progress.completedUnits == 2);
    CHECK(progress.progress.totalUnits == 5);
    CHECK(progress.progress.Fraction() == 0.4);

    CHECK(controller.PublishProgress(SaveOperationStage::CapturingSnapshot, {1, 5}) == SaveOperationTransitionResult::InvalidTransition);
    CHECK(controller.PublishProgress(SaveOperationStage::Serializing, {6, 5}) == SaveOperationTransitionResult::InvalidTransition);
    CHECK(controller.PublishProgress(SaveOperationStage::Serializing, {0, 0}) == SaveOperationTransitionResult::InvalidTransition);
    CHECK(controller.PublishProgress(SaveOperationStage::PreparingRestore, {0, 1}) == SaveOperationTransitionResult::InvalidTransition);
    CHECK(Snapshot(handle).revision == progress.revision);
}

TEST_CASE("Cancellation before commit wins and publishes one immutable terminal result", "[unit][runtime][save][operation]") {
    auto controller = Operation();
    const auto handle = controller.Handle();
    std::atomic<int> callbacks{};
    REQUIRE(handle
                .OnCompletion([&callbacks](const SaveOperationSnapshot &snapshot) {
        CHECK(snapshot.state == SaveOperationState::Cancelled);
        ++callbacks;
    }).HasValue());

    CHECK(handle.RequestCancellation() == SaveCancellationRequestResult::Requested);
    CHECK(handle.RequestCancellation() == SaveCancellationRequestResult::AlreadyRequested);
    CHECK(controller.BeginCommit() == SaveCommitGateResult::CancellationWon);
    CHECK(callbacks.load() == 1);
    const auto terminal = Snapshot(handle);
    CHECK(terminal.state == SaveOperationState::Cancelled);
    CHECK(terminal.commit == SaveOperationCommitOutcome::NotCommitted);
    CHECK(terminal.cancellationReason == SaveCancellationReason::Caller);
    CHECK_FALSE(terminal.cancellable);
    RequireError(terminal.terminalError, SaveErrors::OperationCancelled);
    CHECK(controller.Fail(MakeError(SaveErrors::CompositionInjectedFailure), SaveOperationCommitOutcome::NotCommitted) ==
          SaveOperationTransitionResult::AlreadyTerminal);
    CHECK(Snapshot(handle).revision == terminal.revision);
}

TEST_CASE("Commit gate makes late cancellation too late", "[unit][runtime][save][operation]") {
    auto controller = Operation(SaveOperationKind::Load);
    const auto handle = controller.Handle();
    CHECK(controller.PublishProgress(SaveOperationStage::ReadyToCommit, {1, 1}) == SaveOperationTransitionResult::Applied);
    CHECK(controller.BeginCommit() == SaveCommitGateResult::Entered);
    CHECK(Snapshot(handle).stage == SaveOperationStage::ApplyingState);
    CHECK(handle.RequestCancellation() == SaveCancellationRequestResult::TooLate);
    CHECK(controller.RequestShutdownCancellation() == SaveCancellationRequestResult::TooLate);
    CHECK(controller.Complete(SaveOperationCommitOutcome::Committed) == SaveOperationTransitionResult::Applied);

    const auto terminal = Snapshot(handle);
    CHECK(terminal.state == SaveOperationState::Completed);
    CHECK(terminal.commit == SaveOperationCommitOutcome::Committed);
    CHECK(terminal.progress.Fraction() == 1.0);
    CHECK_FALSE(terminal.terminalError.has_value());
}

TEST_CASE("Deadlines and parent cancellation are observed before commit", "[unit][runtime][save][operation]") {
    const auto now = std::chrono::steady_clock::now();
    auto expired = Operation(SaveOperationKind::Save, 41, 2, now);
    const auto expiredHandle = expired.Handle();
    CHECK(expired.ObserveCancellation(now) == SaveCancellationObservation::Cancelled);
    const auto deadlineTerminal = Snapshot(expiredHandle);
    CHECK(deadlineTerminal.cancellationReason == SaveCancellationReason::Deadline);
    RequireError(deadlineTerminal.terminalError, SaveErrors::OperationDeadlineExceeded);

    CancellationSource parent;
    auto child = Operation(SaveOperationKind::Save, 42, 2, std::nullopt, parent.Token());
    const auto childHandle = child.Handle();
    parent.RequestCancellation();
    CHECK(child.PublishProgress(SaveOperationStage::CapturingSnapshot, {0, 1}) == SaveOperationTransitionResult::CancellationWon);
    CHECK(Snapshot(childHandle).cancellationReason == SaveCancellationReason::Parent);
}

TEST_CASE("Completion callbacks are bounded reentrant and immediate after terminal", "[unit][runtime][save][operation]") {
    auto controller = Operation(SaveOperationKind::RefreshCatalog, 41, 1);
    const auto handle = controller.Handle();
    std::atomic<int> calls{};
    REQUIRE(handle
                .OnCompletion([handle, &calls](const SaveOperationSnapshot &terminal) {
        CHECK(terminal.IsTerminal());
        CHECK(handle.Snapshot()->revision == terminal.revision);
        ++calls;
    }).HasValue());
    const auto excess = handle.OnCompletion([](const SaveOperationSnapshot &) {
    });
    REQUIRE(excess.HasError());
    CHECK(excess.ErrorValue().code.Value() == SaveErrors::OperationCallbackCapacityExceeded.code.Value());
    CHECK(controller.Complete(SaveOperationCommitOutcome::NotCommitted) == SaveOperationTransitionResult::Applied);
    CHECK(calls.load() == 1);

    REQUIRE(handle
                .OnCompletion([&calls](const SaveOperationSnapshot &) {
        ++calls;
    }).HasValue());
    CHECK(calls.load() == 2);
    CHECK(handle.RequestCancellation() == SaveCancellationRequestResult::AlreadyTerminal);
}

TEST_CASE("Post-gate failure preserves unknown publication evidence and the original cause", "[unit][runtime][save][operation]") {
    auto controller = Operation(SaveOperationKind::Delete);
    const auto handle = controller.Handle();
    CHECK(controller.PublishProgress(SaveOperationStage::Deleting, {1, 2}) == SaveOperationTransitionResult::Applied);
    CHECK(controller.BeginCommit() == SaveCommitGateResult::Entered);
    CHECK(controller.Fail(MakeError(SaveErrors::CompositionInjectedFailure), SaveOperationCommitOutcome::Unknown) ==
          SaveOperationTransitionResult::Applied);

    const auto terminal = Snapshot(handle);
    CHECK(terminal.state == SaveOperationState::Failed);
    CHECK(terminal.commit == SaveOperationCommitOutcome::Unknown);
    RequireError(terminal.terminalError, SaveErrors::CompositionInjectedFailure);

    auto preCommit = Operation(SaveOperationKind::Save, 42);
    CHECK(preCommit.Fail(MakeError(SaveErrors::CompositionInjectedFailure), SaveOperationCommitOutcome::Unknown) ==
          SaveOperationTransitionResult::InvalidTransition);
}

TEST_CASE("Producer release terminalizes an abandoned operation", "[unit][runtime][save][operation]") {
    SaveOperationHandle handle;
    std::atomic<int> calls{};
    {
        auto controller = Operation();
        handle = controller.Handle();
        REQUIRE(handle
                    .OnCompletion([&calls](const SaveOperationSnapshot &) {
            ++calls;
        }).HasValue());
    }
    const auto terminal = Snapshot(handle);
    CHECK(terminal.state == SaveOperationState::Failed);
    CHECK(terminal.commit == SaveOperationCommitOutcome::NotCommitted);
    RequireError(terminal.terminalError, SaveErrors::OperationAbandoned);
    CHECK(calls.load() == 1);
}

TEST_CASE("Cancellation and completion races produce exactly one terminal callback", "[unit][runtime][save][operation]") {
    for (int iteration = 0; iteration < 64; ++iteration) {
        auto controller = Operation(SaveOperationKind::RefreshCatalog, static_cast<OperationId>(iteration + 1));
        const auto handle = controller.Handle();
        std::atomic<int> callbacks{};
        REQUIRE(handle
                    .OnCompletion([&callbacks](const SaveOperationSnapshot &) {
            ++callbacks;
        }).HasValue());
        std::barrier start{2};
        std::thread cancellation([&] {
            start.arrive_and_wait();
            static_cast<void>(handle.RequestCancellation());
        });
        start.arrive_and_wait();
        const auto completion = controller.Complete(SaveOperationCommitOutcome::NotCommitted);
        cancellation.join();

        const auto terminal = Snapshot(handle);
        CHECK(terminal.IsTerminal());
        CHECK((terminal.state == SaveOperationState::Completed || terminal.state == SaveOperationState::Cancelled));
        CHECK((completion == SaveOperationTransitionResult::Applied || completion == SaveOperationTransitionResult::CancellationWon));
        CHECK(callbacks.load() == 1);
    }
}

TEST_CASE("Operation errors expose stable actionable descriptors", "[unit][runtime][save][operation]") {
    const ErrorCodeDescriptor *descriptors[]{&SaveErrors::OperationInvalid,
                                             &SaveErrors::OperationTransitionInvalid,
                                             &SaveErrors::OperationCallbackCapacityExceeded,
                                             &SaveErrors::OperationCallbackInvalid,
                                             &SaveErrors::OperationCancelled,
                                             &SaveErrors::OperationDeadlineExceeded,
                                             &SaveErrors::OperationAbandoned};
    for (const auto *descriptor : descriptors) {
        CHECK(descriptor->domain.Value() == "horo.save");
        CHECK_FALSE(descriptor->code.Value().empty());
        CHECK_FALSE(descriptor->summary.empty());
        CHECK_FALSE(descriptor->remediationHint.empty());
    }
}
