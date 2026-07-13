#include "Horo/Editor/WorkspaceLayout.h"

#include <cassert>
#include <memory>
#include <string>
#include <variant>

namespace
{
using namespace Horo::Editor;

LayoutNode MakePanelNode(const std::string &nodeId, const std::string &panelId)
{
    return LayoutNode{PanelNode{nodeId, panelId}};
}

void FindsNodesAndPanelsByStableId()
{
    WorkspaceLayout layout;
    layout.root = MakePanelNode("root.panel", "horo.viewport");

    assert(layout.FindNode("root.panel") != nullptr);
    assert(layout.FindNode("missing") == nullptr);
    assert(layout.FindPanel("horo.viewport") != nullptr);
    assert(layout.FindPanel("missing") == nullptr);
}

void ValidatesAWellFormedTabStack()
{
    WorkspaceLayout layout;
    layout.root = LayoutNode{TabStackNode{
        .id = "root.stack",
        .tabs = {"horo.hierarchy", "horo.inspector"},
        .activeTab = "horo.inspector",
    }};

    assert(layout.Validate().empty());
}

void RejectsAnActiveTabThatIsNotInItsStack()
{
    WorkspaceLayout layout;
    layout.root = LayoutNode{TabStackNode{
        .id = "root.stack",
        .tabs = {"horo.hierarchy"},
        .activeTab = "horo.inspector",
    }};

    const auto issues = layout.Validate();
    assert(!issues.empty());
    assert(issues.front().code == WorkspaceLayoutIssueCode::ActiveTabMissing);
}

void LayoutCopiesOwnItsTree()
{
    WorkspaceLayout original;
    original.root = MakePanelNode("root.panel", "horo.viewport");
    WorkspaceLayout copy = original;

    auto *panel = copy.FindPanel("horo.viewport");
    assert(panel != nullptr);
    panel->panel = "horo.inspector";

    assert(original.FindPanel("horo.viewport") != nullptr);
    assert(original.FindPanel("horo.inspector") == nullptr);
}

void MovesATabBetweenStacksAndActivatesIt()
{
    WorkspaceLayout layout;
    layout.root = LayoutNode{SplitNode{
        .id = "root.split",
        .first = std::make_unique<LayoutNode>(LayoutNode{TabStackNode{
            .id = "left.stack", .tabs = {"horo.hierarchy"}, .activeTab = "horo.hierarchy"}}),
        .second = std::make_unique<LayoutNode>(LayoutNode{TabStackNode{
            .id = "right.stack", .tabs = {"horo.inspector"}, .activeTab = "horo.inspector"}}),
    }};

    const auto result = layout.MoveTab("horo.hierarchy", TabPlacement{"right.stack", std::nullopt});
    assert(result.Succeeded());
    assert(layout.FindTabStack("left.stack")->tabs.empty());
    assert(layout.FindTabStack("right.stack")->tabs.size() == 2);
    assert(layout.FindTabStack("right.stack")->activeTab == "horo.hierarchy");
}

void RejectsMovingAnUnknownTabWithoutMutation()
{
    WorkspaceLayout layout;
    layout.root = LayoutNode{TabStackNode{.id = "root.stack", .tabs = {"horo.viewport"}, .activeTab = "horo.viewport"}};

    const auto result = layout.MoveTab("missing", TabPlacement{"root.stack", std::nullopt});
    assert(!result.Succeeded());
    assert(layout.FindTabStack("root.stack")->tabs.size() == 1);
}

void ClosesATabAndSelectsTheRemainingTab()
{
    WorkspaceLayout layout;
    layout.root = LayoutNode{TabStackNode{
        .id = "root.stack", .tabs = {"horo.viewport", "horo.inspector"}, .activeTab = "horo.viewport"}};

    const auto result = layout.CloseTab("horo.viewport");
    assert(result.Succeeded());
    assert(layout.FindTabStack("root.stack")->tabs == std::vector<TabId>{"horo.inspector"});
    assert(layout.FindTabStack("root.stack")->activeTab == "horo.inspector");
}
} // namespace

int main()
{
    FindsNodesAndPanelsByStableId();
    ValidatesAWellFormedTabStack();
    RejectsAnActiveTabThatIsNotInItsStack();
    LayoutCopiesOwnItsTree();
    MovesATabBetweenStacksAndActivatesIt();
    RejectsMovingAnUnknownTabWithoutMutation();
    ClosesATabAndSelectsTheRemainingTab();
    return 0;
}
