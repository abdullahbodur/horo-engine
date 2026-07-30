#pragma once

#include "editor/screens/workspace/panels/viewport/gizmo/TransformGizmoController.h"
#include "editor/screens/workspace/panels/viewport/interaction/ViewportInteractionCapture.h"
#include "editor/screens/workspace/panels/viewport/navigation/ViewportNavigationController.h"

namespace Horo::Editor {
    /**
     * @brief Coordinates mutually exclusive viewport navigation and transform-gizmo pointer interactions.
     *
     * This object owns transient input sessions only. It emits typed workspace commands and never mutates scene or
     * viewport authorities directly.
     */
    class ViewportInteractionController final : public IViewportCaptureCancellationSink {
    public:
        ViewportInteractionController() noexcept;

        void Attach(Input::InputRouter &router, Input::InputContextToken &workspaceContext) noexcept;
        void Detach() noexcept;

        [[nodiscard]] bool IsActive() const noexcept;

        void Draw(ImDrawList &drawList, const ImVec2 &origin, float width, float height, bool hovered,
                  const EditorWorkspaceViewModel &viewModel, EditorWorkspaceViewCommandData &command, const EditorGuiContext &context,
                  float deltaSeconds, Math::ClipDepthRange depthRange);

        void OnViewportCaptureCancelled(Input::CaptureCancellationReason reason) noexcept override;

    private:
        ViewportInteractionCapture capture_;
        ViewportNavigationController navigation_;
        TransformGizmoController gizmo_;
    };
}  // namespace Horo::Editor
