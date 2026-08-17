#pragma once

#include "Horo/Editor/EditorGuiContext.h"
#include "Horo/Editor/WorkspacePanelRegistry.h"
#include "WorkspaceSplitterInteraction.h"
#include "editor/screens/workspace/EditorWorkspaceViewModel.h"

#include <cstdint>

namespace Horo::Editor {
    struct GuiContentRegion;

    class EditorWorkspaceView final : public Input::IInputCaptureOwner {
    public:
        EditorWorkspaceView(const EditorGuiContext &context, const WorkspacePanelRegistry &panelRegistry, std::uintptr_t logoTexture,
                            Input::InputRouter &inputRouter, Input::InputContextToken &workspaceInputContext);

        void Draw(const EditorWorkspaceViewModel &viewModel, EditorWorkspaceViewCommandData &outCommand,
                  const GuiContentRegion &contentRegion);
        void OnInputCaptureCancelled(Input::CaptureCancellationReason reason) noexcept override;

    private:
        struct ActivityBarOptions {
            WorkspaceDockArea area;
            bool indicatorOnRight;
            bool allowDragSources;
        };

        struct ActivityBarGeometry {
            float cellX;
            float contentY;
            float cellSize;
            ImDrawList *drawList;
        };

        const EditorGuiContext &m_context;
        const WorkspacePanelRegistry &m_panelRegistry;
        std::uintptr_t m_logoTexture;
        Input::InputRouter &m_inputRouter;
        Input::InputContextToken &m_workspaceInputContext;
        mutable WorkspaceSplitterInteraction m_splitterInteraction;
        mutable Input::InputContextToken m_panelDragContext;
        mutable Input::PointerCaptureToken m_panelDragCapture;

        [[nodiscard]] bool EnsurePanelDragCapture();
        [[nodiscard]] bool PanelDragEligible() const noexcept;

        void DrawMenuBar(const ImVec2 &display, const EditorWorkspaceViewModel &viewModel,
                         EditorWorkspaceViewCommandData &outCommand) const;

        void DrawToolbar(const ImVec2 &pos, const ImVec2 &size, const EditorWorkspaceViewModel &viewModel,
                         EditorWorkspaceViewCommandData &outCommand);
        void DrawDocumentRail(const ImVec2 &pos, const ImVec2 &size, float centerY, float minimumX, float maximumX,
                              const EditorWorkspaceViewModel &viewModel, EditorWorkspaceViewCommandData &outCommand);
        void DrawDocumentRailItem(const std::string &panelId, const std::shared_ptr<IWorkspacePanel> &panel, float tabX, float centerY,
                                  const EditorWorkspaceViewModel &viewModel, EditorWorkspaceViewCommandData &outCommand);

        void DrawRecoveryBar(const ImVec2 &pos, const ImVec2 &size, EditorWorkspaceViewCommandData &outCommand) const;

        void DrawExternalConflictBar(const ImVec2 &pos, const ImVec2 &size, EditorWorkspaceViewCommandData &outCommand) const;

        void DrawDockArea(WorkspaceDockArea area, const char *windowId, const ImVec2 &pos, const ImVec2 &size,
                          std::string_view activePanelId, const EditorWorkspaceViewModel &viewModel,
                          EditorWorkspaceViewCommandData &outCommand);
        void DrawMiddleAndBottomDocks(float curY, float leftActivityW, float hierarchyW, float inspectorW, float centerW, float bottomDockW,
                                      float mainH, float contentH, const EditorWorkspaceViewModel &viewModel,
                                      EditorWorkspaceViewCommandData &outCommand);
        void DrawWorkspaceDropTarget(const char *targetNodeId, const char *id, const ImVec2 &position, const ImVec2 &size,
                                     WorkspacePanelHost::DropKind kind, EditorWorkspaceViewCommandData &outCommand) const;

        void DrawActivityBar(const ImVec2 &pos, const ImVec2 &size, const WorkspacePanelRegistry &registry,
                             const EditorWorkspaceViewModel &viewModel, EditorWorkspaceViewCommandData &outCommand,
                             ActivityBarOptions options);
        void DrawActivityBarGroup(std::size_t groupIndex, const ActivityBarGroup &group, float groupTop, float groupBottom,
                                  const ImVec2 &pos, const ImVec2 &size, const ActivityBarGeometry &geometry,
                                  const ActivityBarOptions &options, const EditorWorkspaceViewModel &viewModel,
                                  EditorWorkspaceViewCommandData &outCommand, bool draggingActivityItem);
        bool DrawActivityDropSlot(ActivityBarSlot slot, float y, bool draggingActivityItem, const ActivityBarGeometry &geometry,
                                  EditorWorkspaceViewCommandData &outCommand) const;
        float DrawActivityItem(const std::string &panelId, float y, const ActivityBarGeometry &geometry,
                               const EditorWorkspaceViewModel &viewModel, EditorWorkspaceViewCommandData &outCommand,
                               ActivityBarOptions options, const std::shared_ptr<IWorkspacePanel> &panel);
    };
}  // namespace Horo::Editor
