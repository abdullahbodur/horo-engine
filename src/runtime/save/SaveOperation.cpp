#include "Horo/Runtime/Save/SaveOperation.h"

#include "Horo/Runtime/Save/SaveErrors.h"

#include <algorithm>
#include <exception>
#include <mutex>
#include <new>
#include <utility>
#include <vector>

namespace Horo::Runtime {
    struct SaveOperationDetail::SharedState final {
        mutable std::mutex mutex;
        SaveOperationSnapshot snapshot;
        CancellationToken parentCancellation;
        std::size_t maximumCompletionCallbacks{};
        std::vector<SaveOperationCompletionCallback> completionCallbacks;
        bool commitStarted{};
        Error abandonmentError;
    };

    namespace {
        using SharedState = SaveOperationDetail::SharedState;

        struct CompletionDispatch final {
            std::optional<SaveOperationSnapshot> snapshot;
            std::vector<SaveOperationCompletionCallback> callbacks;
        };

        [[nodiscard]] bool IsKnown(const SaveOperationKind kind) noexcept {
            using enum SaveOperationKind;
            switch (kind) {
                case Save:
                case Load:
                case RefreshCatalog:
                case Delete:
                    return true;
            }
            return false;
        }

        [[nodiscard]] bool IsKnown(const SaveOperationStage stage) noexcept {
            using enum SaveOperationStage;
            switch (stage) {
                case Queued:
                case CapturingSnapshot:
                case Serializing:
                case FinalizingArchive:
                case WritingTemporary:
                case CommitStarted:
                case VerifyingArchive:
                case Migrating:
                case PreparingRestore:
                case ReadyToCommit:
                case ApplyingState:
                case RefreshingCatalog:
                case Deleting:
                    return true;
            }
            return false;
        }

        [[nodiscard]] bool RequiresCommit(const SaveOperationKind kind) noexcept {
            return kind != SaveOperationKind::RefreshCatalog;
        }

        [[nodiscard]] bool IsStageAllowed(const SaveOperationKind kind, const SaveOperationStage stage, const bool commitStarted) noexcept {
            using enum SaveOperationStage;
            switch (kind) {
                case SaveOperationKind::Save:
                    return stage == CapturingSnapshot || stage == Serializing || stage == FinalizingArchive || stage == WritingTemporary ||
                           (stage == CommitStarted && commitStarted);
                case SaveOperationKind::Load:
                    return stage == VerifyingArchive || stage == Migrating || stage == PreparingRestore || stage == ReadyToCommit ||
                           (stage == ApplyingState && commitStarted);
                case SaveOperationKind::RefreshCatalog:
                    return stage == RefreshingCatalog;
                case SaveOperationKind::Delete:
                    return stage == Deleting || (stage == CommitStarted && commitStarted);
            }
            return false;
        }

        [[nodiscard]] SaveOperationStage CommitStage(const SaveOperationKind kind) noexcept {
            return kind == SaveOperationKind::Load ? SaveOperationStage::ApplyingState : SaveOperationStage::CommitStarted;
        }

        [[nodiscard]] SaveCancellationReason PendingCancellation(SharedState &state,
                                                                 const std::chrono::steady_clock::time_point now) noexcept {
            if (state.snapshot.cancellationRequested)
                return state.snapshot.cancellationReason;
            if (state.parentCancellation.IsCancellationRequested())
                return SaveCancellationReason::Parent;
            if (state.snapshot.deadline.has_value() && now >= *state.snapshot.deadline)
                return SaveCancellationReason::Deadline;
            return SaveCancellationReason::None;
        }

        [[nodiscard]] const ErrorCodeDescriptor &CancellationError(const SaveCancellationReason reason) noexcept {
            return reason == SaveCancellationReason::Deadline ? SaveErrors::OperationDeadlineExceeded : SaveErrors::OperationCancelled;
        }

        void MarkCancellationRequested(SharedState &state, const SaveCancellationReason reason) noexcept {
            state.snapshot.cancellationRequested = true;
            state.snapshot.cancellationReason = reason;
            ++state.snapshot.revision;
        }

