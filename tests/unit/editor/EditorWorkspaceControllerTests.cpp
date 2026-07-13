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

void PlacesTheViewportInTheDocumentTopRailByDefault()
{
    EditorWorkspaceController controller("test-project");

    assert((controller.ViewModel().activityBarLayout.FindSlot("horo.viewport") ==
            ActivityBarSlot{ActivityBarRail::DocumentTop, 0, 0}));
}

void DroppingIntoTheLowerHalfSplitsTheLeftDock()
{
    EditorWorkspaceController controller("test-project");

    EditorWorkspaceViewCommandData command;
    command.command = EditorWorkspaceViewCommand::ChangeActivePanel;
    command.targetIndex = 0;
    command.stringPayload = "horo.inspector";
    command.sideDockSlot = SideDockSlot::Bottom;
    command.activityBarSlot = ActivityBarSlot{ActivityBarRail::Left, 1, 0};
    controller.ProcessCommand(command);

    const auto &viewModel = controller.ViewModel();
    assert(viewModel.leftDockMode == SideDockMode::Split);
    assert(viewModel.activeLeftPanelId.empty());
    assert(viewModel.activeLeftTopPanelId == "horo.hierarchy");
    assert(viewModel.activeLeftBottomPanelId == "horo.inspector");
    assert(viewModel.activeRightPanelId.empty());
    assert((viewModel.activityBarLayout.FindSlot("horo.inspector") ==
            ActivityBarSlot{ActivityBarRail::Left, 1, 0}));
}

void DroppingIntoTheLowerHalfSplitsTheRightDock()
{
    EditorWorkspaceController controller("test-project");

    EditorWorkspaceViewCommandData command;
    command.command = EditorWorkspaceViewCommand::ChangeActivePanel;
    command.targetIndex = 1;
    command.stringPayload = "horo.hierarchy";
    command.sideDockSlot = SideDockSlot::Bottom;
    command.activityBarSlot = ActivityBarSlot{ActivityBarRail::Right, 1, 0};
    controller.ProcessCommand(command);

    const auto &viewModel = controller.ViewModel();
    assert(viewModel.rightDockMode == SideDockMode::Split);
    assert(viewModel.activeRightPanelId.empty());
    assert(viewModel.activeRightTopPanelId == "horo.inspector");
    assert(viewModel.activeRightBottomPanelId == "horo.hierarchy");
    assert(viewModel.activeLeftPanelId.empty());
    assert((viewModel.activityBarLayout.FindSlot("horo.hierarchy") ==
            ActivityBarSlot{ActivityBarRail::Right, 1, 0}));
}

void ClickingASplitSidePanelExpandsItToTheFullDock()
{
    EditorWorkspaceController controller("test-project");

    EditorWorkspaceViewCommandData splitCommand;
    splitCommand.command = EditorWorkspaceViewCommand::ChangeActivePanel;
    splitCommand.targetIndex = 0;
    splitCommand.stringPayload = "horo.inspector";
    splitCommand.sideDockSlot = SideDockSlot::Bottom;
    splitCommand.activityBarSlot = ActivityBarSlot{ActivityBarRail::Left, 1, 0};
    controller.ProcessCommand(splitCommand);

    EditorWorkspaceViewCommandData clickCommand;
    clickCommand.command = EditorWorkspaceViewCommand::ChangeActivePanel;
    clickCommand.targetIndex = 0;
    clickCommand.stringPayload = "horo.hierarchy";
    controller.ProcessCommand(clickCommand);

    const auto &viewModel = controller.ViewModel();
    assert(viewModel.leftDockMode == SideDockMode::Full);
    assert(viewModel.activeLeftPanelId == "horo.hierarchy");
    assert(viewModel.activeLeftTopPanelId.empty());
    assert(viewModel.activeLeftBottomPanelId.empty());
}

void MovingOneHalfAwayExpandsTheRemainingSidePanel()
{
    EditorWorkspaceController controller("test-project");

    EditorWorkspaceViewCommandData splitCommand;
    splitCommand.command = EditorWorkspaceViewCommand::ChangeActivePanel;
    splitCommand.targetIndex = 0;
    splitCommand.stringPayload = "horo.inspector";
    splitCommand.sideDockSlot = SideDockSlot::Bottom;
    splitCommand.activityBarSlot = ActivityBarSlot{ActivityBarRail::Left, 1, 0};
    controller.ProcessCommand(splitCommand);

    EditorWorkspaceViewCommandData moveCommand;
    moveCommand.command = EditorWorkspaceViewCommand::ChangeActivePanel;
    moveCommand.targetIndex = 1;
    moveCommand.stringPayload = "horo.inspector";
    moveCommand.activityBarSlot = ActivityBarSlot{ActivityBarRail::Right, 0, 0};
    controller.ProcessCommand(moveCommand);

    const auto &viewModel = controller.ViewModel();
    assert(viewModel.leftDockMode == SideDockMode::Full);
    assert(viewModel.activeLeftPanelId == "horo.hierarchy");
    assert(viewModel.activeLeftTopPanelId.empty());
    assert(viewModel.activeLeftBottomPanelId.empty());
}

