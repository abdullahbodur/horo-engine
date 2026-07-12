#include "Horo/Editor/WorkspacePanelRegistry.h"

#include <algorithm>

namespace Horo::Editor
{

void WorkspacePanelRegistry::RegisterPanel(std::shared_ptr<IWorkspacePanel> panel)
{
    if (const auto it = std::ranges::find_if(m_panels, [&](const std::shared_ptr<IWorkspacePanel>& p) { return p->GetId() == panel->GetId(); });
        it != m_panels.end())
    {
        *it = std::move(panel);
    }
    else
    {
        m_panels.push_back(std::move(panel));
    }
}

std::vector<std::shared_ptr<IWorkspacePanel>> WorkspacePanelRegistry::GetPanelsForArea(const WorkspaceDockArea area) const
{
    std::vector<std::shared_ptr<IWorkspacePanel>> result;
    for (const auto& panel : m_panels)
    {
        if (panel->GetDefaultDockArea() == area)
        {
            result.push_back(panel);
        }
    }
    return result;
}

const std::vector<std::shared_ptr<IWorkspacePanel>>& WorkspacePanelRegistry::GetAllPanels() const
{
    return m_panels;
}

void WorkspacePanelRegistry::AttachAll(PanelContext& ctx)
{
    for (const auto & panel : m_panels)
    {
        panel->OnAttach(ctx);
    }
}

void WorkspacePanelRegistry::DetachAll()
{
    for (const auto & panel : m_panels)
    {
        panel->OnDetach();
    }
}

void WorkspacePanelRegistry::Clear()
{
    m_panels.clear();
}

} // namespace Horo::Editor