        [[nodiscard]] CompletionDispatch TerminalizeLocked(SharedState &state, const SaveOperationState terminalState,
                                                           const SaveOperationCommitOutcome outcome,
                                                           std::optional<Error> error = std::nullopt) {
            state.snapshot.state = terminalState;
            state.snapshot.commit = outcome;
            state.snapshot.cancellable = false;
            state.snapshot.terminalError = std::move(error);
            if (terminalState == SaveOperationState::Completed)
                state.snapshot.progress = {.completedUnits = 1, .totalUnits = 1};
            ++state.snapshot.revision;

            CompletionDispatch dispatch;
            dispatch.snapshot = state.snapshot;
            dispatch.callbacks = std::move(state.completionCallbacks);
            return dispatch;
        }

        [[nodiscard]] CompletionDispatch CancelLocked(SharedState &state, const SaveCancellationReason reason) {
            if (!state.snapshot.cancellationRequested)
                MarkCancellationRequested(state, reason);
            return TerminalizeLocked(state, SaveOperationState::Cancelled, SaveOperationCommitOutcome::NotCommitted,
                                     MakeError(CancellationError(reason)));
        }

        void Dispatch(CompletionDispatch dispatch) noexcept {
            if (!dispatch.snapshot.has_value())
                return;
            for (auto &callback : dispatch.callbacks) {
                try {
                    callback(*dispatch.snapshot);
                } catch (...) {
                    // Completion observers are isolated from operation state and from one another.
                }
            }
        }

        [[nodiscard]] SaveCancellationRequestResult RequestCancellation(const std::shared_ptr<SharedState> &state,
                                                                        const SaveCancellationReason reason) noexcept {
            if (!state)
                return SaveCancellationRequestResult::InvalidHandle;
            std::lock_guard lock(state->mutex);
            if (state->snapshot.IsTerminal())
                return SaveCancellationRequestResult::AlreadyTerminal;
            if (state->commitStarted)
                return SaveCancellationRequestResult::TooLate;
            if (state->snapshot.cancellationRequested)
                return SaveCancellationRequestResult::AlreadyRequested;
            MarkCancellationRequested(*state, reason);
            return SaveCancellationRequestResult::Requested;
        }

        [[nodiscard]] bool IsValidProgress(const SaveOperationProgress progress) noexcept {
            return progress.totalUnits != 0 && progress.completedUnits <= progress.totalUnits;
        }

        [[nodiscard]] bool IsProgressRegression(const SaveOperationSnapshot &snapshot, const SaveOperationStage stage,
                                                const SaveOperationProgress progress) noexcept {
            if (snapshot.stage != stage)
                return false;
            const long double next = static_cast<long double>(progress.completedUnits) / static_cast<long double>(progress.totalUnits);
            const long double current =
                static_cast<long double>(snapshot.progress.completedUnits) / static_cast<long double>(snapshot.progress.totalUnits);
            return next < current;
        }
    }  // namespace

    /** @copydoc SaveOperationProgress::Fraction */
    double SaveOperationProgress::Fraction() const noexcept {
        if (totalUnits == 0)
            return 0.0;
        return static_cast<double>(completedUnits) / static_cast<double>(totalUnits);
    }

    /** @copydoc SaveOperationSnapshot::IsTerminal */
    bool SaveOperationSnapshot::IsTerminal() const noexcept {
        return state == SaveOperationState::Completed || state == SaveOperationState::Failed || state == SaveOperationState::Cancelled;
    }

    SaveOperationHandle::SaveOperationHandle(std::shared_ptr<SaveOperationDetail::SharedState> state) noexcept : state_(std::move(state)) {}

    /** @copydoc SaveOperationHandle::IsValid */
    bool SaveOperationHandle::IsValid() const noexcept {
        return static_cast<bool>(state_);
    }

    /** @copydoc SaveOperationHandle::Id */
    OperationId SaveOperationHandle::Id() const noexcept {
        if (!state_)
            return 0;
        std::lock_guard lock(state_->mutex);
        return state_->snapshot.operation;
    }

