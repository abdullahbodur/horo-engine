#include "Horo/Runtime/Save/SaveOperation.h"

#include "Horo/Runtime/Save/SaveErrors.h"

#include <algorithm>
#include <array>
#include <exception>
#include <mutex>
#include <new>
#include <type_traits>
#include <utility>
#include <vector>

namespace Horo::Runtime {
    struct SaveOperationDetail::SharedState final {
        mutable std::mutex mutex;
        SaveOperationSnapshot snapshot;
        std::optional<SaveOperationSnapshot> terminalSnapshot;
        CancellationToken parentCancellation;
        std::size_t maximumCompletionCallbacks{};
        std::vector<SaveOperationCompletionCallback> completionCallbacks;
        bool commitStarted{};
        Error cancellationError;
        Error deadlineError;
        Error abandonmentError;
    };

    namespace {
        using SharedState = SaveOperationDetail::SharedState;

        struct CompletionDispatch final {
            std::shared_ptr<SharedState> state;
            std::vector<SaveOperationCompletionCallback> callbacks;
        };

        static_assert(std::is_nothrow_move_constructible_v<SaveOperationSnapshot>);
        static_assert(std::is_nothrow_move_assignable_v<SaveOperationSnapshot>);

        [[nodiscard]] const SaveOperationSnapshot &CurrentSnapshot(const SharedState &state) noexcept {
            return state.terminalSnapshot.has_value() ? *state.terminalSnapshot : state.snapshot;
        }

        [[nodiscard]] bool IsTerminal(const SharedState &state) noexcept {
            return state.terminalSnapshot.has_value();
        }

        [[nodiscard]] bool IsKnown(const SaveOperationKind kind) noexcept {
            return static_cast<std::uint8_t>(kind) <= static_cast<std::uint8_t>(SaveOperationKind::Delete);
        }

        [[nodiscard]] bool IsKnown(const SaveOperationStage stage) noexcept {
            return static_cast<std::uint8_t>(stage) <= static_cast<std::uint8_t>(SaveOperationStage::Deleting);
        }

        [[nodiscard]] bool RequiresCommit(const SaveOperationKind kind) noexcept {
            return kind != SaveOperationKind::RefreshCatalog;
        }

