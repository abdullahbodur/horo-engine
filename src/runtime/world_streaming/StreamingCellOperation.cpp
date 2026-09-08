#include "Horo/WorldStreaming/StreamingCellOperation.h"

#include "Horo/WorldStreaming/WorldStreamingErrors.h"

#include <algorithm>
#include <array>
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

        [[nodiscard]] bool IsKnown(const StreamingCellOperationKind kind) noexcept {
            return kind >= StreamingCellOperationKind::Load && kind <= StreamingCellOperationKind::Retire;
        }

        struct TransitionRule final {
            StreamingCellOperationState source;
            StreamingCellOperationTransition transition;
            StreamingCellOperationState destination;
            StreamingCellOperationOutcome outcome;
            StreamingCellOperationKind requiredKind{StreamingCellOperationKind::Load};
            bool anyKind{true};
            bool retainOutcome{};
        };

        constexpr auto TransitionRules = std::to_array<TransitionRule>({
            {StreamingCellOperationState::Queued, StreamingCellOperationTransition::Admit, StreamingCellOperationState::Admitted,
             StreamingCellOperationOutcome::None},
            {StreamingCellOperationState::Admitted, StreamingCellOperationTransition::BeginPreparation,
             StreamingCellOperationState::Preparing, StreamingCellOperationOutcome::None, StreamingCellOperationKind::Load, false},
            {StreamingCellOperationState::Admitted, StreamingCellOperationTransition::BeginPreparation,
             StreamingCellOperationState::Preparing, StreamingCellOperationOutcome::None, StreamingCellOperationKind::Activate, false},
            {StreamingCellOperationState::Preparing, StreamingCellOperationTransition::BeginActivation,
             StreamingCellOperationState::Activating, StreamingCellOperationOutcome::None, StreamingCellOperationKind::Activate, false},
            {StreamingCellOperationState::Admitted, StreamingCellOperationTransition::BeginRetirement,
             StreamingCellOperationState::Retiring, StreamingCellOperationOutcome::Succeeded, StreamingCellOperationKind::Retire, false},
            {StreamingCellOperationState::Preparing, StreamingCellOperationTransition::Complete, StreamingCellOperationState::Terminal,
             StreamingCellOperationOutcome::Succeeded, StreamingCellOperationKind::Load, false},
            {StreamingCellOperationState::Activating, StreamingCellOperationTransition::Complete, StreamingCellOperationState::Terminal,
             StreamingCellOperationOutcome::Succeeded, StreamingCellOperationKind::Activate, false},
            {StreamingCellOperationState::Queued, StreamingCellOperationTransition::Cancel, StreamingCellOperationState::Terminal,
             StreamingCellOperationOutcome::Cancelled},
            {StreamingCellOperationState::Queued, StreamingCellOperationTransition::Fail, StreamingCellOperationState::Terminal,
             StreamingCellOperationOutcome::Failed},
            {StreamingCellOperationState::Queued, StreamingCellOperationTransition::Replace, StreamingCellOperationState::Terminal,
             StreamingCellOperationOutcome::Replaced},
            {StreamingCellOperationState::Queued, StreamingCellOperationTransition::Shutdown, StreamingCellOperationState::Terminal,
             StreamingCellOperationOutcome::Shutdown},
            {StreamingCellOperationState::Admitted, StreamingCellOperationTransition::Cancel, StreamingCellOperationState::Retiring,
             StreamingCellOperationOutcome::Cancelled},
            {StreamingCellOperationState::Admitted, StreamingCellOperationTransition::Fail, StreamingCellOperationState::Retiring,
             StreamingCellOperationOutcome::Failed},
            {StreamingCellOperationState::Admitted, StreamingCellOperationTransition::Replace, StreamingCellOperationState::Retiring,
             StreamingCellOperationOutcome::Replaced},
            {StreamingCellOperationState::Admitted, StreamingCellOperationTransition::Shutdown, StreamingCellOperationState::Retiring,
             StreamingCellOperationOutcome::Shutdown},
            {StreamingCellOperationState::Preparing, StreamingCellOperationTransition::Cancel, StreamingCellOperationState::Retiring,
             StreamingCellOperationOutcome::Cancelled},
            {StreamingCellOperationState::Preparing, StreamingCellOperationTransition::Fail, StreamingCellOperationState::Retiring,
             StreamingCellOperationOutcome::Failed},
            {StreamingCellOperationState::Preparing, StreamingCellOperationTransition::Replace, StreamingCellOperationState::Retiring,
             StreamingCellOperationOutcome::Replaced},
            {StreamingCellOperationState::Preparing, StreamingCellOperationTransition::Shutdown, StreamingCellOperationState::Retiring,
             StreamingCellOperationOutcome::Shutdown},
            {StreamingCellOperationState::Activating, StreamingCellOperationTransition::Cancel, StreamingCellOperationState::Retiring,
             StreamingCellOperationOutcome::Cancelled},
            {StreamingCellOperationState::Activating, StreamingCellOperationTransition::Fail, StreamingCellOperationState::Retiring,
             StreamingCellOperationOutcome::Failed},
            {StreamingCellOperationState::Activating, StreamingCellOperationTransition::Replace, StreamingCellOperationState::Retiring,
             StreamingCellOperationOutcome::Replaced},
            {StreamingCellOperationState::Activating, StreamingCellOperationTransition::Shutdown, StreamingCellOperationState::Retiring,
             StreamingCellOperationOutcome::Shutdown},
            {StreamingCellOperationState::Retiring, StreamingCellOperationTransition::AcknowledgeRetirement,
             StreamingCellOperationState::Terminal, StreamingCellOperationOutcome::None, StreamingCellOperationKind::Load, true, true},
        });
    }  // namespace

    /** @copydoc StreamingCellOperationHandle::IsValid */
    bool StreamingCellOperationHandle::IsValid() const noexcept {
        return operation.IsValid() && fence.IsValid();
    }

    /** @copydoc StreamingCellOperation::Create */
    Result<StreamingCellOperation> StreamingCellOperation::Create(StreamingCellOperationHandle handle,
                                                                  const StreamingCellOperationKind kind) {
        if (!handle.IsValid())
            return Failure<StreamingCellOperation>(WorldStreamingErrors::CellOperationInvalid);
        if (!IsKnown(kind))
            return Failure<StreamingCellOperation>(WorldStreamingErrors::CellOperationUnsupported);
        return Result<StreamingCellOperation>::Success(
            StreamingCellOperation{std::move(handle), kind, StreamingCellOperationState::Queued, StreamingCellOperationOutcome::None});
    }

    /** @copydoc StreamingCellOperation::Handle */
    const StreamingCellOperationHandle &StreamingCellOperation::Handle() const noexcept {
        return handle_;
    }

    /** @copydoc StreamingCellOperation::Kind */
    StreamingCellOperationKind StreamingCellOperation::Kind() const noexcept {
        return kind_;
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

        const auto rule = std::ranges::find_if(TransitionRules, [this, transition](const TransitionRule &candidate) {
            return candidate.source == state_ && candidate.transition == transition &&
                   (candidate.anyKind || candidate.requiredKind == kind_);
        });
        if (rule == TransitionRules.end())
            return Failure<StreamingCellOperation>(WorldStreamingErrors::CellOperationTransitionInvalid);
        const auto successorOutcome = rule->retainOutcome ? outcome_ : rule->outcome;
        return Result<StreamingCellOperation>::Success(StreamingCellOperation{handle_, kind_, rule->destination, successorOutcome});
    }

    StreamingCellOperation::StreamingCellOperation(StreamingCellOperationHandle handle, const StreamingCellOperationKind kind,
                                                   const StreamingCellOperationState state,
                                                   const StreamingCellOperationOutcome outcome) noexcept
        : handle_(std::move(handle)), kind_(kind), state_(state), outcome_(outcome) {}
}  // namespace Horo::WorldStreaming
