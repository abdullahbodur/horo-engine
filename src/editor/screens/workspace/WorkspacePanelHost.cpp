#include "Horo/Editor/WorkspacePanelHost.h"

#include <algorithm>
#include <type_traits>
#include <variant>

namespace Horo::Editor {
    namespace {
        LayoutNode MakeDefaultLayout() {
            auto left = std::make_unique<LayoutNode>(
                TabStackNode{.id = "workspace.left", .tabs = {"horo.hierarchy"}, .activeTab = "horo.hierarchy"});
            auto document = std::make_unique<LayoutNode>(
                TabStackNode{.id = "workspace.document", .tabs = {"horo.viewport"}, .activeTab = "horo.viewport"});
            auto right = std::make_unique<LayoutNode>(
                TabStackNode{.id = "workspace.right", .tabs = {"horo.inspector"}, .activeTab = "horo.inspector"});

            auto documentAndRight = std::make_unique<LayoutNode>(SplitNode{.id = "workspace.document_right",
                                                                           .axis = WorkspaceSplitAxis::Horizontal,
                                                                           .ratio = 0.72F,
                                                                           .first = std::move(document),
                                                                           .second = std::move(right)});

            return LayoutNode(SplitNode{.id = "workspace.root",
                                        .axis = WorkspaceSplitAxis::Horizontal,
                                        .ratio = 0.24F,
                                        .first = std::move(left),
                                        .second = std::move(documentAndRight)});
        }

        bool ContainsPanel(const LayoutNode &node, const std::string_view panelId) {
            return std::visit([&]<typename NodeValue>(const NodeValue &value) {
                using NodeType = std::decay_t<NodeValue>;
                if constexpr (std::is_same_v<NodeType, SplitNode>) {
                    return (value.first != nullptr && ContainsPanel(*value.first, panelId)) ||
                           (value.second != nullptr && ContainsPanel(*value.second, panelId));
                } else if constexpr (std::is_same_v<NodeType, TabStackNode>) {
                    return std::ranges::find(value.tabs, panelId) != value.tabs.end();
                } else {
                    return value.panel == panelId;
                }
            }, node.value);
        }

        bool SplitAround(LayoutNode &node, const std::string_view targetNodeId, const std::string_view panelId,
                         const WorkspacePanelHost::DropKind kind) {
            if (const bool isTarget = std::visit(
                    [&]<typename NodeValue>(const NodeValue &value) {
                return value.id == targetNodeId;
            }, node.value);
                isTarget) {
                const bool horizontal = kind == WorkspacePanelHost::DropKind::SplitLeft || kind == WorkspacePanelHost::DropKind::SplitRight;
                const bool panelFirst = kind == WorkspacePanelHost::DropKind::SplitLeft || kind == WorkspacePanelHost::DropKind::SplitTop;
                auto original = std::make_unique<LayoutNode>(std::move(node));
                auto panel = std::make_unique<LayoutNode>(
                    PanelNode{.id = std::string(targetNodeId) + ".panel." + std::string(panelId), .panel = std::string(panelId)});
                SplitNode split{.id = std::string(targetNodeId) + ".split." + std::string(panelId),
                                .axis = horizontal ? WorkspaceSplitAxis::Horizontal : WorkspaceSplitAxis::Vertical,
                                .ratio = 0.5F};
                if (panelFirst) {
                    split.first = std::move(panel);
                    split.second = std::move(original);
                } else {
                    split.first = std::move(original);
                    split.second = std::move(panel);
                }
                node = LayoutNode(std::move(split));
                return true;
            }

            return std::visit([&]<typename NodeValue>(NodeValue &value) {
                using NodeType = std::decay_t<NodeValue>;
                if constexpr (std::is_same_v<NodeType, SplitNode>) {
                    return (value.first != nullptr && SplitAround(*value.first, targetNodeId, panelId, kind)) ||
                           (value.second != nullptr && SplitAround(*value.second, targetNodeId, panelId, kind));
                }
                return false;
            }, node.value);
        }
    }  // namespace

    WorkspacePanelHost::WorkspacePanelHost() {
        m_layout.root = MakeDefaultLayout();
    }

    WorkspaceLayoutOperationResult WorkspacePanelHost::OpenPanel(const std::string_view panelId, const std::string_view stackId) {
        using enum WorkspaceLayoutOperationCode;
        if (panelId.empty())
            return {UnknownPanel};
        auto *stack = m_layout.FindTabStack(stackId);
        if (stack == nullptr)
            return {UnknownStack};
        if (ContainsPanel(m_layout.root, panelId))
            return {UnknownPanel};
        stack->tabs.emplace_back(panelId);
        stack->activeTab = stack->tabs.back();
        return {};
    }

    WorkspaceLayoutOperationResult WorkspacePanelHost::MovePanel(const std::string_view panelId, const TabPlacement &placement) {
        return m_layout.MoveTab(panelId, placement);
    }

    WorkspaceLayoutOperationResult WorkspacePanelHost::ClosePanel(const std::string_view panelId) {
        return m_layout.CloseTab(panelId);
    }

    WorkspaceLayoutOperationResult WorkspacePanelHost::SetActiveTab(const std::string_view stackId, const std::string_view panelId) {
        using enum WorkspaceLayoutOperationCode;
        auto *stack = m_layout.FindTabStack(stackId);
        if (stack == nullptr)
            return {UnknownStack};
        if (std::ranges::find(stack->tabs, panelId) == stack->tabs.end()) {
            return {UnknownPanel};
        }
        stack->activeTab = std::string(panelId);
        return {};
    }

    WorkspaceLayoutOperationResult WorkspacePanelHost::DockPanel(const std::string_view panelId, const std::string_view targetNodeId,
                                                                 const DropKind kind) {
        using enum WorkspaceLayoutOperationCode;
        if (panelId.empty())
            return {UnknownPanel};
        if (m_layout.FindNode(targetNodeId) == nullptr)
            return {UnknownStack};

        const WorkspaceLayout backup = m_layout;
        const bool alreadyPresent = ContainsPanel(m_layout.root, panelId);
        if (kind == DropKind::TabCenter) {
            auto *stack = m_layout.FindTabStack(targetNodeId);
            if (stack == nullptr)
                return {UnknownStack};
            if (alreadyPresent) {
                const auto result = m_layout.MoveTab(panelId, TabPlacement{std::string(targetNodeId), std::nullopt});
                if (!result.Succeeded())
                    m_layout = backup;
                return result;
            }
            stack->tabs.emplace_back(panelId);
            stack->activeTab = stack->tabs.back();
            return {};
        }

        if (alreadyPresent && !m_layout.CloseTab(panelId).Succeeded()) {
            m_layout = backup;
            return {UnknownPanel};
        }
        if (!SplitAround(m_layout.root, targetNodeId, panelId, kind)) {
            m_layout = backup;
            return {UnknownStack};
        }
        return {};
    }

    bool WorkspacePanelHost::SaveLayout(const std::filesystem::path &path, std::string *error) const {
        return WorkspaceLayoutPersistence::Save(path, m_layout, error);
    }

    bool WorkspacePanelHost::RestoreLayout(const std::filesystem::path &path, std::string *error) {
        auto restored = WorkspaceLayoutPersistence::Load(path, error);
        if (!restored.has_value())
            return false;
        m_layout = std::move(*restored);
        return true;
    }

}  // namespace Horo::Editor
