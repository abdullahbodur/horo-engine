#pragma once

#include "Horo/Editor/EditorGuiContext.h"
#include <imgui.h>

#include "Horo/Editor/IWorkspacePanel.h"

namespace Horo::Editor
{
class ContentBrowserPanel final : public IWorkspacePanel
{
  public:
    [[nodiscard]] std::string GetId() const override
    {
        return "horo.content_browser";
    }
    [[nodiscard]] std::string GetDisplayName() const override
    {
        return "horo.panel.content_browser.title";
    }
    [[nodiscard]] WorkspaceDockArea GetDefaultDockArea() const override
    {
        return WorkspaceDockArea::Bottom;
    }
    [[nodiscard]] std::vector<std::string> GetObservedEventTypes() const override
    {
        return {};
    }

    void OnAttach(PanelContext &ctx) override
    {
    }

    void OnDetach() override
    {
    }

    void DrawIcon(ImDrawList *dl, const ImVec2 &pos, const ImVec2 &size, ImU32 color) override;

    void DrawPanel(const ImVec2 &pos, const ImVec2 &size, const EditorWorkspaceViewModel &vm,
                   EditorWorkspaceViewCommandData &cmd, const EditorGuiContext &ctx) override;
};
} // namespace Horo::Editor
