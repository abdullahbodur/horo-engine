#pragma once

#include "Horo/Editor/EditorGuiContext.h"
#include "editor/renderer/EditorViewportRenderer.h"
#include "editor/screens/workspace/EditorWorkspaceViewModel.h"

#include <imgui.h>

#include <optional>

#include "Horo/Editor/IWorkspacePanel.h"

namespace Horo::Editor
{
    class ViewportPanel final : public IWorkspacePanel, public Input::IInputCaptureOwner
    {
    public:
        [[nodiscard]] std::string GetId() const override
        {
            return "horo.viewport";
        }

        [[nodiscard]] std::string GetDisplayName() const override
        {
            return "horo.panel.viewport.title";
        }

        [[nodiscard]] WorkspaceDockArea GetDefaultDockArea() const override
        {
            return WorkspaceDockArea::Document;
        }

        [[nodiscard]] std::vector<std::string> GetObservedEventTypes() const override
        {
            return {"SceneDocumentChangedEvent", "SelectionChangedEvent"};
        }

        void OnAttach(PanelContext& ctx) override;
        void OnDetach() override;
        void OnInputCaptureCancelled(Input::CaptureCancellationReason reason) noexcept override;

        void DrawIcon(ImDrawList* dl, const ImVec2& pos, const ImVec2& size, ImU32 color) override;

        void DrawPanel(const ImVec2& pos, const ImVec2& size, const EditorWorkspaceViewModel& vm,
                       EditorWorkspaceViewCommandData& cmd, const EditorGuiContext& ctx) override;

    private:
        enum class NavigationMode
        {
            None,
            Fly,
            Pan,
            Orbit,
        };

        struct TransformGizmoDrag
        {
            SceneObjectId object;
            EditorTransformTool tool{EditorTransformTool::Move};
            EditorTransformSpace space{EditorTransformSpace::Local};
            int axis{0}; /**< X/Y/Z, or 3 for uniform scale. */
            Math::Transform initialTransform;
            Math::Transform draftTransform;
            Math::Mat4 initialWorldTransform{Math::Mat4::Identity()};
            Math::Mat4 parentWorldTransform{Math::Mat4::Identity()};
            Math::Vec3 initialWorldPosition;
            Math::Vec3 currentWorldPosition;
            Math::Vec3 worldAxis;
            Math::Vec3 startPlaneVector;
            Math::Mat4 parentWorldInverse{Math::Mat4::Identity()};
            ImVec2 startMouse{};
            ImVec2 screenDirection{};
            ImVec2 gizmoCenter{};
            float pixelsPerWorldUnit{1.0F};
        };

        [[nodiscard]] bool BeginCapture(Input::PointerButton button);
        void FinishCapture() noexcept;
        void DrawInteraction(ImDrawList* drawList, const ImVec2& origin, float width, float height, bool hovered,
                             const EditorWorkspaceViewModel& viewModel, EditorWorkspaceViewCommandData& command,
                             const EditorGuiContext& context, float deltaSeconds);

        IEditorViewportRenderer* viewportRenderer_{nullptr};
        Input::InputRouter* inputRouter_{nullptr};
        Input::InputContextToken* workspaceInputContext_{nullptr};
        Input::InputContextToken toolInputContext_;
        Input::PointerCaptureToken pointerCapture_;
        NavigationMode navigationMode_{NavigationMode::None};
        std::optional<TransformGizmoDrag> gizmoDrag_;
        bool cancelTransformOnNextDraw_{false};
    };
} // namespace Horo::Editor
