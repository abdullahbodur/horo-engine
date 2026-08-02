#pragma once

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace Horo::Editor
{
using LayoutNodeId = std::string;
using PanelId = std::string;
using TabId = std::string;

enum class WorkspaceSplitAxis : std::uint8_t
{
    Horizontal,
    Vertical,
};

enum class WorkspaceLayoutIssueCode : std::uint8_t
{
    EmptyNodeId,
    EmptyPanelId,
    DuplicateNodeId,
    DuplicatePanelId,
    InvalidSplit,
    ActiveTabMissing,
};

struct WorkspaceLayoutIssue
{
    WorkspaceLayoutIssueCode code;
    std::string nodeId;
    std::string panelId;
};

struct TabPlacement
{
    LayoutNodeId stack;
    std::optional<std::size_t> index;
};

enum class WorkspaceLayoutOperationCode : std::uint8_t
{
    Success,
    UnknownPanel,
    UnknownStack,
    InvalidInsertionIndex,
};

struct WorkspaceLayoutOperationResult
{
    WorkspaceLayoutOperationCode code = WorkspaceLayoutOperationCode::Success;

    [[nodiscard]] bool Succeeded() const noexcept
    {
        return code == WorkspaceLayoutOperationCode::Success;
    }
};

struct LayoutNode;

struct SplitNode
{
    LayoutNodeId id;
    WorkspaceSplitAxis axis = WorkspaceSplitAxis::Horizontal;
    float ratio = 0.5F;
    float firstMinimumSize = 160.0F;
    float secondMinimumSize = 160.0F;
    std::unique_ptr<LayoutNode> first;
    std::unique_ptr<LayoutNode> second;
};

struct TabStackNode
{
    LayoutNodeId id;
    std::vector<TabId> tabs;
    std::optional<TabId> activeTab;
    bool collapsed = false;
};

struct PanelNode
{
    LayoutNodeId id;
    PanelId panel;
};

struct LayoutNode
{
    std::variant<SplitNode, TabStackNode, PanelNode> value;

    LayoutNode() = default;
    LayoutNode(SplitNode node) : value(std::move(node))
    {
    }
    LayoutNode(TabStackNode node) : value(std::move(node))
    {
    }
    LayoutNode(PanelNode node) : value(std::move(node))
    {
    }

    LayoutNode(const LayoutNode &other);
    LayoutNode &operator=(const LayoutNode &other);
    LayoutNode(LayoutNode &&) noexcept = default;
    LayoutNode &operator=(LayoutNode &&) noexcept = default;
};

struct WorkspaceLayout
{
    std::uint32_t schemaVersion = 1;
    LayoutNode root;

    WorkspaceLayout() = default;
    WorkspaceLayout(const WorkspaceLayout &) = default;
    WorkspaceLayout &operator=(const WorkspaceLayout &) = default;
    WorkspaceLayout(WorkspaceLayout &&) noexcept = default;
    WorkspaceLayout &operator=(WorkspaceLayout &&) noexcept = default;

    [[nodiscard]] LayoutNode *FindNode(std::string_view nodeId) noexcept;
    [[nodiscard]] const LayoutNode *FindNode(std::string_view nodeId) const noexcept;
    [[nodiscard]] TabStackNode *FindTabStack(std::string_view stackId) noexcept;
    [[nodiscard]] const TabStackNode *FindTabStack(std::string_view stackId) const noexcept;
    [[nodiscard]] PanelNode *FindPanel(std::string_view panelId) noexcept;
    [[nodiscard]] const PanelNode *FindPanel(std::string_view panelId) const noexcept;
    [[nodiscard]] WorkspaceLayoutOperationResult MoveTab(std::string_view panelId, const TabPlacement &placement);
    [[nodiscard]] WorkspaceLayoutOperationResult CloseTab(std::string_view panelId);
    [[nodiscard]] std::vector<WorkspaceLayoutIssue> Validate() const;
};
} // namespace Horo::Editor
