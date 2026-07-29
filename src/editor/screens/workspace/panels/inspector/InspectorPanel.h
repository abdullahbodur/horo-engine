#pragma once

#include "Horo/Editor/EditorGuiContext.h"
#include "Horo/Editor/IWorkspacePanel.h"
#include "editor/screens/workspace/panels/inspector/InspectorEditSession.h"

#include <imgui.h>

namespace Horo::Editor {
    class InspectorPanel final : public IWorkspacePanel {
    public:
        [[nodiscard]] std::string GetId() const override {
            return "horo.inspector";
        }

        [[nodiscard]] std::string GetDisplayName() const override {
            return "horo.panel.inspector.title";
        }

        [[nodiscard]] WorkspaceDockArea GetDefaultDockArea() const override {
            return WorkspaceDockArea::Right;
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
        void DrawSelectedObject(const SceneObject &object, DocumentRevision revision, EditorWorkspaceViewCommandData &command,
                                const EditorGuiContext &context);
        [[nodiscard]] InspectorNameEdit DrawObjectTitleWidgets(const SceneObject &object, const EditorGuiContext &context);
        [[nodiscard]] InspectorTransformEdit DrawTransformWidgets(const EditorGuiContext &context);
        [[nodiscard]] InspectorCameraEdit DrawCameraWidgets(const EditorGuiContext &context);
        void ApplyNameEdit(const InspectorNameEdit &edit, const SceneObject &object, EditorWorkspaceViewCommandData &command);
        void ApplyTransformEdit(const InspectorTransformEdit &edit, const SceneObject &object, EditorWorkspaceViewCommandData &command);
        void ApplyCameraEdit(const InspectorCameraEdit &edit, const SceneObject &object, EditorWorkspaceViewCommandData &command);
        void DrawEmptyState(EditorWorkspaceViewCommandData &command, const EditorGuiContext &context);
        static void AdoptCommand(EditorWorkspaceViewCommandData &destination, EditorWorkspaceViewCommandData source);

        Input::InputRouter *m_inputRouter{nullptr};
        Input::InputContextToken m_nameInputContext;
        InspectorEditSession m_editSession;
    };
}  // namespace Horo::Editor
