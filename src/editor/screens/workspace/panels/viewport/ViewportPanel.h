#pragma once

#include "Horo/Editor/EditorGuiContext.h"
#include "Horo/Editor/IWorkspacePanel.h"
#include "editor/renderer/EditorViewportRenderer.h"
#include "editor/screens/workspace/EditorWorkspaceViewModel.h"
#include "editor/screens/workspace/panels/viewport/interaction/ViewportInteractionController.h"

#include <imgui.h>

namespace Horo::Editor {
    class ViewportPanel final : public IWorkspacePanel {
    public:
        [[nodiscard]] std::string GetId() const override {
            return "horo.viewport";
        }

        [[nodiscard]] std::string GetDisplayName() const override {
            return "horo.panel.viewport.title";
        }

        [[nodiscard]] WorkspaceDockArea GetDefaultDockArea() const override {
            return WorkspaceDockArea::Document;
        }

        [[nodiscard]] std::vector<std::string> GetObservedEventTypes() const override {
            return {"SceneDocumentChangedEvent", "SelectionChangedEvent"};
        }

        void OnAttach(PanelContext &ctx) override;
        void OnDetach() override;

        void DrawIcon(ImDrawList *dl, const ImVec2 &pos, const ImVec2 &size, ImU32 color) override;

        void DrawPanel(const ImVec2 &pos, const ImVec2 &size, const EditorWorkspaceViewModel &vm, EditorWorkspaceViewCommandData &cmd,
                       const EditorGuiContext &ctx) override;

    private:
        void RequestViewportExtent(float width, float height) const noexcept;
        static void DrawViewportSurface(ImDrawList &drawList, const ImVec2 &origin, float width, float height, float centerX, float horizon,
                                        float ground, const EditorViewportTextureView &textureView, bool hasRenderedViewport);
        static void DrawProjectionControl(const ImVec2 &origin, const EditorWorkspaceViewModel &viewModel,
                                          EditorWorkspaceViewCommandData &command, const EditorGuiContext &context);
        static void DrawObjectCount(const ImVec2 &origin, const EditorWorkspaceViewModel &viewModel, const EditorGuiContext &context);
        static void DrawMissingRendererMessage(float centerX, float originY, float height, const EditorGuiContext &context);

        IEditorViewportRenderer *viewportRenderer_{nullptr};
        ViewportInteractionController interaction_;
    };
}  // namespace Horo::Editor
