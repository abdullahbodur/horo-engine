#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Horo::Editor
{
/** @file HierarchyModel.h
 *  @brief Typed, scene-independent hierarchy tree model used by the editor mock hierarchy panel.
 */

using HierarchyNodeId = std::uint64_t;

/** @brief Presentation category associated with one hierarchy node. */
enum class HierarchyNodeType : std::uint8_t
{
    Collection,
    Mesh,
    Empty,
    Light,
    Camera,
};

/** @brief Result of a hierarchy mutation that can be rejected without changing the tree. */
enum class HierarchyMutationResult : std::uint8_t
{
    Success,
    NotFound,
    InvalidName,
    Cycle,
};

/** @brief One owned hierarchy node and its recursively owned child subtree. */
struct HierarchyNode
{
    HierarchyNodeId id{0};
    std::string name;
    HierarchyNodeType type{HierarchyNodeType::Empty};
    bool expanded{true};
    std::vector<std::unique_ptr<HierarchyNode>> children;
};

/** @brief Non-owning flattened row produced for hierarchy presentation. */
struct HierarchyVisibleRow
{
    const HierarchyNode *node{nullptr};
    std::uint32_t depth{0};
};

/**
 * @brief Owns a recursive hierarchy and provides validated editor mutations.
 *
 * Node IDs remain stable until their node is deleted. Reparenting transfers the
 * complete owned subtree and rejects moves below the node itself or a descendant.
 */
class HierarchyModel
{
  public:
    HierarchyModel() = default;
    HierarchyModel(const HierarchyModel &) = delete;
    HierarchyModel &operator=(const HierarchyModel &) = delete;
    HierarchyModel(HierarchyModel &&) noexcept = default;
    HierarchyModel &operator=(HierarchyModel &&) noexcept = default;

    /** @brief Returns root-level nodes in visual order. */
    [[nodiscard]] const std::vector<std::unique_ptr<HierarchyNode>> &Roots() const noexcept;

    /** @brief Finds a mutable node by stable ID, or null when absent. */
    [[nodiscard]] HierarchyNode *Find(HierarchyNodeId id) noexcept;

    /** @brief Finds a read-only node by stable ID, or null when absent. */
    [[nodiscard]] const HierarchyNode *Find(HierarchyNodeId id) const noexcept;

    /**
     * @brief Adds a node at root or below an existing parent.
     * @param parent Parent ID, or null for a root node.
     * @param name Non-empty display name.
     * @param type Presentation category.
     * @return Stable node ID, or zero when the parent/name is invalid.
     */
    [[nodiscard]] HierarchyNodeId AddNode(std::optional<HierarchyNodeId> parent, std::string name,
                                          HierarchyNodeType type);

    /** @brief Selects an existing node by ID. */
    [[nodiscard]] HierarchyMutationResult Select(HierarchyNodeId id) noexcept;

    /** @brief Returns the selected stable ID, if any. */
    [[nodiscard]] std::optional<HierarchyNodeId> SelectedId() const noexcept;

    /** @brief Updates one node's expanded state. */
    [[nodiscard]] HierarchyMutationResult SetExpanded(HierarchyNodeId id, bool expanded) noexcept;

    /** @brief Renames a node after trimming and validating the supplied name. */
    [[nodiscard]] HierarchyMutationResult Rename(HierarchyNodeId id, std::string_view name);

    /** @brief Deletes a node and its complete child subtree. */
    [[nodiscard]] HierarchyMutationResult Delete(HierarchyNodeId id) noexcept;

    /**
     * @brief Moves a complete subtree to root or below another node.
     * @param id Root of the subtree to move.
     * @param newParent Destination parent, or null for root level.
     * @return Success, NotFound, or Cycle. Rejected moves do not mutate the tree.
     */
    [[nodiscard]] HierarchyMutationResult Reparent(HierarchyNodeId id,
                                                   std::optional<HierarchyNodeId> newParent) noexcept;

    /** @brief Returns the direct parent ID, or null for root/unknown nodes. */
    [[nodiscard]] std::optional<HierarchyNodeId> ParentId(HierarchyNodeId id) const noexcept;

    /**
     * @brief Rebuilds flattened visible rows into caller-owned scratch storage.
     *
     * Empty queries respect expansion. Search is ASCII case-insensitive, ignores
     * expansion, and retains ancestor paths needed to understand each match.
     */
    void BuildVisibleRows(std::string_view query, std::vector<HierarchyVisibleRow> &output) const;

  private:
    std::vector<std::unique_ptr<HierarchyNode>> roots_;
    std::optional<HierarchyNodeId> selectedId_;
    HierarchyNodeId nextId_{1};
};

/** @brief Creates the temporary Room/Lighting/Cameras tree shown by the editor until scene integration lands. */
[[nodiscard]] HierarchyModel CreateMockHierarchyModel();
} // namespace Horo::Editor