    /** @copydoc SaveOperationHandle::Snapshot */
    std::optional<SaveOperationSnapshot> SaveOperationHandle::Snapshot() const {
        if (!state_)
            return std::nullopt;
        std::lock_guard lock(state_->mutex);
        return state_->snapshot;
    }

    /** @copydoc SaveOperationHandle::RequestCancellation */
    SaveCancellationRequestResult SaveOperationHandle::RequestCancellation() const noexcept {
        return Horo::Runtime::RequestCancellation(state_, SaveCancellationReason::Caller);
    }

    /** @copydoc SaveOperationHandle::OnCompletion */
    Result<void> SaveOperationHandle::OnCompletion(SaveOperationCompletionCallback callback) const {
        if (!state_)
            return Result<void>::Failure(MakeError(SaveErrors::OperationInvalid));
        if (!callback)
            return Result<void>::Failure(MakeError(SaveErrors::OperationCallbackInvalid));

        std::optional<SaveOperationSnapshot> terminal;
        {
            std::lock_guard lock(state_->mutex);
            if (state_->snapshot.IsTerminal()) {
                terminal = state_->snapshot;
            } else {
                if (state_->completionCallbacks.size() >= state_->maximumCompletionCallbacks)
                    return Result<void>::Failure(MakeError(SaveErrors::OperationCallbackCapacityExceeded));
                try {
                    state_->completionCallbacks.push_back(std::move(callback));
                } catch (const std::bad_alloc &) {
                    return Result<void>::Failure(MakeError(SaveErrors::OperationCallbackCapacityExceeded));
                }
                return Result<void>::Success();
            }
        }
        try {
            callback(*terminal);
        } catch (...) {
            // A late observer cannot affect the already immutable terminal result.
        }
        return Result<void>::Success();
    }

    SaveOperationController::SaveOperationController(std::shared_ptr<SaveOperationDetail::SharedState> state) noexcept
        : state_(std::move(state)) {}

    /** @copydoc SaveOperationController::~SaveOperationController */
    SaveOperationController::~SaveOperationController() {
        Abandon();
    }

    /** @copydoc SaveOperationController::SaveOperationController */
    SaveOperationController::SaveOperationController(SaveOperationController &&other) noexcept : state_(std::move(other.state_)) {}

    /** @copydoc SaveOperationController::operator= */
    SaveOperationController &SaveOperationController::operator=(SaveOperationController &&other) noexcept {
        if (this == &other)
            return *this;
        Abandon();
        state_ = std::move(other.state_);
        return *this;
    }

    /** @copydoc SaveOperationController::Handle */
    SaveOperationHandle SaveOperationController::Handle() const noexcept {
        return SaveOperationHandle{state_};
    }

    /** @copydoc SaveOperationController::PublishProgress */
    SaveOperationTransitionResult SaveOperationController::PublishProgress(const SaveOperationStage stage,
                                                                           const SaveOperationProgress progress,
                                                                           const std::chrono::steady_clock::time_point now) {
        if (!state_)
            return SaveOperationTransitionResult::AlreadyTerminal;
        CompletionDispatch dispatch;
        SaveOperationTransitionResult result = SaveOperationTransitionResult::Applied;
        {
            std::lock_guard lock(state_->mutex);
            if (state_->snapshot.IsTerminal())
                return SaveOperationTransitionResult::AlreadyTerminal;
            const SaveCancellationReason cancellation = PendingCancellation(*state_, now);
            if (!state_->commitStarted && cancellation != SaveCancellationReason::None) {
                dispatch = CancelLocked(*state_, cancellation);
                result = SaveOperationTransitionResult::CancellationWon;
            } else if (!IsKnown(stage) || !IsStageAllowed(state_->snapshot.kind, stage, state_->commitStarted) ||
                       !IsValidProgress(progress) || IsProgressRegression(state_->snapshot, stage, progress)) {
                return SaveOperationTransitionResult::InvalidTransition;
            } else {
                state_->snapshot.state = SaveOperationState::Running;
                state_->snapshot.stage = stage;
                state_->snapshot.progress = progress;
                ++state_->snapshot.revision;
            }
        }
        Dispatch(std::move(dispatch));
        return result;
    }

