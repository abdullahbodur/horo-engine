#pragma once

#include "Horo/Editor/EditorGuiContext.h"
#include "Horo/Editor/EditorWorkspaceViewModel.h"
#include "Horo/Editor/WorkspacePanelRegistry.h"
#include "WorkspaceSplitterInteraction.h"

#include <imgui.h>

#include <cstdint>

namespace Horo::Editor
{
struct GuiContentRegion;

class EditorWorkspaceView
{
  public:
    EditorWorkspaceView(const EditorGuiContext &context, const WorkspacePanelRegistry &panelRegistry,
                        std::uintptr_t logoTexture);

    void Draw(const EditorWorkspaceViewModel &viewModel, EditorWorkspaceViewCommandData &outCommand,
              const GuiContentRegion &contentRegion) const;

  private:
    const EditorGuiContext &m_context;
    const WorkspacePanelRegistry &m_panelRegistry;
    std::uintptr_t m_logoTexture;
    mutable WorkspaceSplitterInteraction m_splitterInteraction;

    void DrawMenuBar(const ImVec2 &display, const EditorWorkspaceViewModel &viewModel,
                     EditorWorkspaceViewCommandData &outCommand) const;

    void DrawToolbar(const ImVec2 &pos, const ImVec2 &size, const EditorWorkspaceViewModel &viewModel,
                     EditorWorkspaceViewCommandData &outCommand) const;

    void DrawDockArea(WorkspaceDockArea area, const char *windowId, const ImVec2 &pos, const ImVec2 &size,
                      std::string_view activePanelId, const EditorWorkspaceViewModel &viewModel,
                      EditorWorkspaceViewCommandData &outCommand) const;

    static void DrawActivityBar(const ImVec2 &pos, const ImVec2 &size, const WorkspacePanelRegistry &registry,
                                const EditorWorkspaceViewModel &viewModel, EditorWorkspaceViewCommandData &outCommand,
                                WorkspaceDockArea area, bool indicatorOnRight);
};
} // namespace Horo::Editor
