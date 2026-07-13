#include "Horo/Editor/WorkspacePanelHost.h"

#include <cassert>

namespace
{
using namespace Horo::Editor;

void DefaultLayoutContainsExpectedStacks()
{
    WorkspacePanelHost host;
    assert(host.Layout().FindTabStack("workspace.left") != nullptr);
    assert(host.Layout().FindTabStack("workspace.document") != nullptr);
    assert(host.Layout().FindTabStack("workspace.right") != nullptr);
    assert(host.Layout().FindTabStack("workspace.document")->activeTab == "horo.viewport");
}

void OpensAndMovesPanelTransactionally()
{
    WorkspacePanelHost host;
    assert(host.OpenPanel("horo.content_browser", "workspace.document").Succeeded());
    assert(host.Layout().FindTabStack("workspace.document")->activeTab == "horo.content_browser");

    assert(host.MovePanel("horo.content_browser", TabPlacement{"workspace.left", 1}).Succeeded());
    const auto *left = host.Layout().FindTabStack("workspace.left");
    assert(left != nullptr);
    assert(left->tabs.size() == 2);
    assert(left->tabs[1] == "horo.content_browser");
}

void RejectsDuplicatePanelWithoutMutation()
{
    WorkspacePanelHost host;
    assert(!host.OpenPanel("horo.viewport", "workspace.left").Succeeded());
    assert(host.Layout().FindTabStack("workspace.left")->tabs.size() == 1);
}

void ClosesPanelAndKeepsActiveTabValid()
{
    WorkspacePanelHost host;
    assert(host.OpenPanel("horo.content_browser", "workspace.document").Succeeded());
    assert(host.ClosePanel("horo.content_browser").Succeeded());
    const auto *document = host.Layout().FindTabStack("workspace.document");
    assert(document != nullptr);
    assert(document->activeTab == "horo.viewport");
    assert(!host.ClosePanel("missing").Succeeded());
}

void CreatesASplitWithoutLosingTheTargetNode()
{
    WorkspacePanelHost host;
    assert(host.DockPanel("horo.content_browser", "workspace.document",
                          WorkspacePanelHost::DropKind::SplitBottom).Succeeded());
    assert(host.Layout().FindNode("workspace.document") != nullptr);
    assert(host.Layout().FindNode("workspace.document.split.horo.content_browser") != nullptr);
    assert(host.Layout().Validate().empty());
}
} // namespace

int main()
{
    DefaultLayoutContainsExpectedStacks();
    OpensAndMovesPanelTransactionally();
    RejectsDuplicatePanelWithoutMutation();
    ClosesPanelAndKeepsActiveTabValid();
    CreatesASplitWithoutLosingTheTargetNode();
    return 0;
}