    /** @copydoc SaveOperationController::ObserveCancellation */
    SaveCancellationObservation SaveOperationController::ObserveCancellation(const std::chrono::steady_clock::time_point now) {
        if (!state_)
            return SaveCancellationObservation::AlreadyTerminal;
        CompletionDispatch dispatch;
        SaveCancellationObservation result = SaveCancellationObservation::NotRequested;
        {
            std::lock_guard lock(state_->mutex);
            if (state_->snapshot.IsTerminal())
                return SaveCancellationObservation::AlreadyTerminal;
            if (state_->commitStarted)
                return SaveCancellationObservation::TooLate;
            const SaveCancellationReason cancellation = PendingCancellation(*state_, now);
            if (cancellation != SaveCancellationReason::None) {
                dispatch = CancelLocked(*state_, cancellation);
                result = SaveCancellationObservation::Cancelled;
            }
        }
        Dispatch(std::move(dispatch));
        return result;
    }

    /** @copydoc SaveOperationController::BeginCommit */
    SaveCommitGateResult SaveOperationController::BeginCommit(const std::chrono::steady_clock::time_point now) {
        if (!state_)
            return SaveCommitGateResult::AlreadyTerminal;
        CompletionDispatch dispatch;
        SaveCommitGateResult result = SaveCommitGateResult::Entered;
        {
            std::lock_guard lock(state_->mutex);
            if (state_->snapshot.IsTerminal())
                return SaveCommitGateResult::AlreadyTerminal;
            if (state_->commitStarted)
                return SaveCommitGateResult::AlreadyEntered;
            const SaveCancellationReason cancellation = PendingCancellation(*state_, now);
            if (cancellation != SaveCancellationReason::None) {
                dispatch = CancelLocked(*state_, cancellation);
                result = SaveCommitGateResult::CancellationWon;
            } else if (!RequiresCommit(state_->snapshot.kind)) {
                return SaveCommitGateResult::NotRequired;
            } else {
                state_->commitStarted = true;
                state_->snapshot.state = SaveOperationState::Running;
                state_->snapshot.stage = CommitStage(state_->snapshot.kind);
                state_->snapshot.progress = {};
                state_->snapshot.cancellable = false;
                ++state_->snapshot.revision;
            }
        }
        Dispatch(std::move(dispatch));
        return result;
    }

    /** @copydoc SaveOperationController::RequestShutdownCancellation */
    SaveCancellationRequestResult SaveOperationController::RequestShutdownCancellation() const noexcept {
        return Horo::Runtime::RequestCancellation(state_, SaveCancellationReason::Shutdown);
    }

    /** @copydoc SaveOperationController::Complete */
    SaveOperationTransitionResult SaveOperationController::Complete(const SaveOperationCommitOutcome outcome,
                                                                    const std::chrono::steady_clock::time_point now) {
        if (!state_)
            return SaveOperationTransitionResult::AlreadyTerminal;
        CompletionDispatch dispatch;
        SaveOperationTransitionResult result = SaveOperationTransitionResult::Applied;
        {
            std::lock_guard lock(state_->mutex);
            if (state_->snapshot.IsTerminal())
                return SaveOperationTransitionResult::AlreadyTerminal;
            const SaveCancellationReason cancellation = PendingCancellation(*state_, now);
            if (!state_->commitStarted && cancellation != SaveCancellationReason::None) {
                dispatch = CancelLocked(*state_, cancellation);
                result = SaveOperationTransitionResult::CancellationWon;
            } else {
                const bool validMutation =
                    RequiresCommit(state_->snapshot.kind) && state_->commitStarted && outcome == SaveOperationCommitOutcome::Committed;
                const bool validQuery =
                    !RequiresCommit(state_->snapshot.kind) && !state_->commitStarted && outcome == SaveOperationCommitOutcome::NotCommitted;
                if (!validMutation && !validQuery)
                    return SaveOperationTransitionResult::InvalidTransition;
                dispatch = TerminalizeLocked(*state_, SaveOperationState::Completed, outcome);
            }
        }
        Dispatch(std::move(dispatch));
        return result;
    }

