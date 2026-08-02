#include "Horo/Editor/DefaultWorkspacePanels.h"

#include "Horo/Editor/WorkspacePanelRegistry.h"
#include "editor/screens/workspace/panels/game/GamePanel.h"
#include "editor/screens/workspace/panels/global_dock/GlobalDockPanel.h"
#include "editor/screens/workspace/panels/hierarchy/HierarchyPanel.h"
#include "editor/screens/workspace/panels/input_mapping/InputMappingPanel.h"
#include "editor/screens/workspace/panels/inspector/InspectorPanel.h"
#include "editor/screens/workspace/panels/viewport/ViewportPanel.h"

namespace Horo::Editor {
    void RegisterDefaultWorkspacePanels(WorkspacePanelRegistry &registry) {
        registry.RegisterPanel(std::make_shared<HierarchyPanel>());
        registry.RegisterPanel(std::make_shared<InspectorPanel>());
        registry.RegisterPanel(std::make_shared<InputMappingPanel>());
        registry.RegisterPanel(std::make_shared<GlobalDockPanel>());
        registry.RegisterPanel(std::make_shared<ViewportPanel>());
        registry.RegisterPanel(std::make_shared<GamePanel>());
    }
}  // namespace Horo::Editor
