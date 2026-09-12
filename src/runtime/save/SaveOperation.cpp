#include "Horo/Runtime/Save/SaveOperation.h"

#include "Horo/Runtime/Save/SaveErrors.h"

#include <array>
#include <exception>
#include <mutex>
#include <new>
#include <tuple>
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

        enum class PreflightDisposition : std::uint8_t {
            Proceed,
            AlreadyTerminal,
            CommitStarted,
            CancellationWon,
        };

        struct PreflightResult final {
            PreflightDisposition disposition{PreflightDisposition::Proceed};
            CompletionDispatch dispatch;
        };

        struct TransitionRequest;
        using TransitionHandler = SaveOperationTransitionResult (*)(const std::shared_ptr<SharedState> &, TransitionRequest &,
                                                                    CompletionDispatch &) noexcept;

        struct TransitionRequest final {
            TransitionHandler handler{};
            SaveOperationStage stage{SaveOperationStage::Queued};
            SaveOperationProgress progress{};
            SaveOperationCommitOutcome outcome{SaveOperationCommitOutcome::NotCommitted};
            std::optional<Error> error;
        };

        struct StagePolicy final {
            std::array<std::uint8_t, 13> ranks;
            SaveOperationStage readyStage;
            SaveOperationStage commitStage;
            bool requiresCommit;
        };

        constexpr std::uint8_t InvalidStageRank = 0xffU;
        constexpr std::array StagePolicies{
            StagePolicy{{InvalidStageRank, 1, 2, 3, 4, 5, InvalidStageRank, InvalidStageRank, InvalidStageRank, InvalidStageRank,
                         InvalidStageRank, InvalidStageRank, InvalidStageRank},
                        SaveOperationStage::WritingTemporary,
                        SaveOperationStage::CommitStarted,
                        true},
            StagePolicy{{InvalidStageRank, InvalidStageRank, InvalidStageRank, InvalidStageRank, InvalidStageRank, InvalidStageRank, 1, 2,
                         3, 4, 5, InvalidStageRank, InvalidStageRank},
                        SaveOperationStage::ReadyToCommit,
                        SaveOperationStage::ApplyingState,
                        true},
            StagePolicy{{InvalidStageRank, InvalidStageRank, InvalidStageRank, InvalidStageRank, InvalidStageRank, InvalidStageRank,
                         InvalidStageRank, InvalidStageRank, InvalidStageRank, InvalidStageRank, InvalidStageRank, 1, InvalidStageRank},
                        SaveOperationStage::RefreshingCatalog,
                        SaveOperationStage::Queued,
                        false},
            StagePolicy{{InvalidStageRank, InvalidStageRank, InvalidStageRank, InvalidStageRank, InvalidStageRank, 2, InvalidStageRank,
                         InvalidStageRank, InvalidStageRank, InvalidStageRank, InvalidStageRank, InvalidStageRank, 1},
                        SaveOperationStage::Deleting,
                        SaveOperationStage::CommitStarted,
                        true},
        };

        static_assert(std::is_nothrow_move_constructible_v<SaveOperationSnapshot>);
        static_assert(std::is_nothrow_move_assignable_v<SaveOperationSnapshot>);

        [[nodiscard]] const SaveOperationSnapshot &CurrentSnapshot(const SharedState &state) noexcept {
            return state.terminalSnapshot.has_value() ? *state.terminalSnapshot : state.snapshot;
        }

        [[nodiscard]] bool IsTerminal(const SharedState &state) noexcept {
            return state.terminalSnapshot.has_value();
        }

        template <typename Enum> [[nodiscard]] bool IsKnown(const Enum value, const Enum last) noexcept {
            return static_cast<std::uint8_t>(value) <= static_cast<std::uint8_t>(last);
        }

        [[nodiscard]] bool RequiresCommit(const SaveOperationKind kind) noexcept {
            return StagePolicies[static_cast<std::uint8_t>(kind)].requiresCommit;
        }

        [[nodiscard]] const StagePolicy &PolicyFor(const SaveOperationKind kind) noexcept {
            return StagePolicies[static_cast<std::uint8_t>(kind)];
        }

        [[nodiscard]] SaveOperationStage CommitStage(const SaveOperationKind kind) noexcept {
            return PolicyFor(kind).commitStage;
        }

        [[nodiscard]] bool IsReadyForCommit(const SharedState &state) noexcept {
            const StagePolicy &policy = PolicyFor(state.snapshot.kind);
            return policy.requiresCommit && state.snapshot.stage == policy.readyStage &&
                   state.snapshot.progress.completedUnits == state.snapshot.progress.totalUnits;
        }

        [[nodiscard]] bool IsAllowedProgressStage(const SharedState &state, const SaveOperationStage stage) noexcept {
            if (!IsKnown(stage, SaveOperationStage::Deleting))
                return false;
            const StagePolicy &policy = PolicyFor(state.snapshot.kind);
            const std::uint8_t nextRank = policy.ranks[static_cast<std::uint8_t>(stage)];
            if (nextRank == InvalidStageRank)
                return false;
            if (state.commitStarted)
                return stage == policy.commitStage;
            const std::uint8_t currentRank =
                state.snapshot.stage == SaveOperationStage::Queued ? 0 : policy.ranks[static_cast<std::uint8_t>(state.snapshot.stage)];
            return stage != policy.commitStage && nextRank >= currentRank;
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

        [[nodiscard]] PreflightResult PreflightLocked(const std::shared_ptr<SharedState> &state,
                                                      const std::chrono::steady_clock::time_point now) noexcept {
            if (IsTerminal(*state))
                return {.disposition = PreflightDisposition::AlreadyTerminal};
            if (state->commitStarted)
                return {.disposition = PreflightDisposition::CommitStarted};
            const SaveCancellationReason cancellation = PendingCancellation(*state, now);
            if (cancellation == SaveCancellationReason::None)
                return {};
            return {.disposition = PreflightDisposition::CancellationWon, .dispatch = CancelLocked(state, cancellation)};
        }

        void Dispatch(const CompletionDispatch &dispatch) noexcept {
            if (!dispatch.state)
                return;
            const SaveOperationSnapshot &snapshot = *dispatch.state->terminalSnapshot;
            for (const auto &callback : dispatch.callbacks) {
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

        struct WideProduct final {
            std::uint64_t high{};
            std::uint64_t low{};
        };

        [[nodiscard]] constexpr WideProduct MultiplyWide(const std::uint64_t left, const std::uint64_t right) noexcept {
            constexpr std::uint64_t lowerMask = 0xffffffffULL;
            const std::uint64_t leftLow = left & lowerMask;
            const std::uint64_t leftHigh = left >> 32U;
            const std::uint64_t rightLow = right & lowerMask;
            const std::uint64_t rightHigh = right >> 32U;
            const std::uint64_t lowProduct = leftLow * rightLow;
            const std::uint64_t firstCross = leftHigh * rightLow + (lowProduct >> 32U);
            const std::uint64_t secondCross = leftLow * rightHigh + (firstCross & lowerMask);
            return {.high = leftHigh * rightHigh + (firstCross >> 32U) + (secondCross >> 32U),
                    .low = (secondCross << 32U) + (lowProduct & lowerMask)};
        }

        [[nodiscard]] bool IsProgressRegression(const SaveOperationSnapshot &snapshot, const SaveOperationStage stage,
                                                const SaveOperationProgress progress) noexcept {
            if (snapshot.stage != stage)
                return false;
            const WideProduct next = MultiplyWide(progress.completedUnits, snapshot.progress.totalUnits);
            const WideProduct current = MultiplyWide(snapshot.progress.completedUnits, progress.totalUnits);
            return std::tie(next.high, next.low) < std::tie(current.high, current.low);
        }

        [[nodiscard]] bool IsValidProgressTransition(const SharedState &state, const TransitionRequest &request) noexcept {
            return IsAllowedProgressStage(state, request.stage) && IsValidProgress(request.progress) &&
                   !IsProgressRegression(state.snapshot, request.stage, request.progress);
        }

        [[nodiscard]] SaveOperationTransitionResult PublishProgressLocked(const std::shared_ptr<SharedState> &state,
                                                                          TransitionRequest &request, CompletionDispatch &) noexcept {
            if (!IsValidProgressTransition(*state, request))
                return SaveOperationTransitionResult::InvalidTransition;
            state->snapshot.state = SaveOperationState::Running;
            state->snapshot.stage = request.stage;
            state->snapshot.progress = request.progress;
            ++state->snapshot.revision;
            return SaveOperationTransitionResult::Applied;
        }

        [[nodiscard]] SaveOperationTransitionResult CompleteLocked(const std::shared_ptr<SharedState> &state,
                                                                   const TransitionRequest &request,
                                                                   CompletionDispatch &dispatch) noexcept {
            const bool validMutation =
                RequiresCommit(state->snapshot.kind) && state->commitStarted && request.outcome == SaveOperationCommitOutcome::Committed;
            const bool validQuery = !RequiresCommit(state->snapshot.kind) && !state->commitStarted &&
                                    request.outcome == SaveOperationCommitOutcome::NotCommitted;
            if (!validMutation && !validQuery)
                return SaveOperationTransitionResult::InvalidTransition;
            state->snapshot.progress = {.completedUnits = 1, .totalUnits = 1};
            dispatch = TerminalizeLocked(state, SaveOperationState::Completed, request.outcome);
            return SaveOperationTransitionResult::Applied;
        }

        [[nodiscard]] SaveOperationTransitionResult FailLocked(const std::shared_ptr<SharedState> &state, TransitionRequest &request,
                                                               CompletionDispatch &dispatch) noexcept {
            const bool validOutcome = request.outcome == SaveOperationCommitOutcome::NotCommitted ||
                                      (state->commitStarted && request.outcome == SaveOperationCommitOutcome::Unknown);
            if (!validOutcome)
                return SaveOperationTransitionResult::InvalidTransition;
            dispatch = TerminalizeLocked(state, SaveOperationState::Failed, request.outcome, std::move(request.error));
            return SaveOperationTransitionResult::Applied;
        }

        [[nodiscard]] SaveOperationTransitionResult ApplyTransition(const std::shared_ptr<SharedState> &state,
                                                                    const std::chrono::steady_clock::time_point now,
                                                                    TransitionRequest request) {
            if (!state)
                return SaveOperationTransitionResult::AlreadyTerminal;
            CompletionDispatch dispatch;
            SaveOperationTransitionResult result = SaveOperationTransitionResult::Applied;
            {
                std::lock_guard lock(state->mutex);
                PreflightResult preflight = PreflightLocked(state, now);
                if (preflight.disposition == PreflightDisposition::AlreadyTerminal)
                    return SaveOperationTransitionResult::AlreadyTerminal;
                if (preflight.disposition == PreflightDisposition::CancellationWon) {
                    dispatch = std::move(preflight.dispatch);
                    result = SaveOperationTransitionResult::CancellationWon;
                } else {
                    result = request.handler(state, request, dispatch);
                }
            }
            Dispatch(dispatch);
            return result;
        }

        void AbandonState(const std::shared_ptr<SharedState> &state) noexcept {
            if (!state)
                return;
            CompletionDispatch dispatch;
            try {
                std::lock_guard lock(state->mutex);
                PreflightResult preflight = PreflightLocked(state, std::chrono::steady_clock::now());
                dispatch = std::move(preflight.dispatch);
                if (preflight.disposition == PreflightDisposition::Proceed ||
                    preflight.disposition == PreflightDisposition::CommitStarted) {
                    const SaveOperationCommitOutcome outcome = preflight.disposition == PreflightDisposition::CommitStarted
                                                                   ? SaveOperationCommitOutcome::Unknown
                                                                   : SaveOperationCommitOutcome::NotCommitted;
                    dispatch = TerminalizeLocked(state, SaveOperationState::Failed, outcome, std::move(state->abandonmentError));
                }
            } catch (...) {
                // Destructors never propagate platform mutex failures.
            }
            Dispatch(dispatch);
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
        const std::shared_ptr<SaveOperationDetail::SharedState> state = state_;
        if (!state)
            return Result<void>::Failure(MakeError(SaveErrors::OperationInvalid));
        if (!callback)
            return Result<void>::Failure(MakeError(SaveErrors::OperationCallbackInvalid));

        const SaveOperationSnapshot *terminal = nullptr;
        {
            std::lock_guard lock(state->mutex);
            if (IsTerminal(*state)) {
                terminal = std::addressof(*state->terminalSnapshot);
            } else {
                if (state->completionCallbacks.size() >= state->maximumCompletionCallbacks)
                    return Result<void>::Failure(MakeError(SaveErrors::OperationCallbackCapacityExceeded));
                try {
                    state->completionCallbacks.push_back(std::move(callback));
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
    SaveOperationController::SaveOperationController(SaveOperationController &&other) noexcept : state_(std::exchange(other.state_, {})) {}

    /** @copydoc SaveOperationController::operator= */
    SaveOperationController &SaveOperationController::operator=(SaveOperationController &&other) noexcept {
        if (this == &other)
            return *this;
        std::shared_ptr<SaveOperationDetail::SharedState> replacement = std::exchange(other.state_, {});
        std::shared_ptr<SaveOperationDetail::SharedState> prior = std::exchange(state_, std::move(replacement));
        AbandonState(prior);
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
        return ApplyTransition(state_, now, {.handler = PublishProgressLocked, .stage = stage, .progress = progress});
    }

    /** @copydoc SaveOperationController::ObserveCancellation */
    SaveCancellationObservation SaveOperationController::ObserveCancellation(const std::chrono::steady_clock::time_point now) {
        if (!state_)
            return SaveCancellationObservation::AlreadyTerminal;
        CompletionDispatch dispatch;
        SaveCancellationObservation result = SaveCancellationObservation::NotRequested;
        {
            std::lock_guard lock(state_->mutex);
            PreflightResult preflight = PreflightLocked(state_, now);
            if (preflight.disposition == PreflightDisposition::AlreadyTerminal)
                return SaveCancellationObservation::AlreadyTerminal;
            if (preflight.disposition == PreflightDisposition::CommitStarted)
                return SaveCancellationObservation::TooLate;
            if (preflight.disposition == PreflightDisposition::CancellationWon) {
                dispatch = std::move(preflight.dispatch);
                result = SaveCancellationObservation::Cancelled;
            }
        }
        Dispatch(dispatch);
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
            PreflightResult preflight = PreflightLocked(state_, now);
            if (preflight.disposition == PreflightDisposition::AlreadyTerminal)
                return SaveCommitGateResult::AlreadyTerminal;
            if (preflight.disposition == PreflightDisposition::CommitStarted)
                return SaveCommitGateResult::AlreadyEntered;
            if (preflight.disposition == PreflightDisposition::CancellationWon) {
                dispatch = std::move(preflight.dispatch);
                result = SaveCommitGateResult::CancellationWon;
            } else if (!RequiresCommit(state_->snapshot.kind)) {
                return SaveCommitGateResult::NotRequired;
            } else if (!IsReadyForCommit(*state_)) {
                return SaveCommitGateResult::NotReady;
            } else {
                state_->commitStarted = true;
                state_->snapshot.state = SaveOperationState::Running;
                state_->snapshot.stage = CommitStage(state_->snapshot.kind);
                state_->snapshot.progress = {};
                state_->snapshot.cancellable = false;
                ++state_->snapshot.revision;
            }
        }
        Dispatch(dispatch);
        return result;
    }

    /** @copydoc SaveOperationController::RequestShutdownCancellation */
    SaveCancellationRequestResult SaveOperationController::RequestShutdownCancellation() const noexcept {
        return Horo::Runtime::RequestCancellation(state_, SaveCancellationReason::Shutdown);
    }

    /** @copydoc SaveOperationController::Complete */
    SaveOperationTransitionResult SaveOperationController::Complete(const SaveOperationCommitOutcome outcome,
                                                                    const std::chrono::steady_clock::time_point now) {
        return ApplyTransition(state_, now, {.handler = CompleteLocked, .outcome = outcome});
    }

    /** @copydoc SaveOperationController::Fail */
    SaveOperationTransitionResult SaveOperationController::Fail(Error error, const SaveOperationCommitOutcome outcome,
                                                                const std::chrono::steady_clock::time_point now) {
        return ApplyTransition(state_, now, {.handler = FailLocked, .outcome = outcome, .error = std::move(error)});
    }

    void SaveOperationController::Abandon() noexcept {
        AbandonState(std::exchange(state_, {}));
    }

    /** @copydoc CreateSaveOperation */
    Result<SaveOperationController> CreateSaveOperation(SaveOperationDescriptor descriptor) {
        if (descriptor.operation == 0 || descriptor.maximumCompletionCallbacks == 0 ||
            descriptor.maximumCompletionCallbacks > MaximumSaveOperationCompletionCallbacks ||
            !IsKnown(descriptor.kind, SaveOperationKind::Delete))
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
            return Result<SaveOperationController>::Failure(MakeError(SaveErrors::OperationAllocationFailed));
        }
    }
}  // namespace Horo::Runtime
