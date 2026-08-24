#pragma once

#include "Horo/Runtime/Input.h"
#include "TransformGizmoMath.h"
#include "editor/screens/workspace/EditorWorkspaceViewModel.h"

#include <imgui.h>
#include <optional>

namespace Horo::Editor {
    struct TransformGizmoFrameGeometry;
    class ViewportInteractionCapture;

    /**
     * @brief Owns one viewport transform-gizmo interaction session and emits semantic preview/commit commands.
     *
     * The controller owns only transient pointer interaction state. Scene and viewport state remain owned by the
     * workspace controller.
     */
    class TransformGizmoController {
    public:
        void OnCaptureCancelled() noexcept;
        void Reset() noexcept;

        [[nodiscard]] bool IsActive() const noexcept;

        /**
         * @brief Draws the current gizmo and advances its pointer interaction.
         * @return True when the gizmo consumed the viewport interaction for this frame.
         */
        [[nodiscard]] bool Draw(ImDrawList &drawList, const ImVec2 &origin, float width, float height, bool hovered,
                                const Input::RawInputSnapshot &input, const EditorWorkspaceViewModel &viewModel,
                                EditorWorkspaceViewCommandData &command, ViewportInteractionCapture &capture,
                                Math::ClipDepthRange depthRange);  // NOSONAR(cpp:S107)

    private:
        struct DragSession {
            SceneObjectId object;
            EditorTransformTool tool{EditorTransformTool::Move};
            EditorTransformSpace space{EditorTransformSpace::Local};
            int axis{0}; /**< X/Y/Z, or 3 for uniform scale. */
            Math::Transform draftTransform;
            TransformGizmoMathSession math;
            Math::Vec3 currentWorldPosition;
            ImVec2 startMouse{};
            ImVec2 screenDirection{};
        };

        [[nodiscard]] Result<void> TryBeginDrag(const TransformGizmoFrameGeometry &geometry, const Math::Mat4 &worldTransform,
                                                const SceneObject &selectedObject, const EditorWorkspaceViewModel &viewModel,
                                                const Input::RawInputSnapshot &input, const ImVec2 &origin, float width, float height,
                                                ViewportInteractionCapture &capture,
                                                Math::ClipDepthRange depthRange);  // NOSONAR(cpp:S107)
        void AdvanceDrag(const Input::RawInputSnapshot &input, const EditorWorkspaceViewModel &viewModel, const ImVec2 &origin, float width,
                         float height, EditorWorkspaceViewCommandData &command, ViewportInteractionCapture &capture,
                         Math::ClipDepthRange depthRange);  // NOSONAR(cpp:S107)

        std::optional<DragSession> drag_;
        bool cancelPreviewOnNextDraw_{false};
        bool geometryFailureReported_{false};
    };
}  // namespace Horo::Editor
