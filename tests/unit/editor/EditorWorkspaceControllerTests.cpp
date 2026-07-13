#include "Horo/Editor/EditorWorkspaceController.h"

#include <cassert>

namespace
{
using namespace Horo::Editor;

void MovingAnActivePanelAcrossAreasUpdatesItsRuntimePlacement()
{
    EditorWorkspaceController controller("test-project");

    EditorWorkspaceViewCommandData command;
    command.command = EditorWorkspaceViewCommand::ChangeActivePanel;
    command.targetIndex = 2;
    command.stringPayload = "horo.hierarchy";
    command.activityBarSlot = ActivityBarSlot{ActivityBarRail::Left, 2, 1};
    controller.ProcessCommand(command);

    const auto &viewModel = controller.ViewModel();
    assert(viewModel.activeLeftPanelId.empty());
    assert(viewModel.activeBottomPanelId == "horo.hierarchy");
    assert(viewModel.panelDockAreas.at("horo.hierarchy") == WorkspaceDockArea::Bottom);
    const auto slot = viewModel.activityBarLayout.FindSlot("horo.hierarchy");
    assert(slot.has_value());
    assert((*slot == ActivityBarSlot{ActivityBarRail::Left, 2, 1}));
    assert(viewModel.activityBarLayout.ItemAt(ActivityBarRail::Left, 2, 0) == "horo.content_browser");
}

void ReplacingATargetAreaPreservesTheDisplacedPanelsPlacement()
{
    EditorWorkspaceController controller("test-project");

    EditorWorkspaceViewCommandData command;
    command.command = EditorWorkspaceViewCommand::ChangeActivePanel;
    command.targetIndex = 2;
    command.stringPayload = "horo.hierarchy";
    controller.ProcessCommand(command);

    const auto &viewModel = controller.ViewModel();
    assert(viewModel.activeBottomPanelId == "horo.hierarchy");
    assert(viewModel.panelDockAreas.at("horo.content_browser") == WorkspaceDockArea::Bottom);
}

void DroppingIntoBottomRightSplitsTheFullBottomDock()
{
    EditorWorkspaceController controller("test-project");

    EditorWorkspaceViewCommandData command;
    command.command = EditorWorkspaceViewCommand::ChangeActivePanel;
    command.targetIndex = 2;
    command.stringPayload = "horo.inspector";
    command.bottomDockSlot = BottomDockSlot::Right;
    command.activityBarSlot = ActivityBarSlot{ActivityBarRail::Right, 2, 0};
    controller.ProcessCommand(command);

    const auto &viewModel = controller.ViewModel();
    assert(viewModel.bottomDockMode == BottomDockMode::Split);
    assert(viewModel.activeBottomPanelId.empty());
    assert(viewModel.activeBottomLeftPanelId == "horo.content_browser");
    assert(viewModel.activeBottomRightPanelId == "horo.inspector");
    assert((viewModel.activityBarLayout.FindSlot("horo.inspector") ==
            ActivityBarSlot{ActivityBarRail::Right, 2, 0}));
}

void ClickingASplitPanelExpandsItToTheFullBottomDock()
{
    EditorWorkspaceController controller("test-project");

    EditorWorkspaceViewCommandData splitCommand;
    splitCommand.command = EditorWorkspaceViewCommand::ChangeActivePanel;
    splitCommand.targetIndex = 2;
    splitCommand.stringPayload = "horo.inspector";
    splitCommand.bottomDockSlot = BottomDockSlot::Right;
    splitCommand.activityBarSlot = ActivityBarSlot{ActivityBarRail::Right, 2, 0};
    controller.ProcessCommand(splitCommand);

    EditorWorkspaceViewCommandData clickCommand;
    clickCommand.command = EditorWorkspaceViewCommand::ChangeActivePanel;
    clickCommand.targetIndex = 2;
    clickCommand.stringPayload = "horo.inspector";
    controller.ProcessCommand(clickCommand);

    const auto &viewModel = controller.ViewModel();
    assert(viewModel.bottomDockMode == BottomDockMode::Full);
    assert(viewModel.activeBottomPanelId == "horo.inspector");
    assert(viewModel.activeBottomLeftPanelId.empty());
    assert(viewModel.activeBottomRightPanelId.empty());
}

void DroppingALeftRailIconIntoBottomRightMovesItToTheRightRail()
{
    EditorWorkspaceController controller("test-project");

    EditorWorkspaceViewCommandData fullCommand;
    fullCommand.command = EditorWorkspaceViewCommand::ChangeActivePanel;
    fullCommand.targetIndex = 2;
    fullCommand.stringPayload = "horo.inspector";
    controller.ProcessCommand(fullCommand);

    EditorWorkspaceViewCommandData splitCommand;
    splitCommand.command = EditorWorkspaceViewCommand::ChangeActivePanel;
    splitCommand.targetIndex = 2;
    splitCommand.stringPayload = "horo.hierarchy";
    splitCommand.bottomDockSlot = BottomDockSlot::Right;
    splitCommand.activityBarSlot = ActivityBarSlot{ActivityBarRail::Right, 2, 0};
    controller.ProcessCommand(splitCommand);

    const auto &viewModel = controller.ViewModel();
    assert(viewModel.bottomDockMode == BottomDockMode::Split);
    assert(viewModel.activeBottomLeftPanelId == "horo.inspector");
    assert(viewModel.activeBottomRightPanelId == "horo.hierarchy");
    assert((viewModel.activityBarLayout.FindSlot("horo.hierarchy") ==
            ActivityBarSlot{ActivityBarRail::Right, 2, 0}));
}
} // namespace

int main()
{
    MovingAnActivePanelAcrossAreasUpdatesItsRuntimePlacement();
    ReplacingATargetAreaPreservesTheDisplacedPanelsPlacement();
    DroppingIntoBottomRightSplitsTheFullBottomDock();
    ClickingASplitPanelExpandsItToTheFullBottomDock();
    DroppingALeftRailIconIntoBottomRightMovesItToTheRightRail();
    return 0;
}
