#include "ViewportInteractionCapture.h"

namespace Horo::Editor {
    ViewportInteractionCapture::ViewportInteractionCapture(IViewportCaptureCancellationSink &sink) noexcept : sink_(&sink) {}

    void ViewportInteractionCapture::Attach(Input::InputRouter &router, Input::InputContextToken &workspaceContext) noexcept {
        router_ = &router;
        workspaceContext_ = &workspaceContext;
    }

    void ViewportInteractionCapture::Detach() noexcept {
        if (router_ != nullptr && pointerCapture_.IsActive())
            router_->CancelCapture(Input::CaptureCancellationReason::OwnerDestroyed);
        Finish();
        workspaceContext_ = nullptr;
        router_ = nullptr;
    }

    bool ViewportInteractionCapture::Begin(const Input::PointerButton button) {
        if (router_ == nullptr || workspaceContext_ == nullptr || pointerCapture_.IsActive())
            return false;
        toolContext_ = router_->PushContext(Input::InputContextId{"editor.viewport.capture"}, Input::InputContextKind::EditorToolCapture);
        Result<Input::PointerCaptureToken> captured = router_->CapturePointer(toolContext_, button, *this);
        if (captured.HasError()) {
            toolContext_.Reset();
            return false;
        }
        pointerCapture_ = std::move(captured).Value();
        return true;
    }

    void ViewportInteractionCapture::Finish() noexcept {
        pointerCapture_.Release();
        toolContext_.Reset();
    }

    void ViewportInteractionCapture::Cancel(const Input::CaptureCancellationReason reason) noexcept {
        if (router_ != nullptr && pointerCapture_.IsActive())
            router_->CancelCapture(reason);
    }

    Input::InputRouter *ViewportInteractionCapture::Router() const noexcept {
        return router_;
    }

    Input::InputContextToken *ViewportInteractionCapture::WorkspaceContext() const noexcept {
        return workspaceContext_;
    }

    void ViewportInteractionCapture::OnInputCaptureCancelled(const Input::CaptureCancellationReason reason) noexcept {
        pointerCapture_ = {};
        toolContext_.Reset();
        if (sink_ != nullptr)
            sink_->OnViewportCaptureCancelled(reason);
    }
}  // namespace Horo::Editor
