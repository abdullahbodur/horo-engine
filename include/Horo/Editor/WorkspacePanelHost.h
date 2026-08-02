#pragma once

#include "Horo/Editor/WorkspaceLayout.h"
#include "Horo/Editor/WorkspaceLayoutPersistence.h"

#include <string_view>

namespace Horo::Editor
{
/** @brief Owns the editor workspace layout and applies validated panel mutations. */
class WorkspacePanelHost
{
  public:
    enum class DropKind : std::uint8_t
    {
        TabCenter,
        SplitLeft,
        SplitRight,
        SplitTop,
        SplitBottom,
    };

    WorkspacePanelHost();

    [[nodiscard]] WorkspaceLayout &Layout() noexcept
    {
        return m_layout;
    }
    [[nodiscard]] const WorkspaceLayout &Layout() const noexcept
    {
        return m_layout;
    }

    /** @brief Opens a panel in an existing tab stack and activates it. */
    [[nodiscard]] WorkspaceLayoutOperationResult OpenPanel(std::string_view panelId, std::string_view stackId);

    /** @brief Moves an existing panel tab to another stack and activates it. */
    [[nodiscard]] WorkspaceLayoutOperationResult MovePanel(std::string_view panelId, const TabPlacement &placement);

    /** @brief Closes a panel tab while preserving the stack active-tab invariant. */
    [[nodiscard]] WorkspaceLayoutOperationResult ClosePanel(std::string_view panelId);

    /** @brief Activates an existing tab in a stack. */
    [[nodiscard]] WorkspaceLayoutOperationResult SetActiveTab(std::string_view stackId, std::string_view panelId);

    /** @brief Docks a panel into a tab stack or creates a split around a target node. */
    [[nodiscard]] WorkspaceLayoutOperationResult DockPanel(std::string_view panelId, std::string_view targetNodeId,
                                                           DropKind kind);

    [[nodiscard]] bool SaveLayout(const std::filesystem::path &path, std::string *error = nullptr) const;
    [[nodiscard]] bool RestoreLayout(const std::filesystem::path &path, std::string *error = nullptr);

  private:
    WorkspaceLayout m_layout;
};
} // namespace Horo::Editor
