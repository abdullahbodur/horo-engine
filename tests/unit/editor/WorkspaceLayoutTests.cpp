#include <catch2/catch_test_macros.hpp>

#include "Horo/Editor/WorkspaceLayout.h"

#include <memory>
#include <string>

namespace
{
using namespace Horo::Editor;

LayoutNode MakePanelNode(const std::string &nodeId, const std::string &panelId)
{
    return LayoutNode{PanelNode{nodeId, panelId}};
}

TEST_CASE("Finds Nodes And Panels By Stable Id", "[unit][editor]")
{
    WorkspaceLayout layout;
    layout.root = MakePanelNode("root.panel", "horo.viewport");

    REQUIRE((layout.FindNode("root.panel") != nullptr));
    REQUIRE((layout.FindNode("missing") == nullptr));
    REQUIRE((layout.FindPanel("horo.viewport") != nullptr));
    REQUIRE((layout.FindPanel("missing") == nullptr));
}

TEST_CASE("Validates A Well Formed Tab Stack", "[unit][editor]")
{
    WorkspaceLayout layout;
    layout.root = LayoutNode{TabStackNode{
        .id = "root.stack",
        .tabs = {"horo.hierarchy", "horo.inspector"},
        .activeTab = "horo.inspector",
    }};

    REQUIRE((layout.Validate().empty()));
}

TEST_CASE("Rejects An Active Tab That Is Not In Its Stack", "[unit][editor]")
{
    WorkspaceLayout layout;
    layout.root = LayoutNode{TabStackNode{
        .id = "root.stack",
        .tabs = {"horo.hierarchy"},
        .activeTab = "horo.inspector",
    }};

    const auto issues = layout.Validate();
    REQUIRE((!issues.empty()));
    REQUIRE((issues.front().code == WorkspaceLayoutIssueCode::ActiveTabMissing));
}

TEST_CASE("Layout Copies Own Its Tree", "[unit][editor]")
{
    WorkspaceLayout original;
    original.root = MakePanelNode("root.panel", "horo.viewport");
    WorkspaceLayout copy = original;

    auto *panel = copy.FindPanel("horo.viewport");
    REQUIRE((panel != nullptr));
    panel->panel = "horo.inspector";

    REQUIRE((original.FindPanel("horo.viewport") != nullptr));
    REQUIRE((original.FindPanel("horo.inspector") == nullptr));
}

TEST_CASE("Moves A Tab Between Stacks And Activates It", "[unit][editor]")
{
    WorkspaceLayout layout;
    layout.root = LayoutNode{SplitNode{
        .id = "root.split",
        .first = std::make_unique<LayoutNode>(
            LayoutNode{TabStackNode{.id = "left.stack", .tabs = {"horo.hierarchy"}, .activeTab = "horo.hierarchy"}}),
        .second = std::make_unique<LayoutNode>(
            LayoutNode{TabStackNode{.id = "right.stack", .tabs = {"horo.inspector"}, .activeTab = "horo.inspector"}}),
    }};

    const auto result = layout.MoveTab("horo.hierarchy", TabPlacement{"right.stack", std::nullopt});
    REQUIRE((result.Succeeded()));
    REQUIRE((layout.FindTabStack("left.stack")->tabs.empty()));
    REQUIRE((layout.FindTabStack("right.stack")->tabs.size() == 2));
    REQUIRE((layout.FindTabStack("right.stack")->activeTab == "horo.hierarchy"));
}

TEST_CASE("Rejects Moving An Unknown Tab Without Mutation", "[unit][editor]")
{
    WorkspaceLayout layout;
    layout.root = LayoutNode{TabStackNode{.id = "root.stack", .tabs = {"horo.viewport"}, .activeTab = "horo.viewport"}};

    const auto result = layout.MoveTab("missing", TabPlacement{"root.stack", std::nullopt});
    REQUIRE((!result.Succeeded()));
    REQUIRE((layout.FindTabStack("root.stack")->tabs.size() == 1));
}

TEST_CASE("Closes A Tab And Selects The Remaining Tab", "[unit][editor]")
{
    WorkspaceLayout layout;
    layout.root = LayoutNode{
        TabStackNode{.id = "root.stack", .tabs = {"horo.viewport", "horo.inspector"}, .activeTab = "horo.viewport"}};

    const auto result = layout.CloseTab("horo.viewport");
    REQUIRE((result.Succeeded()));
    REQUIRE((layout.FindTabStack("root.stack")->tabs == std::vector<TabId>{"horo.inspector"}));
    REQUIRE((layout.FindTabStack("root.stack")->activeTab == "horo.inspector"));
}
} // namespace
