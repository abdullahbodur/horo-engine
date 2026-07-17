#include "Horo/Editor/WorkspaceLayout.h"

#include <algorithm>
#include <type_traits>
#include <unordered_set>
#include <utility>

namespace Horo::Editor
{
    namespace
    {
        LayoutNode CloneNode(const LayoutNode& node)
        {
            return std::visit(
                [](const auto& value) -> LayoutNode
                {
                    using Value = std::decay_t<decltype(value)>;
                    if constexpr (std::is_same_v<Value, SplitNode>)
                    {
                        SplitNode copy;
                        copy.id = value.id;
                        copy.axis = value.axis;
                        copy.ratio = value.ratio;
                        copy.firstMinimumSize = value.firstMinimumSize;
                        copy.secondMinimumSize = value.secondMinimumSize;
                        if (value.first)
                        {
                            copy.first = std::make_unique<LayoutNode>(CloneNode(*value.first));
                        }
                        if (value.second)
                        {
                            copy.second = std::make_unique<LayoutNode>(CloneNode(*value.second));
                        }
                        return LayoutNode{std::move(copy)};
                    }
                    else
                    {
                        return LayoutNode{value};
                    }
                },
                node.value);
        }

        LayoutNode* FindNode(LayoutNode& node, const std::string_view nodeId)
        {
            const bool matches = std::visit([nodeId](const auto& value) { return value.id == nodeId; }, node.value);
            if (matches)
            {
                return &node;
            }

            if (const auto* split = std::get_if<SplitNode>(&node.value))
            {
                if (split->first)
                {
                    if (auto* found = FindNode(*split->first, nodeId))
                    {
                        return found;
                    }
                }
                if (split->second)
                {
                    if (auto* found = FindNode(*split->second, nodeId))
                    {
                        return found;
                    }
                }
            }
            return nullptr;
        }

        const LayoutNode* FindNode(const LayoutNode& node, const std::string_view nodeId)
        {
            return FindNode(const_cast<LayoutNode&>(node), nodeId);
        }

        TabStackNode* FindTabStack(LayoutNode& node, const std::string_view stackId)
        {
            if (auto* stack = std::get_if<TabStackNode>(&node.value); stack != nullptr && stack->id == stackId)
            {
                return stack;
            }

            if (const auto* split = std::get_if<SplitNode>(&node.value))
            {
                if (split->first)
                {
                    if (auto* found = FindTabStack(*split->first, stackId))
                    {
                        return found;
                    }
                }
                if (split->second)
                {
                    if (auto* found = FindTabStack(*split->second, stackId))
                    {
                        return found;
                    }
                }
            }
            return nullptr;
        }

        const TabStackNode* FindTabStack(const LayoutNode& node, const std::string_view stackId)
        {
            return FindTabStack(const_cast<LayoutNode&>(node), stackId);
        }

        TabStackNode* FindTabContaining(LayoutNode& node, const std::string_view panelId, std::size_t& index)
        {
            if (auto* stack = std::get_if<TabStackNode>(&node.value))
            {
                const auto it = std::find(stack->tabs.begin(), stack->tabs.end(), panelId);
                if (it != stack->tabs.end())
                {
                    index = static_cast<std::size_t>(std::distance(stack->tabs.begin(), it));
                    return stack;
                }
            }

            if (const auto* split = std::get_if<SplitNode>(&node.value))
            {
                if (split->first)
                {
                    if (auto* found = FindTabContaining(*split->first, panelId, index))
                    {
                        return found;
                    }
                }
                if (split->second)
                {
                    if (auto* found = FindTabContaining(*split->second, panelId, index))
                    {
                        return found;
                    }
                }
            }
            return nullptr;
        }

        void SelectReplacementTab(TabStackNode& stack, const std::size_t removedIndex,
                                  const std::string_view removedPanel)
        {
            if (stack.activeTab != removedPanel)
            {
                return;
            }

            if (stack.tabs.empty())
            {
                stack.activeTab.reset();
                return;
            }

            const std::size_t replacementIndex = (std::min)(removedIndex, stack.tabs.size() - 1);
            stack.activeTab = stack.tabs[replacementIndex];
        }

        PanelNode* FindPanel(LayoutNode& node, const std::string_view panelId)
        {
            if (auto* panel = std::get_if<PanelNode>(&node.value); panel != nullptr && panel->panel == panelId)
            {
                return panel;
            }

            if (const auto* split = std::get_if<SplitNode>(&node.value))
            {
                if (split->first)
                {
                    if (auto* found = FindPanel(*split->first, panelId))
                    {
                        return found;
                    }
                }
                if (split->second)
                {
                    if (auto* found = FindPanel(*split->second, panelId))
                    {
                        return found;
                    }
                }
            }
            return nullptr;
        }

        const PanelNode* FindPanel(const LayoutNode& node, const std::string_view panelId)
        {
            return FindPanel(const_cast<LayoutNode&>(node), panelId);
        }

