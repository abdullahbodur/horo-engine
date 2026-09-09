#pragma once

#include "Horo/Editor/IGlobalDockPane.h"

#include <span>

namespace Horo::Editor {
    class WorkspacePanelRegistry;

    /**
     * @brief Registers the built-in workspace panels.
     * @param registry Target workspace-panel registry.
     */
    void RegisterDefaultWorkspacePanels(WorkspacePanelRegistry &registry);

    /**
     * @brief Registers the default workspace panels and module-provided global-dock panes.
     * @param registry Target workspace-panel registry.
     * @param globalDockPaneFactories Composition-time factories for optional global-dock tabs.
     */
    void RegisterDefaultWorkspacePanels(WorkspacePanelRegistry &registry,
                                        std::span<const GlobalDockPaneFactory> globalDockPaneFactories);

}  // namespace Horo::Editor
