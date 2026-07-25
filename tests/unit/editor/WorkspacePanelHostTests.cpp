#include <catch2/catch_test_macros.hpp>

#include "Horo/Editor/WorkspacePanelHost.h"

namespace
{
    using namespace Horo::Editor;

    TEST_CASE("Default Layout Contains Expected Stacks", "[unit][editor]")
    {
        WorkspacePanelHost host;
        REQUIRE((host.Layout().FindTabStack("workspace.left") != nullptr));
        REQUIRE((host.Layout().FindTabStack("workspace.document") != nullptr));
        REQUIRE((host.Layout().FindTabStack("workspace.right") != nullptr));
        REQUIRE((host.Layout().FindTabStack("workspace.document")->activeTab == "horo.viewport"));
    }

    TEST_CASE("Opens And Moves Panel Transactionally", "[unit][editor]")
    {
        WorkspacePanelHost host;
        REQUIRE((host.OpenPanel("horo.content_browser", "workspace.document").Succeeded()));
        REQUIRE((host.Layout().FindTabStack("workspace.document")->activeTab == "horo.content_browser"));

        REQUIRE((host.MovePanel("horo.content_browser", TabPlacement{"workspace.left", 1}).Succeeded()));
        const auto* left = host.Layout().FindTabStack("workspace.left");
        REQUIRE((left != nullptr));
        REQUIRE((left->tabs.size() == 2));
        REQUIRE((left->tabs[1] == "horo.content_browser"));
    }

    TEST_CASE("Rejects Duplicate Panel Without Mutation", "[unit][editor]")
    {
        WorkspacePanelHost host;
        REQUIRE((!host.OpenPanel("horo.viewport", "workspace.left").Succeeded()));
        REQUIRE((host.Layout().FindTabStack("workspace.left")->tabs.size() == 1));
    }

    TEST_CASE("Closes Panel And Keeps Active Tab Valid", "[unit][editor]")
    {
        WorkspacePanelHost host;
        REQUIRE((host.OpenPanel("horo.content_browser", "workspace.document").Succeeded()));
        REQUIRE((host.ClosePanel("horo.content_browser").Succeeded()));
        const auto* document = host.Layout().FindTabStack("workspace.document");
        REQUIRE((document != nullptr));
        REQUIRE((document->activeTab == "horo.viewport"));
        REQUIRE((!host.ClosePanel("missing").Succeeded()));
    }

    TEST_CASE("Creates A Split Without Losing The Target Node", "[unit][editor]")
    {
        WorkspacePanelHost host;
        REQUIRE((host.DockPanel("horo.content_browser", "workspace.document", WorkspacePanelHost::DropKind::SplitBottom)
            .Succeeded()));
        REQUIRE((host.Layout().FindNode("workspace.document") != nullptr));
        REQUIRE((host.Layout().FindNode("workspace.document.split.horo.content_browser") != nullptr));
        REQUIRE((host.Layout().Validate().empty()));
    }
} // namespace