void ReorderingAnActiveIconMovesItsPanelAndActivatesASourceFallback()
{
    EditorWorkspaceController controller("test-project");

    EditorWorkspaceViewCommandData closeFallback;
    closeFallback.command = EditorWorkspaceViewCommand::ChangeActivePanel;
    closeFallback.targetIndex = 2;
    closeFallback.stringPayload = std::string{};
    controller.ProcessCommand(closeFallback);

    EditorWorkspaceViewCommandData placeFallback;
    placeFallback.command = EditorWorkspaceViewCommand::ReorderActivityBarItem;
    placeFallback.stringPayload = "horo.content_browser";
    placeFallback.activityBarSlot = ActivityBarSlot{ActivityBarRail::Left, 0, 1};
    controller.ProcessCommand(placeFallback);

    EditorWorkspaceViewCommandData openBottom;
    openBottom.command = EditorWorkspaceViewCommand::ChangeActivePanel;
    openBottom.targetIndex = 0;
    openBottom.stringPayload = "horo.inspector";
    openBottom.sideDockSlot = SideDockSlot::Bottom;
    openBottom.activityBarSlot = ActivityBarSlot{ActivityBarRail::Left, 1, 0};
    controller.ProcessCommand(openBottom);

    EditorWorkspaceViewCommandData moveActiveIcon;
    moveActiveIcon.command = EditorWorkspaceViewCommand::ReorderActivityBarItem;
    moveActiveIcon.stringPayload = "horo.hierarchy";
    moveActiveIcon.activityBarSlot = ActivityBarSlot{ActivityBarRail::Left, 1, 1};
    controller.ProcessCommand(moveActiveIcon);

    const auto &viewModel = controller.ViewModel();
    assert(viewModel.leftDockMode == SideDockMode::Split);
    assert(viewModel.activeLeftTopPanelId == "horo.content_browser");
    assert(viewModel.activeLeftBottomPanelId == "horo.hierarchy");
    assert(viewModel.activeLeftBottomPanelId != "horo.inspector");
    assert(viewModel.panelDockAreas.at("horo.hierarchy") == WorkspaceDockArea::Left);
    assert(viewModel.panelDockAreas.at("horo.content_browser") == WorkspaceDockArea::Left);
    assert((viewModel.activityBarLayout.FindSlot("horo.hierarchy") ==
            ActivityBarSlot{ActivityBarRail::Left, 1, 1}));
}

void ReorderingTheOnlyBottomIconDoesNotLeaveAHalfEmptySplit()
{
    EditorWorkspaceController controller("test-project");

    EditorWorkspaceViewCommandData command;
    command.command = EditorWorkspaceViewCommand::ReorderActivityBarItem;
    command.stringPayload = "horo.content_browser";
    command.activityBarSlot = ActivityBarSlot{ActivityBarRail::Right, 2, 0};
    controller.ProcessCommand(command);

    const auto &viewModel = controller.ViewModel();
    assert(viewModel.bottomDockMode == BottomDockMode::Full);
    assert(viewModel.activeBottomPanelId == "horo.content_browser");
    assert(viewModel.activeBottomLeftPanelId.empty());
    assert(viewModel.activeBottomRightPanelId.empty());
}

void DroppingOnASideMergeTargetExpandsThePanelWithoutMovingItsIcon()
{
    EditorWorkspaceController controller("test-project");

    EditorWorkspaceViewCommandData splitCommand;
    splitCommand.command = EditorWorkspaceViewCommand::ChangeActivePanel;
    splitCommand.targetIndex = 0;
    splitCommand.stringPayload = "horo.inspector";
    splitCommand.sideDockSlot = SideDockSlot::Bottom;
    splitCommand.activityBarSlot = ActivityBarSlot{ActivityBarRail::Left, 1, 0};
    controller.ProcessCommand(splitCommand);

    EditorWorkspaceViewCommandData mergeCommand;
    mergeCommand.command = EditorWorkspaceViewCommand::ChangeActivePanel;
    mergeCommand.targetIndex = 0;
    mergeCommand.stringPayload = "horo.hierarchy";
    controller.ProcessCommand(mergeCommand);

    const auto &viewModel = controller.ViewModel();
    assert(viewModel.leftDockMode == SideDockMode::Full);
    assert(viewModel.activeLeftPanelId == "horo.hierarchy");
    assert(viewModel.activeLeftTopPanelId.empty());
    assert(viewModel.activeLeftBottomPanelId.empty());
    assert((viewModel.activityBarLayout.FindSlot("horo.hierarchy") ==
            ActivityBarSlot{ActivityBarRail::Left, 0, 0}));
    assert((viewModel.activityBarLayout.FindSlot("horo.inspector") ==
            ActivityBarSlot{ActivityBarRail::Left, 1, 0}));

    EditorWorkspaceViewCommandData replaceFullCommand;
    replaceFullCommand.command = EditorWorkspaceViewCommand::ChangeActivePanel;
    replaceFullCommand.targetIndex = 0;
    replaceFullCommand.stringPayload = "horo.inspector";
    controller.ProcessCommand(replaceFullCommand);

    assert(viewModel.leftDockMode == SideDockMode::Full);
    assert(viewModel.activeLeftPanelId == "horo.inspector");
    assert((viewModel.activityBarLayout.FindSlot("horo.inspector") ==
            ActivityBarSlot{ActivityBarRail::Left, 1, 0}));
}

