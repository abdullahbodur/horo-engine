#include "ViewportInteractionController.h"

namespace Horo::Editor {
    ViewportInteractionController::ViewportInteractionController() noexcept : capture_(*this) {}

    void ViewportInteractionController::Attach(Input::InputRouter &router, Input::InputContextToken &workspaceContext) noexcept {
        capture_.Attach(router, workspaceContext);
    }

    void ViewportInteractionController::Detach() noexcept {
        capture_.Detach();
        navigation_.Reset();
        gizmo_.Reset();
    }

    bool ViewportInteractionController::IsActive() const noexcept {
        return navigation_.IsActive() || gizmo_.IsActive();
    }

    void ViewportInteractionController::Draw(ImDrawList &drawList, const ImVec2 &origin, const float width, const float height,
                                             const bool hovered, const EditorWorkspaceViewModel &viewModel,
                                             EditorWorkspaceViewCommandData &command, const EditorGuiContext &context,
                                             const float deltaSeconds, const Math::ClipDepthRange depthRange) {
        const Input::InputRouter *router = capture_.Router();
        if (router == nullptr || capture_.WorkspaceContext() == nullptr)
            return;
        const Input::RawInputSnapshot &input = router->Snapshot();
        if (gizmo_.Draw(drawList, origin, width, height, hovered, input, viewModel, command, capture_, depthRange))
            return;
        navigation_.Update(origin, width, height, hovered, input, viewModel, command, context, deltaSeconds, capture_, depthRange);
    }

    void ViewportInteractionController::OnViewportCaptureCancelled(const Input::CaptureCancellationReason) noexcept {
        navigation_.Reset();
        gizmo_.OnCaptureCancelled();
    }
}  // namespace Horo::Editor
