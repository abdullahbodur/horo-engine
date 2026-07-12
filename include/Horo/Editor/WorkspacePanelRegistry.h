#pragma once

#include "Horo/Editor/EditorWorkspaceViewModel.h"
#include "Horo/Editor/EditorGuiContext.h"

#include <imgui.h>

#include <functional>
#include <string>
#include <vector>

#include "Horo/Editor/IWorkspacePanel.h"
#include <memory>

namespace Horo::Editor
{

/**
 * @brief Registry for workspace panels injected by plugins or default modules.
 */
class WorkspacePanelRegistry
{
public:
    WorkspacePanelRegistry() = default;
    ~WorkspacePanelRegistry() = default;

    /**
     * @brief Registers a new panel.
     */
    void RegisterPanel(std::shared_ptr<IWorkspacePanel> panel);

    /**
     * @brief Retrieves all panels assigned to a specific dock area.
     */
    [[nodiscard]] std::vector<std::shared_ptr<IWorkspacePanel>> GetPanelsForArea(WorkspaceDockArea area) const;

    /**
     * @brief Retrieves all registered panels.
     */
    [[nodiscard]] const std::vector<std::shared_ptr<IWorkspacePanel>>& GetAllPanels() const;

    /**
     * @brief Attaches all registered panels using the given context.
     */
    void AttachAll(PanelContext& ctx);

    /**
     * @brief Detaches all registered panels.
     */
    void DetachAll();

    /**
     * @brief Unregisters and clears all panels.
     */
    void Clear();

private:
    std::vector<std::shared_ptr<IWorkspacePanel>> m_panels;
};

} // namespace Horo::Editor
