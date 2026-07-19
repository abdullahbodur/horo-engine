#pragma once

#include <array>
#include <functional>
#include <memory>
#include <vector>

#include "Horo/Editor/IWorkspacePanel.h"

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
    WorkspacePanelRegistry(WorkspacePanelRegistry &&) noexcept = default;
    WorkspacePanelRegistry &operator=(WorkspacePanelRegistry &&) noexcept = default;
    WorkspacePanelRegistry(const WorkspacePanelRegistry &) = delete;
    WorkspacePanelRegistry &operator=(const WorkspacePanelRegistry &) = delete;

    /**
     * @brief Registers a new panel.
     */
    void RegisterPanel(std::shared_ptr<IWorkspacePanel> panel);

    /**
     * @brief Retrieves all panels assigned to a specific dock area.
     */
    [[nodiscard]] const std::vector<std::shared_ptr<IWorkspacePanel>> &GetPanelsForArea(WorkspaceDockArea area) const;

    /**
     * @brief Retrieves all registered panels.
     */
    [[nodiscard]] const std::vector<std::shared_ptr<IWorkspacePanel>> &GetAllPanels() const;

    /**
     * @brief Attaches all registered panels using the given context.
     *
     * Repeated calls while the registry is attached are ignored.
     */
    void AttachAll(PanelContext &ctx);

    /**
     * @brief Detaches all registered panels.
     *
     * Repeated calls while the registry is detached are ignored.
     */
    void DetachAll();

    /**
     * @brief Unregisters and clears all panels.
     */
    void Clear();

  private:
    void RebuildAreaIndex();

    std::vector<std::shared_ptr<IWorkspacePanel>> m_panels;
    std::array<std::vector<std::shared_ptr<IWorkspacePanel>>, 4> m_panelsByArea;
    bool m_attached = false;
};

} // namespace Horo::Editor