    /** @copydoc SaveOperationController::Fail */
    SaveOperationTransitionResult SaveOperationController::Fail(Error error, const SaveOperationCommitOutcome outcome,
                                                                const std::chrono::steady_clock::time_point now) {
        if (!state_)
            return SaveOperationTransitionResult::AlreadyTerminal;
        CompletionDispatch dispatch;
        SaveOperationTransitionResult result = SaveOperationTransitionResult::Applied;
        {
            std::lock_guard lock(state_->mutex);
            if (state_->snapshot.IsTerminal())
                return SaveOperationTransitionResult::AlreadyTerminal;
            const SaveCancellationReason cancellation = PendingCancellation(*state_, now);
            if (!state_->commitStarted && cancellation != SaveCancellationReason::None) {
                dispatch = CancelLocked(*state_, cancellation);
                result = SaveOperationTransitionResult::CancellationWon;
            } else if (outcome == SaveOperationCommitOutcome::Committed ||
                       (!state_->commitStarted && outcome != SaveOperationCommitOutcome::NotCommitted)) {
                return SaveOperationTransitionResult::InvalidTransition;
            } else {
                dispatch = TerminalizeLocked(*state_, SaveOperationState::Failed, outcome, std::move(error));
            }
        }
        Dispatch(std::move(dispatch));
        return result;
    }

    void SaveOperationController::Abandon() noexcept {
        if (!state_)
            return;
        CompletionDispatch dispatch;
        try {
            std::lock_guard lock(state_->mutex);
            if (!state_->snapshot.IsTerminal()) {
                const SaveCancellationReason cancellation = PendingCancellation(*state_, std::chrono::steady_clock::now());
                if (!state_->commitStarted && cancellation != SaveCancellationReason::None) {
                    dispatch = CancelLocked(*state_, cancellation);
                } else {
                    const SaveOperationCommitOutcome outcome =
                        state_->commitStarted ? SaveOperationCommitOutcome::Unknown : SaveOperationCommitOutcome::NotCommitted;
                    dispatch = TerminalizeLocked(*state_, SaveOperationState::Failed, outcome, std::move(state_->abandonmentError));
                }
            }
        } catch (...) {
            // The state remains owned by consumer handles; destructors never propagate failures.
        }
        Dispatch(std::move(dispatch));
        state_.reset();
    }

    /** @copydoc CreateSaveOperation */
    Result<SaveOperationController> CreateSaveOperation(SaveOperationDescriptor descriptor) {
        if (descriptor.operation == 0 || descriptor.maximumCompletionCallbacks == 0 ||
            descriptor.maximumCompletionCallbacks > MaximumSaveOperationCompletionCallbacks || !IsKnown(descriptor.kind))
            return Result<SaveOperationController>::Failure(MakeError(SaveErrors::OperationInvalid));
        try {
            auto state = std::make_shared<SaveOperationDetail::SharedState>();
            state->snapshot.operation = descriptor.operation;
            state->snapshot.kind = descriptor.kind;
            state->snapshot.deadline = descriptor.deadline;
            state->parentCancellation = std::move(descriptor.parentCancellation);
            state->maximumCompletionCallbacks = descriptor.maximumCompletionCallbacks;
            state->completionCallbacks.reserve(descriptor.maximumCompletionCallbacks);
            state->abandonmentError = MakeError(SaveErrors::OperationAbandoned);
            return Result<SaveOperationController>::Success(SaveOperationController{std::move(state)});
        } catch (const std::bad_alloc &) {
            return Result<SaveOperationController>::Failure(MakeError(SaveErrors::OperationInvalid));
        }
    }
}  // namespace Horo::Runtime
