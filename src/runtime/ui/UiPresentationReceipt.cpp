#include "Horo/Runtime/Ui/UiPresentationReceipt.h"

#include "Horo/Runtime/Ui/UiErrors.h"

namespace Horo::Runtime::Ui {
    namespace {
        template <typename T> [[nodiscard]] Result<T> Failure(const ErrorCodeDescriptor &descriptor) {
            return Result<T>::Failure(MakeError(descriptor));
        }
    }  // namespace

    /** @copydoc UiPresentationReceipt::IsValid */
    bool UiPresentationReceipt::IsValid() const noexcept {
        if (!view.IsValid() || !canvas.IsValid() || !interactionRevision.IsValid() || !snapshotRevision.IsValid())
            return false;
        if (outcome == UiPresentationOutcome::Presented)
            return reason == UiPresentationReason::None;
        if (outcome == UiPresentationOutcome::Skipped)
            return reason >= UiPresentationReason::Suppressed && reason <= UiPresentationReason::ViewReplaced;
        return outcome == UiPresentationOutcome::Failed && reason >= UiPresentationReason::ResourceUnavailable &&
               reason <= UiPresentationReason::ExecutionFailure;
    }

    /** @copydoc UiPresentedInteractionState::Create */
    Result<UiPresentedInteractionState> UiPresentedInteractionState::Create(const UiRenderViewId view, const UiCanvasInstanceId canvas) {
        if (!view.IsValid() || !canvas.IsValid())
            return Failure<UiPresentedInteractionState>(UiErrors::HandleMalformed);
        if (view.ownership != canvas.ownership)
            return Failure<UiPresentedInteractionState>(UiErrors::HandleOwnerMismatch);
        return Result<UiPresentedInteractionState>::Success(UiPresentedInteractionState(view, canvas));
    }

    /** @copydoc UiPresentedInteractionState::Apply */
    Result<bool> UiPresentedInteractionState::Apply(const UiPresentationReceipt &receipt) {
        if (!receipt.IsValid())
            return Failure<bool>(UiErrors::RenderPresentationInvalid);
        if (receipt.view != view_ || receipt.canvas != canvas_)
            return Failure<bool>(UiErrors::HandleOwnerMismatch);
        if (lastObservedSnapshot_.IsValid() && receipt.snapshotRevision.Compare(lastObservedSnapshot_) != UiRevisionRelation::Newer)
            return Failure<bool>(UiErrors::RenderPresentationStale);
        if (lastPresentedInteraction_.IsValid() &&
            receipt.interactionRevision.Compare(lastPresentedInteraction_) == UiRevisionRelation::Older)
            return Failure<bool>(UiErrors::RenderPresentationStale);

        lastObservedSnapshot_ = receipt.snapshotRevision;
        if (receipt.outcome != UiPresentationOutcome::Presented)
            return Result<bool>::Success(false);

        const bool advanced = !lastPresentedInteraction_.IsValid() ||
                              receipt.interactionRevision.Compare(lastPresentedInteraction_) == UiRevisionRelation::Newer;
        lastPresentedInteraction_ = receipt.interactionRevision;
        return Result<bool>::Success(advanced);
    }

    /** @copydoc UiPresentedInteractionState::LastObservedSnapshot */
    UiRenderSnapshotRevision UiPresentedInteractionState::LastObservedSnapshot() const noexcept {
        return lastObservedSnapshot_;
    }

    /** @copydoc UiPresentedInteractionState::LastPresentedInteraction */
    UiInteractionRevision UiPresentedInteractionState::LastPresentedInteraction() const noexcept {
        return lastPresentedInteraction_;
    }

    UiPresentedInteractionState::UiPresentedInteractionState(const UiRenderViewId view, const UiCanvasInstanceId canvas) noexcept
        : view_(view), canvas_(canvas) {}
}  // namespace Horo::Runtime::Ui
