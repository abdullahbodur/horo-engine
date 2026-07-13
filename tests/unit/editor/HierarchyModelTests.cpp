#include "Horo/Editor/HierarchyModel.h"

#include <cstdlib>
#include <iostream>
#include <optional>
#include <string_view>
#include <vector>

namespace
{
void Expect(const bool condition, const std::string_view message)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

void TestMockHierarchyMatchesEditorReference()
{
    using namespace Horo::Editor;
    HierarchyModel model = CreateMockHierarchyModel();

    Expect(model.Roots().size() == 3, "mock hierarchy should expose Room, Lighting, and Cameras roots");
    Expect(model.Roots()[0]->name == "Room", "first root should be Room");
    Expect(model.Roots()[0]->children.size() == 4, "Room should own four children");
    Expect(model.Roots()[0]->children[0]->name == "floor 000", "Room should contain floor 000");
    Expect(model.Roots()[0]->children[0]->type == HierarchyNodeType::Mesh, "floor 000 should be a mesh");
    Expect(model.Roots()[1]->children[0]->type == HierarchyNodeType::Light, "Lighting should contain a light");
    Expect(model.Roots()[2]->children[0]->type == HierarchyNodeType::Camera, "Cameras should contain a camera");
}

void TestVisibleRowsRespectExpansionAndSearchAncestors()
{
    using namespace Horo::Editor;
    HierarchyModel model = CreateMockHierarchyModel();
    std::vector<HierarchyVisibleRow> rows;

    model.BuildVisibleRows("", rows);
    Expect(rows.size() == 9, "expanded mock hierarchy should expose nine rows");
    Expect(rows[1].depth == 1, "Room children should be indented one level");

    const auto roomId = model.Roots()[0]->id;
    Expect(model.SetExpanded(roomId, false) == HierarchyMutationResult::Success, "Room should collapse");
    model.BuildVisibleRows("", rows);
    Expect(rows.size() == 5, "collapsed Room should hide its descendants");

    model.BuildVisibleRows("floor", rows);
    Expect(rows.size() == 2, "search should retain a matching node and its ancestor path");
    Expect(rows[0].node->name == "Room" && rows[1].node->name == "floor 000",
           "search should preserve hierarchy context");
}

void TestRenameValidatesAndUpdatesNode()
{
    using namespace Horo::Editor;
    HierarchyModel model = CreateMockHierarchyModel();
    const auto floorId = model.Roots()[0]->children[0]->id;

    Expect(model.Rename(floorId, "Player Floor") == HierarchyMutationResult::Success, "valid rename should succeed");
    Expect(model.Find(floorId)->name == "Player Floor", "rename should update the stable node");
    Expect(model.Rename(floorId, "   ") == HierarchyMutationResult::InvalidName,
           "blank rename should be rejected without mutation");
    Expect(model.Find(floorId)->name == "Player Floor", "failed rename should preserve the prior name");
}

void TestDeleteRemovesWholeSubtreeAndSelection()
{
    using namespace Horo::Editor;
    HierarchyModel model;
    const auto parent = model.AddNode(std::nullopt, "Parent", HierarchyNodeType::Empty);
    const auto child = model.AddNode(parent, "Child", HierarchyNodeType::Mesh);
    const auto grandchild = model.AddNode(child, "Grandchild", HierarchyNodeType::Light);
    Expect(model.Select(grandchild) == HierarchyMutationResult::Success, "grandchild should be selectable");

    Expect(model.Delete(parent) == HierarchyMutationResult::Success, "parent deletion should succeed");
    Expect(model.Find(parent) == nullptr && model.Find(child) == nullptr && model.Find(grandchild) == nullptr,
           "deleting a parent should delete its complete subtree");
    Expect(!model.SelectedId().has_value(), "deleting the selected subtree should clear selection");
}

void TestReparentPreservesSubtreeAndRejectsCycles()
{
    using namespace Horo::Editor;
    HierarchyModel model;
    const auto parentA = model.AddNode(std::nullopt, "Parent A", HierarchyNodeType::Empty);
    const auto child = model.AddNode(parentA, "Child", HierarchyNodeType::Mesh);
    const auto grandchild = model.AddNode(child, "Grandchild", HierarchyNodeType::Light);
    const auto parentB = model.AddNode(std::nullopt, "Parent B", HierarchyNodeType::Empty);

    Expect(model.Reparent(child, parentB) == HierarchyMutationResult::Success,
           "node should become a child of another node");
    Expect(model.ParentId(child) == parentB, "reparent should update the direct parent");
    Expect(model.ParentId(grandchild) == child, "reparent should preserve descendant hierarchy");
    Expect(model.Reparent(parentB, grandchild) == HierarchyMutationResult::Cycle,
           "a node must not move below its own descendant");
    Expect(model.ParentId(parentB) == std::nullopt, "rejected cycle must not mutate the tree");

    Expect(model.Reparent(child, std::nullopt) == HierarchyMutationResult::Success,
           "node should move back to root level");
    Expect(model.ParentId(child) == std::nullopt, "root move should clear the parent");
    Expect(model.ParentId(grandchild) == child, "root move should still preserve descendants");
}
} // namespace

int main()
{
    TestMockHierarchyMatchesEditorReference();
    TestVisibleRowsRespectExpansionAndSearchAncestors();
    TestRenameValidatesAndUpdatesNode();
    TestDeleteRemovesWholeSubtreeAndSelection();
    TestReparentPreservesSubtreeAndRejectsCycles();
    return 0;
}
