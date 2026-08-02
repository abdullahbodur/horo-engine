#pragma once

#include "Horo/Runtime/Input.h"

namespace Horo::Editor {
    /** @brief Receives synchronous cancellation from the viewport pointer-capture owner. */
    class IViewportCaptureCancellationSink {
    public:
        virtual ~IViewportCaptureCancellationSink() = default;
        virtual void OnViewportCaptureCancelled(Input::CaptureCancellationReason reason) noexcept = 0;
    };

    /** @brief Owns the routed input context and exclusive pointer token for one viewport interaction. */
    class ViewportInteractionCapture final : public Input::IInputCaptureOwner {
    public:
        explicit ViewportInteractionCapture(IViewportCaptureCancellationSink &sink) noexcept;

        void Attach(Input::InputRouter &router, Input::InputContextToken &workspaceContext) noexcept;
        void Detach() noexcept;

        [[nodiscard]] bool Begin(Input::PointerButton button);
        void Finish() noexcept;
        void Cancel(Input::CaptureCancellationReason reason) noexcept;

        [[nodiscard]] Input::InputRouter *Router() const noexcept;
        [[nodiscard]] Input::InputContextToken *WorkspaceContext() const noexcept;

        void OnInputCaptureCancelled(Input::CaptureCancellationReason reason) noexcept override;

    private:
        IViewportCaptureCancellationSink *sink_{nullptr};
        Input::InputRouter *router_{nullptr};
        Input::InputContextToken *workspaceContext_{nullptr};
        Input::InputContextToken toolContext_;
        Input::PointerCaptureToken pointerCapture_;
    };
}  // namespace Horo::Editor
