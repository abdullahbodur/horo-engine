#pragma once

#include "Horo/Editor/EditorGuiContext.h"
#include "Horo/Editor/EditorWorkspaceViewModel.h"
#include "Horo/Editor/WorkspacePanelRegistry.h"

#include <imgui.h>

namespace Horo::Editor
{
class EditorWorkspaceView
{
  public:
    EditorWorkspaceView(const EditorGuiContext &context, const WorkspacePanelRegistry &panelRegistry);

    void Draw(const EditorWorkspaceViewModel &viewModel, EditorWorkspaceViewCommandData &outCommand) const;

  private:
    const EditorGuiContext &m_context;
    const WorkspacePanelRegistry &m_panelRegistry;

    static void DrawMenuBar(const ImVec2 &display, const EditorWorkspaceViewModel &viewModel,
                            EditorWorkspaceViewCommandData &outCommand);
    void DrawToolbar(const ImVec2 &pos, const ImVec2 &size) const;
    void DrawDockArea(WorkspaceDockArea area, const char* windowId, const ImVec2& pos, const ImVec2& size, 
                      std::string_view activePanelId, const EditorWorkspaceViewModel& viewModel, 
                      EditorWorkspaceViewCommandData& outCommand) const;
    static void DrawActivityBarLeft(const ImVec2 &pos, const ImVec2 &size, const WorkspacePanelRegistry &registry, 
                                    std::string_view activePanelId, EditorWorkspaceViewCommandData& outCommand, 
                                    WorkspaceDockArea area);
    static void DrawActivityBarRight(const ImVec2 &pos, const ImVec2 &size, const WorkspacePanelRegistry &registry,
                                     std::string_view activePanelId, EditorWorkspaceViewCommandData& outCommand,
                                     WorkspaceDockArea area);
    static void DrawStatusBar(const ImVec2 &pos, const ImVec2 &size, const EditorWorkspaceViewModel &viewModel);
};
} // namespace Horo::Editor
