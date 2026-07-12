#include "Horo/Editor/DefaultWorkspacePanels.h"
#include "Horo/Editor/WorkspacePanelRegistry.h"

#include "ContentBrowserPanel.h"
#include "HierarchyPanel.h"
#include "InspectorPanel.h"
#include "ViewportPanel.h"

namespace Horo::Editor
{

void RegisterDefaultWorkspacePanels(WorkspacePanelRegistry &registry)
{
    registry.RegisterPanel(std::make_shared<HierarchyPanel>());
    registry.RegisterPanel(std::make_shared<InspectorPanel>());
    registry.RegisterPanel(std::make_shared<ContentBrowserPanel>());
    registry.RegisterPanel(std::make_shared<ViewportPanel>());
}

} // namespace Horo::Editor
