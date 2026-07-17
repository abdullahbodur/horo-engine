#include "Horo/Editor/HierarchyModel.h"

#include <algorithm>
#include <cctype>
#include <unordered_map>
#include <utility>

namespace Horo::Editor
{
namespace
{
[[nodiscard]] HierarchyNode *FindNode(std::vector<std::unique_ptr<HierarchyNode>> &nodes,
                                      const HierarchyNodeId id) noexcept
{
    for (const auto &node : nodes)
    {
        if (node->id == id)
        {
            return node.get();
        }
        if (HierarchyNode *found = FindNode(node->children, id))
        {
            return found;
        }
    }
    return nullptr;
}

[[nodiscard]] const HierarchyNode *FindNode(const std::vector<std::unique_ptr<HierarchyNode>> &nodes,
                                            const HierarchyNodeId id) noexcept
{
    for (const auto &node : nodes)
    {
        if (node->id == id)
        {
            return node.get();
        }
        if (const HierarchyNode *found = FindNode(node->children, id))
        {
            return found;
        }
    }
    return nullptr;
}

[[nodiscard]] bool ContainsNode(const HierarchyNode &root, const HierarchyNodeId id) noexcept
{
    if (root.id == id)
    {
        return true;
    }
    return std::ranges::any_of(root.children, [id](const auto &child) { return ContainsNode(*child, id); });
}

[[nodiscard]] std::unique_ptr<HierarchyNode> ExtractNode(std::vector<std::unique_ptr<HierarchyNode>> &nodes,
                                                         const HierarchyNodeId id) noexcept
{
    const auto direct = std::ranges::find_if(nodes, [id](const auto &node) { return node->id == id; });
    if (direct != nodes.end())
    {
        std::unique_ptr<HierarchyNode> extracted = std::move(*direct);
        nodes.erase(direct);
        return extracted;
    }
    for (const auto &node : nodes)
    {
        if (std::unique_ptr<HierarchyNode> extracted = ExtractNode(node->children, id))
        {
            return extracted;
        }
    }
    return nullptr;
}

[[nodiscard]] std::optional<HierarchyNodeId> FindParentId(const std::vector<std::unique_ptr<HierarchyNode>> &nodes,
                                                          const HierarchyNodeId id,
                                                          const std::optional<HierarchyNodeId> parent) noexcept
{
    for (const auto &node : nodes)
    {
        if (node->id == id)
        {
            return parent;
        }
        const std::optional<HierarchyNodeId> found = FindParentId(node->children, id, node->id);
        if (found.has_value())
        {
            return found;
        }
    }
    return std::nullopt;
}

[[nodiscard]] bool ContainsCaseInsensitive(const std::string_view text, const std::string_view query) noexcept
{
    if (query.empty())
    {
        return true;
    }
    if (query.size() > text.size())
    {
        return false;
    }
    for (std::size_t start = 0; start + query.size() <= text.size(); ++start)
    {
        bool matches = true;
        for (std::size_t offset = 0; offset < query.size(); ++offset)
        {
            const auto left = static_cast<unsigned char>(text[start + offset]);
            const auto right = static_cast<unsigned char>(query[offset]);
            if (std::tolower(left) != std::tolower(right))
            {
                matches = false;
                break;
            }
        }
        if (matches)
        {
            return true;
        }
    }
    return false;
}

void AppendExpandedRows(const HierarchyNode &node, const std::uint32_t depth, std::vector<HierarchyVisibleRow> &output)
{
    output.push_back(HierarchyVisibleRow{.node = &node, .depth = depth});
    if (!node.expanded)
    {
        return;
    }
    for (const auto &child : node.children)
    {
        AppendExpandedRows(*child, depth + 1, output);
    }
}

[[nodiscard]] bool AppendFilteredRows(const HierarchyNode &node, const std::uint32_t depth,
                                      const std::string_view query, std::vector<HierarchyVisibleRow> &output)
{
    const std::size_t rowStart = output.size();
    output.push_back(HierarchyVisibleRow{.node = &node, .depth = depth});
    bool descendantMatched = false;
    for (const auto &child : node.children)
    {
        descendantMatched = AppendFilteredRows(*child, depth + 1, query, output) || descendantMatched;
    }
    if (ContainsCaseInsensitive(node.name, query) || descendantMatched)
    {
        return true;
    }
    output.resize(rowStart);
    return false;
}

[[nodiscard]] std::string TrimName(const std::string_view name)
{
    const auto isWhitespace = [](const unsigned char character) { return std::isspace(character) != 0; };
    const auto first = std::ranges::find_if_not(name, isWhitespace);
    const auto last = std::find_if_not(name.rbegin(), name.rend(), isWhitespace).base();
    if (first >= last)
    {
        return {};
    }
    return std::string(first, last);
}
} // namespace

/** @copydoc HierarchyModel::Roots */
const std::vector<std::unique_ptr<HierarchyNode>> &HierarchyModel::Roots() const noexcept
{
    return roots_;
}

/** @copydoc HierarchyModel::Find */
HierarchyNode *HierarchyModel::Find(const HierarchyNodeId id) noexcept
{
    return FindNode(roots_, id);
}

/** @copydoc HierarchyModel::Find */
const HierarchyNode *HierarchyModel::Find(const HierarchyNodeId id) const noexcept
{
    return FindNode(roots_, id);
}

/** @copydoc HierarchyModel::AddNode */
HierarchyNodeId HierarchyModel::AddNode(const std::optional<HierarchyNodeId> parent, std::string name,
                                        const HierarchyNodeType type)
{
    name = TrimName(name);
    if (name.empty() || name.size() > 128)
    {
        return 0;
    }

    std::vector<std::unique_ptr<HierarchyNode>> *destination = &roots_;
    if (parent.has_value())
    {
        HierarchyNode *parentNode = Find(*parent);
        if (parentNode == nullptr)
        {
            return 0;
        }
        destination = &parentNode->children;
        parentNode->expanded = true;
    }

    const HierarchyNodeId id = nextId_++;
    destination->push_back(std::make_unique<HierarchyNode>(
        HierarchyNode{.id = id, .name = std::move(name), .type = type, .expanded = true, .children = {}}));
    return id;
}

/** @copydoc HierarchyModel::Select */
HierarchyMutationResult HierarchyModel::Select(const HierarchyNodeId id) noexcept
{
    if (Find(id) == nullptr)
    {
        return HierarchyMutationResult::NotFound;
    }
    selectedId_ = id;
    return HierarchyMutationResult::Success;
}

/** @copydoc HierarchyModel::SelectedId */
std::optional<HierarchyNodeId> HierarchyModel::SelectedId() const noexcept
{
    return selectedId_;
}

/** @copydoc HierarchyModel::Replace */
void HierarchyModel::Replace(const std::span<const HierarchyNodeInput> nodes)
{
    std::vector<HierarchyNodeId> collapsed;
    for (const HierarchyNodeInput &input : nodes)
    {
        if (const HierarchyNode *existing = Find(input.id); existing != nullptr && !existing->expanded)
        {
            collapsed.push_back(input.id);
        }
    }

    std::vector<std::unique_ptr<HierarchyNode>> owned;
    owned.reserve(nodes.size());
    std::unordered_map<HierarchyNodeId, HierarchyNode *> byId;
    byId.reserve(nodes.size());
    for (const HierarchyNodeInput &input : nodes)
    {
        if (input.id == 0 || byId.contains(input.id))
        {
            owned.push_back(nullptr);
            continue;
        }
        auto node = std::make_unique<HierarchyNode>(HierarchyNode{
            .id = input.id,
            .name = std::string(input.name),
            .type = input.type,
            .expanded = std::ranges::find(collapsed, input.id) == collapsed.end(),
            .children = {},
        });
        byId.emplace(input.id, node.get());
        owned.push_back(std::move(node));
    }

    roots_.clear();
    for (std::size_t index = 0; index < nodes.size() && index < owned.size(); ++index)
    {
        if (!owned[index])
        {
            continue;
        }
        const std::optional<HierarchyNodeId> parent = nodes[index].parent;
        const auto foundParent = parent.has_value() ? byId.find(*parent) : byId.end();
        if (foundParent == byId.end() || foundParent->second == owned[index].get())
        {
            roots_.push_back(std::move(owned[index]));
        }
        else
        {
            foundParent->second->children.push_back(std::move(owned[index]));
        }
    }
    if (selectedId_.has_value() && Find(*selectedId_) == nullptr)
    {
        selectedId_.reset();
    }
}

/** @copydoc HierarchyModel::ClearSelection */
void HierarchyModel::ClearSelection() noexcept
{
    selectedId_.reset();
}

/** @copydoc HierarchyModel::SetExpanded */
HierarchyMutationResult HierarchyModel::SetExpanded(const HierarchyNodeId id, const bool expanded) noexcept
{
    HierarchyNode *node = Find(id);
    if (node == nullptr)
    {
        return HierarchyMutationResult::NotFound;
    }
    node->expanded = expanded;
    return HierarchyMutationResult::Success;
}

/** @copydoc HierarchyModel::Rename */
HierarchyMutationResult HierarchyModel::Rename(const HierarchyNodeId id, const std::string_view name)
{
    HierarchyNode *node = Find(id);
    if (node == nullptr)
    {
        return HierarchyMutationResult::NotFound;
    }
    std::string validated = TrimName(name);
    if (validated.empty() || validated.size() > 128)
    {
        return HierarchyMutationResult::InvalidName;
    }
    node->name = std::move(validated);
    return HierarchyMutationResult::Success;
}

/** @copydoc HierarchyModel::Delete */
HierarchyMutationResult HierarchyModel::Delete(const HierarchyNodeId id) noexcept
{
    const HierarchyNode *node = Find(id);
    if (node == nullptr)
    {
        return HierarchyMutationResult::NotFound;
    }
    if (selectedId_.has_value() && ContainsNode(*node, *selectedId_))
    {
        selectedId_.reset();
    }
    static_cast<void>(ExtractNode(roots_, id));
    return HierarchyMutationResult::Success;
}

/** @copydoc HierarchyModel::Reparent */
HierarchyMutationResult HierarchyModel::Reparent(const HierarchyNodeId id,
                                                 const std::optional<HierarchyNodeId> newParent) noexcept
{
    HierarchyNode *source = Find(id);
    if (source == nullptr)
    {
        return HierarchyMutationResult::NotFound;
    }
    if (ParentId(id) == newParent)
    {
        return HierarchyMutationResult::Success;
    }

    HierarchyNode *destinationParent = nullptr;
    if (newParent.has_value())
    {
        destinationParent = Find(*newParent);
        if (destinationParent == nullptr)
        {
            return HierarchyMutationResult::NotFound;
        }
        if (ContainsNode(*source, *newParent))
        {
            return HierarchyMutationResult::Cycle;
        }
    }

    std::unique_ptr<HierarchyNode> subtree = ExtractNode(roots_, id);
    if (destinationParent != nullptr)
    {
        destinationParent->expanded = true;
        destinationParent->children.push_back(std::move(subtree));
    }
    else
    {
        roots_.push_back(std::move(subtree));
    }
    return HierarchyMutationResult::Success;
}

/** @copydoc HierarchyModel::ParentId */
std::optional<HierarchyNodeId> HierarchyModel::ParentId(const HierarchyNodeId id) const noexcept
{
    return FindParentId(roots_, id, std::nullopt);
}

/** @copydoc HierarchyModel::BuildVisibleRows */
void HierarchyModel::BuildVisibleRows(const std::string_view query, std::vector<HierarchyVisibleRow> &output) const
{
    output.clear();
    for (const auto &root : roots_)
    {
        if (query.empty())
        {
            AppendExpandedRows(*root, 0, output);
        }
        else
        {
            static_cast<void>(AppendFilteredRows(*root, 0, query, output));
        }
    }
}

/** @copydoc CreateMockHierarchyModel */
HierarchyModel CreateMockHierarchyModel()
{
    HierarchyModel model;
    const HierarchyNodeId room = model.AddNode(std::nullopt, "Room", HierarchyNodeType::Collection);
    const HierarchyNodeId floor = model.AddNode(room, "floor 000", HierarchyNodeType::Mesh);
    static_cast<void>(model.AddNode(room, "wall north", HierarchyNodeType::Mesh));
    static_cast<void>(model.AddNode(room, "wall south", HierarchyNodeType::Mesh));
    static_cast<void>(model.AddNode(room, "player spawn", HierarchyNodeType::Empty));

    const HierarchyNodeId lighting = model.AddNode(std::nullopt, "Lighting", HierarchyNodeType::Collection);
    static_cast<void>(model.AddNode(lighting, "sun directional", HierarchyNodeType::Light));

    const HierarchyNodeId cameras = model.AddNode(std::nullopt, "Cameras", HierarchyNodeType::Collection);
    static_cast<void>(model.AddNode(cameras, "main camera", HierarchyNodeType::Camera));
    static_cast<void>(model.Select(floor));
    return model;
}
} // namespace Horo::Editor