        [[nodiscard]] bool IsStageAllowed(const SaveOperationKind kind, const SaveOperationStage stage, const bool commitStarted) noexcept {
            constexpr auto stageBit = [](const SaveOperationStage value) {
                return static_cast<std::uint16_t>(1U << static_cast<std::uint8_t>(value));
            };
            constexpr std::array<std::uint16_t, 4> allowedStages{
                stageBit(SaveOperationStage::CapturingSnapshot) | stageBit(SaveOperationStage::Serializing) |
                    stageBit(SaveOperationStage::FinalizingArchive) | stageBit(SaveOperationStage::WritingTemporary) |
                    stageBit(SaveOperationStage::CommitStarted),
                stageBit(SaveOperationStage::VerifyingArchive) | stageBit(SaveOperationStage::Migrating) |
                    stageBit(SaveOperationStage::PreparingRestore) | stageBit(SaveOperationStage::ReadyToCommit) |
                    stageBit(SaveOperationStage::ApplyingState),
                stageBit(SaveOperationStage::RefreshingCatalog),
                stageBit(SaveOperationStage::Deleting) | stageBit(SaveOperationStage::CommitStarted)};
            constexpr std::uint16_t commitStages = stageBit(SaveOperationStage::CommitStarted) | stageBit(SaveOperationStage::ApplyingState);
            const std::uint16_t selectedStage = stageBit(stage);
            const bool allowedForKind = (allowedStages[static_cast<std::uint8_t>(kind)] & selectedStage) != 0;
            return allowedForKind && (commitStarted || (commitStages & selectedStage) == 0);
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

        void MarkCancellationRequested(SharedState &state, const SaveCancellationReason reason) noexcept {
            state.snapshot.cancellationRequested = true;
            state.snapshot.cancellationReason = reason;
            ++state.snapshot.revision;
        }

        [[nodiscard]] CompletionDispatch TerminalizeLocked(const std::shared_ptr<SharedState> &state,
                                                           const SaveOperationState terminalState, const SaveOperationCommitOutcome outcome,
                                                           std::optional<Error> error = std::nullopt) noexcept {
            state->snapshot.state = terminalState;
            state->snapshot.commit = outcome;
            state->snapshot.cancellable = false;
            state->snapshot.terminalError = std::move(error);
            if (terminalState == SaveOperationState::Completed)
                state->snapshot.progress = {.completedUnits = 1, .totalUnits = 1};
            ++state->snapshot.revision;
            state->terminalSnapshot.emplace(std::move(state->snapshot));
            return CompletionDispatch{state, std::move(state->completionCallbacks)};
        }

        [[nodiscard]] CompletionDispatch CancelLocked(const std::shared_ptr<SharedState> &state,
                                                      const SaveCancellationReason reason) noexcept {
            if (!state->snapshot.cancellationRequested)
                MarkCancellationRequested(*state, reason);
            Error error =
                reason == SaveCancellationReason::Deadline ? std::move(state->deadlineError) : std::move(state->cancellationError);
            return TerminalizeLocked(state, SaveOperationState::Cancelled, SaveOperationCommitOutcome::NotCommitted, std::move(error));
        }

        void Dispatch(CompletionDispatch dispatch) noexcept {
            if (!dispatch.state)
                return;
            const SaveOperationSnapshot &snapshot = *dispatch.state->terminalSnapshot;
            for (auto &callback : dispatch.callbacks) {
                try {
                    callback(snapshot);
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
            if (IsTerminal(*state))
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

        [[nodiscard]] int CompareFractions(std::uint64_t leftNumerator, std::uint64_t leftDenominator, std::uint64_t rightNumerator,
                                           std::uint64_t rightDenominator) noexcept {
            int direction = 1;
            while (true) {
                const std::uint64_t leftQuotient = leftNumerator / leftDenominator;
                const std::uint64_t rightQuotient = rightNumerator / rightDenominator;
                if (leftQuotient != rightQuotient)
                    return direction * (leftQuotient < rightQuotient ? -1 : 1);

                const std::uint64_t leftRemainder = leftNumerator % leftDenominator;
                const std::uint64_t rightRemainder = rightNumerator % rightDenominator;
                if (leftRemainder == 0 || rightRemainder == 0) {
                    if (leftRemainder == rightRemainder)
                        return 0;
                    return direction * (leftRemainder == 0 ? -1 : 1);
                }
                leftNumerator = leftDenominator;
                leftDenominator = leftRemainder;
                rightNumerator = rightDenominator;
                rightDenominator = rightRemainder;
                direction = -direction;
            }
        }

        [[nodiscard]] bool IsProgressRegression(const SaveOperationSnapshot &snapshot, const SaveOperationStage stage,
                                                const SaveOperationProgress progress) noexcept {
            if (snapshot.stage != stage)
                return false;
            return CompareFractions(progress.completedUnits, progress.totalUnits, snapshot.progress.completedUnits,
                                    snapshot.progress.totalUnits) < 0;
        }

        template <typename Apply>
        [[nodiscard]] SaveOperationTransitionResult ApplyTransition(const std::shared_ptr<SharedState> &state,
                                                                    const std::chrono::steady_clock::time_point now, Apply &&apply) {
            if (!state)
                return SaveOperationTransitionResult::AlreadyTerminal;
            CompletionDispatch dispatch;
            SaveOperationTransitionResult result = SaveOperationTransitionResult::Applied;
            {
                std::lock_guard lock(state->mutex);
                if (IsTerminal(*state))
                    return SaveOperationTransitionResult::AlreadyTerminal;
                const SaveCancellationReason cancellation = PendingCancellation(*state, now);
                if (!state->commitStarted && cancellation != SaveCancellationReason::None) {
                    dispatch = CancelLocked(state, cancellation);
                    result = SaveOperationTransitionResult::CancellationWon;
                } else {
                    result = std::forward<Apply>(apply)(*state, dispatch);
                }
            }
            Dispatch(std::move(dispatch));
            return result;
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
        return state >= SaveOperationState::Completed && state <= SaveOperationState::Cancelled;
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
        return CurrentSnapshot(*state_).operation;
    }

    /** @copydoc SaveOperationHandle::Snapshot */
    std::optional<SaveOperationSnapshot> SaveOperationHandle::Snapshot() const {
        if (!state_)
            return std::nullopt;
        std::lock_guard lock(state_->mutex);
        return CurrentSnapshot(*state_);
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

        const SaveOperationSnapshot *terminal = nullptr;
        {
            std::lock_guard lock(state_->mutex);
            if (IsTerminal(*state_)) {
                terminal = std::addressof(*state_->terminalSnapshot);
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
        return ApplyTransition(state_, now, [stage, progress](SharedState &state, CompletionDispatch &) {
            if (!IsKnown(stage) || !IsStageAllowed(state.snapshot.kind, stage, state.commitStarted) || !IsValidProgress(progress) ||
                IsProgressRegression(state.snapshot, stage, progress))
                return SaveOperationTransitionResult::InvalidTransition;
            state.snapshot.state = SaveOperationState::Running;
            state.snapshot.stage = stage;
            state.snapshot.progress = progress;
            ++state.snapshot.revision;
            return SaveOperationTransitionResult::Applied;
        });
    }

    /** @copydoc SaveOperationController::ObserveCancellation */
    SaveCancellationObservation SaveOperationController::ObserveCancellation(const std::chrono::steady_clock::time_point now) {
        if (!state_)
            return SaveCancellationObservation::AlreadyTerminal;
        CompletionDispatch dispatch;
        SaveCancellationObservation result = SaveCancellationObservation::NotRequested;
        {
            std::lock_guard lock(state_->mutex);
            if (IsTerminal(*state_))
                return SaveCancellationObservation::AlreadyTerminal;
            if (state_->commitStarted)
                return SaveCancellationObservation::TooLate;
            const SaveCancellationReason cancellation = PendingCancellation(*state_, now);
            if (cancellation != SaveCancellationReason::None) {
                dispatch = CancelLocked(state_, cancellation);
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
            if (IsTerminal(*state_))
                return SaveCommitGateResult::AlreadyTerminal;
            if (state_->commitStarted)
                return SaveCommitGateResult::AlreadyEntered;
            const SaveCancellationReason cancellation = PendingCancellation(*state_, now);
            if (cancellation != SaveCancellationReason::None) {
                dispatch = CancelLocked(state_, cancellation);
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
        return ApplyTransition(state_, now, [this, outcome](SharedState &state, CompletionDispatch &dispatch) {
            const bool validMutation =
                RequiresCommit(state.snapshot.kind) && state.commitStarted && outcome == SaveOperationCommitOutcome::Committed;
            const bool validQuery =
                !RequiresCommit(state.snapshot.kind) && !state.commitStarted && outcome == SaveOperationCommitOutcome::NotCommitted;
            if (!validMutation && !validQuery)
                return SaveOperationTransitionResult::InvalidTransition;
            dispatch = TerminalizeLocked(state_, SaveOperationState::Completed, outcome);
            return SaveOperationTransitionResult::Applied;
        });
    }

    /** @copydoc SaveOperationController::Fail */
    SaveOperationTransitionResult SaveOperationController::Fail(Error error, const SaveOperationCommitOutcome outcome,
                                                                const std::chrono::steady_clock::time_point now) {
        return ApplyTransition(state_, now,
                               [this, outcome, error = std::move(error)](SharedState &state, CompletionDispatch &dispatch) mutable {
            if (outcome == SaveOperationCommitOutcome::Committed ||
                (!state.commitStarted && outcome != SaveOperationCommitOutcome::NotCommitted))
                return SaveOperationTransitionResult::InvalidTransition;
            dispatch = TerminalizeLocked(state_, SaveOperationState::Failed, outcome, std::move(error));
            return SaveOperationTransitionResult::Applied;
        });
    }

    void SaveOperationController::Abandon() noexcept {
        if (!state_)
            return;
        CompletionDispatch dispatch;
        try {
            std::lock_guard lock(state_->mutex);
            if (!IsTerminal(*state_)) {
                const SaveCancellationReason cancellation = PendingCancellation(*state_, std::chrono::steady_clock::now());
                if (!state_->commitStarted && cancellation != SaveCancellationReason::None) {
                    dispatch = CancelLocked(state_, cancellation);
                } else {
                    const SaveOperationCommitOutcome outcome =
                        state_->commitStarted ? SaveOperationCommitOutcome::Unknown : SaveOperationCommitOutcome::NotCommitted;
                    dispatch = TerminalizeLocked(state_, SaveOperationState::Failed, outcome, std::move(state_->abandonmentError));
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
            state->cancellationError = MakeError(SaveErrors::OperationCancelled);
            state->deadlineError = MakeError(SaveErrors::OperationDeadlineExceeded);
            state->abandonmentError = MakeError(SaveErrors::OperationAbandoned);
            return Result<SaveOperationController>::Success(SaveOperationController{std::move(state)});
        } catch (const std::bad_alloc &) {
            return Result<SaveOperationController>::Failure(MakeError(SaveErrors::OperationInvalid));
        }
    }
}  // namespace Horo::Runtime