        void ValidateNode(const LayoutNode& node, std::vector<WorkspaceLayoutIssue>& issues,
                          std::unordered_set<std::string>& nodeIds, std::unordered_set<std::string>& panelIds)
        {
            std::visit(
                [&](const auto& value)
                {
                    if (value.id.empty())
                    {
                        issues.push_back({WorkspaceLayoutIssueCode::EmptyNodeId, {}, {}});
                    }
                    else if (!nodeIds.insert(value.id).second)
                    {
                        issues.push_back({WorkspaceLayoutIssueCode::DuplicateNodeId, value.id, {}});
                    }

                    using Value = std::decay_t<decltype(value)>;
                    if constexpr (std::is_same_v<Value, PanelNode>)
                    {
                        if (value.panel.empty())
                        {
                            issues.push_back({WorkspaceLayoutIssueCode::EmptyPanelId, value.id, {}});
                        }
                        else if (!panelIds.insert(value.panel).second)
                        {
                            issues.push_back({WorkspaceLayoutIssueCode::DuplicatePanelId, value.id, value.panel});
                        }
                    }
                    else if constexpr (std::is_same_v<Value, TabStackNode>)
                    {
                        if (value.activeTab.has_value() &&
                            std::find(value.tabs.begin(), value.tabs.end(), *value.activeTab) == value.tabs.end())
                        {
                            issues.push_back({WorkspaceLayoutIssueCode::ActiveTabMissing, value.id, *value.activeTab});
                        }
                        for (const auto& tab : value.tabs)
                        {
                            if (tab.empty() || !panelIds.insert(tab).second)
                            {
                                issues.push_back({WorkspaceLayoutIssueCode::DuplicatePanelId, value.id, tab});
                            }
                        }
                    }
                    else if constexpr (std::is_same_v<Value, SplitNode>)
                    {
                        if (value.first == nullptr || value.second == nullptr || value.ratio <= 0.0F || value.ratio >=
                            1.0F)
                        {
                            issues.push_back({WorkspaceLayoutIssueCode::InvalidSplit, value.id, {}});
                        }
                        if (value.first)
                        {
                            ValidateNode(*value.first, issues, nodeIds, panelIds);
                        }
                        if (value.second)
                        {
                            ValidateNode(*value.second, issues, nodeIds, panelIds);
                        }
                    }
                },
                node.value);
        }
    } // namespace

    LayoutNode::LayoutNode(const LayoutNode& other) : value(std::move(CloneNode(other).value))
    {
    }

    LayoutNode& LayoutNode::operator=(const LayoutNode& other)
    {
        if (this != &other)
        {
            value = CloneNode(other).value;
        }
        return *this;
    }

    LayoutNode* WorkspaceLayout::FindNode(const std::string_view nodeId) noexcept
    {
        return ::Horo::Editor::FindNode(root, nodeId);
    }

    const LayoutNode* WorkspaceLayout::FindNode(const std::string_view nodeId) const noexcept
    {
        return ::Horo::Editor::FindNode(root, nodeId);
    }

    TabStackNode* WorkspaceLayout::FindTabStack(const std::string_view stackId) noexcept
    {
        return ::Horo::Editor::FindTabStack(root, stackId);
    }

    const TabStackNode* WorkspaceLayout::FindTabStack(const std::string_view stackId) const noexcept
    {
        return ::Horo::Editor::FindTabStack(root, stackId);
    }

    PanelNode* WorkspaceLayout::FindPanel(const std::string_view panelId) noexcept
    {
        return ::Horo::Editor::FindPanel(root, panelId);
    }

    const PanelNode* WorkspaceLayout::FindPanel(const std::string_view panelId) const noexcept
    {
        return ::Horo::Editor::FindPanel(root, panelId);
    }

    WorkspaceLayoutOperationResult WorkspaceLayout::MoveTab(const std::string_view panelId,
                                                            const TabPlacement& placement)
    {
        std::size_t sourceIndex = 0;
        TabStackNode* source = FindTabContaining(root, panelId, sourceIndex);
        if (source == nullptr)
        {
            return {WorkspaceLayoutOperationCode::UnknownPanel};
        }

        TabStackNode* target = ::Horo::Editor::FindTabStack(root, placement.stack);
        if (target == nullptr)
        {
            return {WorkspaceLayoutOperationCode::UnknownStack};
        }

        const std::size_t requestedIndex = placement.index.value_or(target->tabs.size());
        if (requestedIndex > target->tabs.size())
        {
            return {WorkspaceLayoutOperationCode::InvalidInsertionIndex};
        }

        if (source == target)
        {
            if (requestedIndex == sourceIndex || requestedIndex == sourceIndex + 1)
            {
                source->activeTab = std::string{panelId};
                return {};
            }

            std::string moved = std::move(source->tabs[sourceIndex]);
            source->tabs.erase(source->tabs.begin() + static_cast<std::ptrdiff_t>(sourceIndex));
            const std::size_t adjustedIndex = requestedIndex > sourceIndex ? requestedIndex - 1 : requestedIndex;
            source->tabs.insert(source->tabs.begin() + static_cast<std::ptrdiff_t>(adjustedIndex), std::move(moved));
            source->activeTab = std::string{panelId};
            return {};
        }

        std::string moved = std::move(source->tabs[sourceIndex]);
        source->tabs.erase(source->tabs.begin() + static_cast<std::ptrdiff_t>(sourceIndex));
        SelectReplacementTab(*source, sourceIndex, panelId);
        target->tabs.insert(target->tabs.begin() + static_cast<std::ptrdiff_t>(requestedIndex), std::move(moved));
        target->activeTab = std::string{panelId};
        return {};
    }

    WorkspaceLayoutOperationResult WorkspaceLayout::CloseTab(const std::string_view panelId)
    {
        std::size_t sourceIndex = 0;
        TabStackNode* source = FindTabContaining(root, panelId, sourceIndex);
        if (source == nullptr)
        {
            return {WorkspaceLayoutOperationCode::UnknownPanel};
        }

        source->tabs.erase(source->tabs.begin() + static_cast<std::ptrdiff_t>(sourceIndex));
        SelectReplacementTab(*source, sourceIndex, panelId);
        return {};
    }

    std::vector<WorkspaceLayoutIssue> WorkspaceLayout::Validate() const
    {
        std::vector<WorkspaceLayoutIssue> issues;
        std::unordered_set<std::string> nodeIds;
        std::unordered_set<std::string> panelIds;
        ValidateNode(root, issues, nodeIds, panelIds);
        return issues;
    }
} // namespace Horo::Editor