void DroppingOnTheBottomMergeTargetPreservesItsActivityGroup()
{
    EditorWorkspaceController controller("test-project");

    EditorWorkspaceViewCommandData splitCommand;
    splitCommand.command = EditorWorkspaceViewCommand::ChangeActivePanel;
    splitCommand.targetIndex = 2;
    splitCommand.stringPayload = "horo.inspector";
    splitCommand.bottomDockSlot = BottomDockSlot::Right;
    splitCommand.activityBarSlot = ActivityBarSlot{ActivityBarRail::Right, 2, 0};
    controller.ProcessCommand(splitCommand);

    EditorWorkspaceViewCommandData mergeCommand;
    mergeCommand.command = EditorWorkspaceViewCommand::ChangeActivePanel;
    mergeCommand.targetIndex = 2;
    mergeCommand.stringPayload = "horo.content_browser";
    controller.ProcessCommand(mergeCommand);

    const auto &viewModel = controller.ViewModel();
    assert(viewModel.bottomDockMode == BottomDockMode::Full);
    assert(viewModel.activeBottomPanelId == "horo.content_browser");
    assert(viewModel.activeBottomLeftPanelId.empty());
    assert(viewModel.activeBottomRightPanelId.empty());
    assert((viewModel.activityBarLayout.FindSlot("horo.content_browser") ==
            ActivityBarSlot{ActivityBarRail::Left, 2, 0}));
    assert((viewModel.activityBarLayout.FindSlot("horo.inspector") ==
            ActivityBarSlot{ActivityBarRail::Right, 2, 0}));
}

void ReorderingAnActiveBottomPanelToTheLeftDoesNotRenderItTwice()
{
    EditorWorkspaceController controller("test-project");

    EditorWorkspaceViewCommandData command;
    command.command = EditorWorkspaceViewCommand::ReorderActivityBarItem;
    command.stringPayload = "horo.content_browser";
    command.activityBarSlot = ActivityBarSlot{ActivityBarRail::Left, 1, 0};
    controller.ProcessCommand(command);

    const auto &viewModel = controller.ViewModel();
    assert(viewModel.leftDockMode == SideDockMode::Split);
    assert(viewModel.activeLeftTopPanelId == "horo.hierarchy");
    assert(viewModel.activeLeftBottomPanelId == "horo.content_browser");
    assert(viewModel.activeBottomPanelId.empty());
    assert(viewModel.activeBottomLeftPanelId.empty());
    assert(viewModel.activeBottomRightPanelId.empty());
}
} // namespace

int main()
{
    MovingAnActivePanelAcrossAreasUpdatesItsRuntimePlacement();
    ReplacingATargetAreaPreservesTheDisplacedPanelsPlacement();
    DroppingIntoBottomRightSplitsTheFullBottomDock();
    ClickingASplitPanelExpandsItToTheFullBottomDock();
    DroppingALeftRailIconIntoBottomRightMovesItToTheRightRail();
    PlacesTheViewportInTheDocumentTopRailByDefault();
    DroppingIntoTheLowerHalfSplitsTheLeftDock();
    DroppingIntoTheLowerHalfSplitsTheRightDock();
    ClickingASplitSidePanelExpandsItToTheFullDock();
    MovingOneHalfAwayExpandsTheRemainingSidePanel();
    ReorderingAnActiveIconMovesItsPanelAndActivatesASourceFallback();
    ReorderingTheOnlyBottomIconDoesNotLeaveAHalfEmptySplit();
    DroppingOnASideMergeTargetExpandsThePanelWithoutMovingItsIcon();
    DroppingOnTheBottomMergeTargetPreservesItsActivityGroup();
    ReorderingAnActiveBottomPanelToTheLeftDoesNotRenderItTwice();
    return 0;
}
