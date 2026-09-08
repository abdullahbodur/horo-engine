#include "Horo/WorldStreaming/StreamingCellOperation.h"

#include "Horo/WorldStreaming/WorldStreamingErrors.h"

#include <utility>

namespace Horo::WorldStreaming {
    namespace {
        template <typename T> [[nodiscard]] Result<T> Failure(const ErrorCodeDescriptor &descriptor) {
            return Result<T>::Failure(MakeError(descriptor));
        }

        [[nodiscard]] bool IsKnown(const StreamingCellOperationTransition transition) noexcept {
            return transition >= StreamingCellOperationTransition::Admit &&
                   transition <= StreamingCellOperationTransition::AcknowledgeRetirement;
        }

        [[nodiscard]] StreamingCellOperationOutcome InterruptionOutcome(const StreamingCellOperationTransition transition) noexcept {
            switch (transition) {
                case StreamingCellOperationTransition::Cancel:
                    return StreamingCellOperationOutcome::Cancelled;
                case StreamingCellOperationTransition::Fail:
                    return StreamingCellOperationOutcome::Failed;
                case StreamingCellOperationTransition::Replace:
                    return StreamingCellOperationOutcome::Replaced;
                case StreamingCellOperationTransition::Shutdown:
                    return StreamingCellOperationOutcome::Shutdown;
                default:
                    return StreamingCellOperationOutcome::None;
            }
        }
    }  // namespace

    /** @copydoc StreamingCellOperationHandle::IsValid */
    bool StreamingCellOperationHandle::IsValid() const noexcept {
        return operation.IsValid() && fence.IsValid();
    }

    /** @copydoc StreamingCellOperation::Create */
    Result<StreamingCellOperation> StreamingCellOperation::Create(StreamingCellOperationHandle handle) {
        if (!handle.IsValid())
            return Failure<StreamingCellOperation>(WorldStreamingErrors::CellOperationInvalid);
        return Result<StreamingCellOperation>::Success(
            StreamingCellOperation{std::move(handle), StreamingCellOperationState::Queued, StreamingCellOperationOutcome::None});
    }

    /** @copydoc StreamingCellOperation::Handle */
    const StreamingCellOperationHandle &StreamingCellOperation::Handle() const noexcept {
        return handle_;
    }

    /** @copydoc StreamingCellOperation::State */
    StreamingCellOperationState StreamingCellOperation::State() const noexcept {
        return state_;
    }

    /** @copydoc StreamingCellOperation::Outcome */
    StreamingCellOperationOutcome StreamingCellOperation::Outcome() const noexcept {
        return outcome_;
    }

    /** @copydoc StreamingCellOperation::IsTerminal */
    bool StreamingCellOperation::IsTerminal() const noexcept {
        return state_ == StreamingCellOperationState::Terminal;
    }

    /** @copydoc StreamingCellOperation::Advance */
    Result<StreamingCellOperation> StreamingCellOperation::Advance(const StreamingCellOperationHandle &expected,
                                                                   const StreamingCellOperationTransition transition) const {
        if (!expected.IsValid())
            return Failure<StreamingCellOperation>(WorldStreamingErrors::CellOperationInvalid);
        if (expected != handle_)
            return Failure<StreamingCellOperation>(WorldStreamingErrors::CellOperationStale);
        if (!IsKnown(transition))
            return Failure<StreamingCellOperation>(WorldStreamingErrors::CellOperationUnsupported);

        if (state_ == StreamingCellOperationState::Queued && transition == StreamingCellOperationTransition::Admit)
            return Result<StreamingCellOperation>::Success(
                StreamingCellOperation{handle_, StreamingCellOperationState::Admitted, StreamingCellOperationOutcome::None});
        if (state_ == StreamingCellOperationState::Admitted && transition == StreamingCellOperationTransition::BeginPreparation)
            return Result<StreamingCellOperation>::Success(
                StreamingCellOperation{handle_, StreamingCellOperationState::Preparing, StreamingCellOperationOutcome::None});
        if (state_ == StreamingCellOperationState::Preparing && transition == StreamingCellOperationTransition::BeginActivation)
            return Result<StreamingCellOperation>::Success(
                StreamingCellOperation{handle_, StreamingCellOperationState::Activating, StreamingCellOperationOutcome::None});
        if (state_ == StreamingCellOperationState::Activating && transition == StreamingCellOperationTransition::Complete)
            return Result<StreamingCellOperation>::Success(
                StreamingCellOperation{handle_, StreamingCellOperationState::Terminal, StreamingCellOperationOutcome::Succeeded});

        const auto interruption = InterruptionOutcome(transition);
        if (state_ == StreamingCellOperationState::Queued && interruption != StreamingCellOperationOutcome::None)
            return Result<StreamingCellOperation>::Success(
                StreamingCellOperation{handle_, StreamingCellOperationState::Terminal, interruption});
        if ((state_ == StreamingCellOperationState::Admitted || state_ == StreamingCellOperationState::Preparing ||
             state_ == StreamingCellOperationState::Activating) &&
            interruption != StreamingCellOperationOutcome::None)
            return Result<StreamingCellOperation>::Success(
                StreamingCellOperation{handle_, StreamingCellOperationState::Retiring, interruption});
        if (state_ == StreamingCellOperationState::Retiring && transition == StreamingCellOperationTransition::AcknowledgeRetirement)
            return Result<StreamingCellOperation>::Success(
                StreamingCellOperation{handle_, StreamingCellOperationState::Terminal, outcome_});

        return Failure<StreamingCellOperation>(WorldStreamingErrors::CellOperationTransitionInvalid);
    }

    StreamingCellOperation::StreamingCellOperation(StreamingCellOperationHandle handle, const StreamingCellOperationState state,
                                                   const StreamingCellOperationOutcome outcome) noexcept
        : handle_(std::move(handle)), state_(state), outcome_(outcome) {}
}  // namespace Horo::WorldStreaming
