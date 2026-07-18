#include "editor/screens/workspace/EditorWorkspaceController.h"

#include <cassert>

namespace
{
using namespace Horo;
using namespace Horo::Editor;
namespace Math = Horo::Math;

class TestWorkspaceController final
{
  public:
    TestWorkspaceController() : controller_("test-project", runtimeScene_)
    {
        assert(runtimeScene_.Startup(cancellation_.Token()).HasValue());
        PumpLifecycleCommit();
    }

    void ProcessCommand(const EditorWorkspaceViewCommandData &command)
    {
        controller_.ProcessCommand(command);
        PumpLifecycleCommit();
    }

    [[nodiscard]] const EditorWorkspaceViewModel &ViewModel() const noexcept
    {
        return controller_.ViewModel();
    }

    [[nodiscard]] EditorDataBus &DataBus() noexcept { return controller_.DataBus(); }

    [[nodiscard]] ViewportRevision CurrentViewportRevision() const noexcept
    {
        return controller_.CurrentViewportRevision();
    }

    [[nodiscard]] const EditorViewportSceneSnapshot &ViewportScene() const noexcept
    {
        return controller_.ViewportScene();
    }

  private:
    void PumpLifecycleCommit()
    {
        const Runtime::FrameContext context{1, {}, 0.0, 0, {}, false, cancellation_.Token()};
        assert(runtimeScene_
                   .OnPhase(Runtime::RuntimePhase::CommitDeferredLifecycleChanges, context)
                   .HasValue());
        controller_.SynchronizeRuntimeScenePreview();
    }

    Runtime::RuntimeSceneService runtimeScene_;
    CancellationSource cancellation_;
    EditorWorkspaceController controller_;
};

void MovingAnActivePanelAcrossAreasUpdatesItsRuntimePlacement()
{
    TestWorkspaceController controller;

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
    TestWorkspaceController controller;

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
    TestWorkspaceController controller;

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
    TestWorkspaceController controller;

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
    TestWorkspaceController controller;

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
    TestWorkspaceController controller;

    assert((controller.ViewModel().activityBarLayout.FindSlot("horo.viewport") ==
            ActivityBarSlot{ActivityBarRail::DocumentTop, 0, 0}));
}

void DroppingIntoTheLowerHalfSplitsTheLeftDock()
{
    TestWorkspaceController controller;

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
    TestWorkspaceController controller;

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
    TestWorkspaceController controller;

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
    TestWorkspaceController controller;

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
    TestWorkspaceController controller;

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
    TestWorkspaceController controller;

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
    TestWorkspaceController controller;

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
    TestWorkspaceController controller;

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
    TestWorkspaceController controller;

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

void SceneCommandsPublishCommittedEventsAndDriveUndoRedoState()
{
    TestWorkspaceController controller;
    std::vector<SceneDocumentChangedEvent> events;
    auto subscription = controller.DataBus().Subscribe<SceneDocumentChangedEvent>(
        [&events](const SceneDocumentChangedEvent &event) { events.push_back(event); });
    assert(controller.ViewModel().objects.size() == 1);
    assert(!controller.ViewModel().isDirty);
    assert(!controller.ViewModel().canUndo);

    EditorWorkspaceViewCommandData add;
    add.command = EditorWorkspaceViewCommand::CreatePrimitive;
    add.primitivePayload = Horo::Runtime::PrimitiveId{"primitive.mesh.box"};
    controller.ProcessCommand(add);
    assert(controller.ViewModel().objects.size() == 2);
    assert(controller.ViewModel().isDirty);
    assert(controller.ViewModel().canUndo);
    assert(events.size() == 1 && events.back().kind == DocumentChangeKind::Created);

    EditorWorkspaceViewCommandData undo;
    undo.command = EditorWorkspaceViewCommand::UndoScene;
    controller.ProcessCommand(undo);
    assert(controller.ViewModel().objects.size() == 1);
    assert(!controller.ViewModel().isDirty);
    assert(controller.ViewModel().canRedo);
    assert(events.size() == 2 && events.back().kind == DocumentChangeKind::Undone);

    EditorWorkspaceViewCommandData redo;
    redo.command = EditorWorkspaceViewCommand::RedoScene;
    controller.ProcessCommand(redo);
    assert(controller.ViewModel().objects.size() == 2);
    assert(controller.ViewModel().isDirty);
    assert(events.size() == 3 && events.back().kind == DocumentChangeKind::Redone);
}

void CatalogCreationSelectsTheResultAndHonorsTheRequestedParent()
{
    TestWorkspaceController controller;
    EditorWorkspaceViewCommandData createRoot;
    createRoot.command = EditorWorkspaceViewCommand::CreatePrimitive;
    createRoot.primitivePayload = Horo::Runtime::PrimitiveId{"primitive.object.empty"};
    controller.ProcessCommand(createRoot);
    const SceneObject root = controller.ViewModel().objects.back();
    assert(root.kind == SceneObjectKind::Empty);
    assert(!root.parent.has_value());
    assert(controller.ViewModel().primarySelection == root.id);

    EditorWorkspaceViewCommandData createCamera;
    createCamera.command = EditorWorkspaceViewCommand::CreatePrimitive;
    createCamera.primitivePayload = Horo::Runtime::PrimitiveId{"primitive.object.camera"};
    createCamera.objectPayload = root.id;
    controller.ProcessCommand(createCamera);
    const SceneObject camera = controller.ViewModel().objects.back();
    assert(camera.kind == SceneObjectKind::Camera);
    assert(camera.parent == root.id);
    assert(controller.ViewModel().primarySelection == camera.id);
    assert(controller.ViewModel().hierarchyRevealObject == camera.id);
}

void StableSelectionDrivesInspectorProjectionAndReconcilesAfterDelete()
{
    TestWorkspaceController controller;
    const SceneObjectId object = controller.ViewModel().objects.front().id;
    std::vector<SelectionChangedEvent> events;
    auto subscription = controller.DataBus().Subscribe<SelectionChangedEvent>(
        [&events](const SelectionChangedEvent &event) { events.push_back(event); });

    EditorWorkspaceViewCommandData select;
    select.command = EditorWorkspaceViewCommand::SelectObject;
    select.objectPayload = object;
    controller.ProcessCommand(select);
    assert(controller.ViewModel().primarySelection == object);
    assert(controller.ViewportScene().instances.front().presentation.tintStrength > 0.0F);
    assert(events.size() == 1 && events.back().kind == SelectionChangeKind::ObjectsChanged);

    EditorWorkspaceViewCommandData remove;
    remove.command = EditorWorkspaceViewCommand::DeleteObject;
    remove.objectPayload = object;
    controller.ProcessCommand(remove);
    assert(controller.ViewModel().objects.empty());
    assert(!controller.ViewModel().primarySelection.has_value());
    assert(events.size() == 2 && events.back().kind == SelectionChangeKind::Cleared);

    EditorWorkspaceViewCommandData undo;
    undo.command = EditorWorkspaceViewCommand::UndoScene;
    controller.ProcessCommand(undo);
    assert(controller.ViewModel().objects.size() == 1);
    assert(controller.ViewModel().objects.front().id == object);
    assert(!controller.ViewModel().primarySelection.has_value());
    assert(events.size() == 2);
    static_cast<void>(subscription);
}

void ViewportPickingUsesTheAuthoritativeSelectionModel()
{
    TestWorkspaceController controller;
    const SceneObjectId object = controller.ViewModel().objects.front().id;
    std::vector<SelectionChangedEvent> events;
    auto subscription = controller.DataBus().Subscribe<SelectionChangedEvent>(
        [&events](const SelectionChangedEvent &event) { events.push_back(event); });

    EditorWorkspaceViewCommandData hit;
    hit.command = EditorWorkspaceViewCommand::PickViewport;
    hit.viewportPickPayload = ViewportPickRequest{.normalizedX = 0.5F, .normalizedY = 0.5F, .aspect = 1.0F};
    controller.ProcessCommand(hit);
    assert(controller.ViewModel().primarySelection == object);
    assert(controller.ViewportScene().instances.front().presentation.tintStrength > 0.0F);
    assert(events.size() == 1 && events.back().kind == SelectionChangeKind::ObjectsChanged);

    EditorWorkspaceViewCommandData miss;
    miss.command = EditorWorkspaceViewCommand::PickViewport;
    miss.viewportPickPayload = ViewportPickRequest{.normalizedX = 0.0F, .normalizedY = 0.0F, .aspect = 1.0F};
    controller.ProcessCommand(miss);
    assert(!controller.ViewModel().primarySelection.has_value());
    assert(controller.ViewportScene().instances.front().presentation.tintStrength == 0.0F);
    assert(events.size() == 2 && events.back().kind == SelectionChangeKind::Cleared);
    static_cast<void>(subscription);
}

void TransformCommandsUpdateTheDocumentProjectionViewportAndHistory()
{
    TestWorkspaceController controller;
    const SceneObjectId object = controller.ViewModel().objects.front().id;
    const Math::Transform original = controller.ViewModel().objects.front().localTransform;
    std::vector<SceneDocumentChangedEvent> events;
    auto subscription = controller.DataBus().Subscribe<SceneDocumentChangedEvent>(
        [&events](const SceneDocumentChangedEvent &event) { events.push_back(event); });

    const Math::Transform edited{
        .translation = {2.0F, 3.0F, -1.0F},
        .rotation = Math::Quaternion::FromEulerRadians({0.2F, -0.3F, 0.4F}),
        .scale = {1.5F, 2.0F, 0.5F},
    };
    EditorWorkspaceViewCommandData transform;
    transform.command = EditorWorkspaceViewCommand::UpdateObjectTransform;
    transform.objectPayload = object;
    transform.transformPayload = edited;
    controller.ProcessCommand(transform);

    assert(controller.ViewModel().objects.front().localTransform == edited);
    assert(controller.ViewModel().isDirty);
    assert(controller.ViewModel().canUndo);
    assert(events.size() == 1);
    assert(events.back().kind == DocumentChangeKind::TransformChanged);
    assert(events.back().affectedObjects == std::vector{object});
    const Math::Vec3 worldOrigin =
        Math::TransformPoint(controller.ViewportScene().instances.front().localToWorld, {});
    assert(worldOrigin == edited.translation);

    EditorWorkspaceViewCommandData undo;
    undo.command = EditorWorkspaceViewCommand::UndoScene;
    controller.ProcessCommand(undo);
    assert(controller.ViewModel().objects.front().localTransform == original);
    assert(!controller.ViewModel().isDirty);
    assert(events.size() == 2 && events.back().kind == DocumentChangeKind::Undone);
    static_cast<void>(subscription);
}

void ViewportNavigationUpdatesOnlyTheEditorCameraAuthority()
{
    TestWorkspaceController controller;
    const EditorViewportCamera before = controller.ViewportScene().camera;
    const DocumentRevision documentRevision = controller.ViewModel().documentRevision;
    std::vector<ViewportChangedEvent> events;
    auto subscription = controller.DataBus().Subscribe<ViewportChangedEvent>(
        [&events](const ViewportChangedEvent &event) { events.push_back(event); });

    EditorWorkspaceViewCommandData navigate;
    navigate.command = EditorWorkspaceViewCommand::NavigateViewport;
    navigate.viewportNavigationPayload =
        EditorViewportNavigationDelta{.yawRadians = 0.2F, .moveForward = 0.5F};
    controller.ProcessCommand(navigate);

    assert(controller.CurrentViewportRevision() == ViewportRevision{1});
    assert(controller.ViewportScene().camera.IsValid());
    assert(controller.ViewportScene().camera.position != before.position);
    assert(controller.ViewModel().documentRevision == documentRevision);
    assert(!controller.ViewModel().isDirty);
    assert(!controller.ViewModel().canUndo);
    assert(events.size() == 1 && events.front().kind == ViewportChangeKind::CameraMoved);
    static_cast<void>(subscription);
}

void GizmoPreviewIsTransientAndCommitCreatesOneUndoableDocumentChange()
{
    TestWorkspaceController controller;
    const SceneObjectId object = controller.ViewModel().objects.front().id;
    const DocumentRevision initialRevision = controller.ViewModel().documentRevision;
    Math::Transform edited = controller.ViewModel().objects.front().localTransform;
    edited.translation = {1.0F, 2.0F, -0.5F};
    std::vector<SceneDocumentChangedEvent> documentEvents;
    std::vector<ViewportChangedEvent> viewportEvents;
    auto documentSubscription = controller.DataBus().Subscribe<SceneDocumentChangedEvent>(
        [&documentEvents](const SceneDocumentChangedEvent &event) { documentEvents.push_back(event); });
    auto viewportSubscription = controller.DataBus().Subscribe<ViewportChangedEvent>(
        [&viewportEvents](const ViewportChangedEvent &event) { viewportEvents.push_back(event); });

    EditorWorkspaceViewCommandData preview;
    preview.command = EditorWorkspaceViewCommand::PreviewObjectTransform;
    preview.objectPayload = object;
    preview.transformPayload = edited;
    controller.ProcessCommand(preview);
    assert(controller.ViewModel().objects.front().localTransform != edited);
    assert(controller.ViewModel().documentRevision == initialRevision);
    assert(!controller.ViewModel().isDirty && !controller.ViewModel().canUndo);
    const Math::Vec3 previewOrigin =
        Math::TransformPoint(controller.ViewportScene().instances.front().localToWorld, {});
    assert(previewOrigin == edited.translation);
    assert(documentEvents.empty());
    assert(viewportEvents.size() == 1 && viewportEvents.front().kind == ViewportChangeKind::ScenePreviewChanged);

    EditorWorkspaceViewCommandData commit;
    commit.command = EditorWorkspaceViewCommand::CommitObjectTransform;
    commit.objectPayload = object;
    commit.transformPayload = edited;
    controller.ProcessCommand(commit);
    assert(controller.ViewModel().objects.front().localTransform == edited);
    assert(controller.ViewModel().documentRevision.value == initialRevision.value + 1);
    assert(controller.ViewModel().isDirty && controller.ViewModel().canUndo);
    assert(documentEvents.size() == 1 && documentEvents.front().kind == DocumentChangeKind::TransformChanged);
    assert(controller.CurrentViewportRevision() == ViewportRevision{2});
    assert(viewportEvents.size() == 2 && viewportEvents.back().kind == ViewportChangeKind::ScenePreviewChanged);

    EditorWorkspaceViewCommandData undo;
    undo.command = EditorWorkspaceViewCommand::UndoScene;
    controller.ProcessCommand(undo);
    assert(!controller.ViewModel().isDirty);
    static_cast<void>(documentSubscription);
    static_cast<void>(viewportSubscription);
}

void CancellingGizmoPreviewRestoresTheExactCommittedProjection()
{
    TestWorkspaceController controller;
    const SceneObjectId object = controller.ViewModel().objects.front().id;
    const Math::Transform committedTransform = controller.ViewModel().objects.front().localTransform;
    const Math::Mat4 committedMatrix = controller.ViewportScene().instances.front().localToWorld;
    const DocumentRevision committedRevision = controller.ViewModel().documentRevision;
    std::vector<SceneDocumentChangedEvent> documentEvents;
    std::vector<ViewportChangedEvent> viewportEvents;
    auto documentSubscription = controller.DataBus().Subscribe<SceneDocumentChangedEvent>(
        [&documentEvents](const SceneDocumentChangedEvent &event) { documentEvents.push_back(event); });
    auto viewportSubscription = controller.DataBus().Subscribe<ViewportChangedEvent>(
        [&viewportEvents](const ViewportChangedEvent &event) { viewportEvents.push_back(event); });

    Math::Transform previewTransform = committedTransform;
    previewTransform.translation = {-2.0F, 0.5F, 1.0F};
    EditorWorkspaceViewCommandData preview;
    preview.command = EditorWorkspaceViewCommand::PreviewObjectTransform;
    preview.objectPayload = object;
    preview.transformPayload = previewTransform;
    controller.ProcessCommand(preview);

    EditorWorkspaceViewCommandData cancel;
    cancel.command = EditorWorkspaceViewCommand::CancelObjectTransformPreview;
    controller.ProcessCommand(cancel);

    assert(controller.ViewModel().objects.front().localTransform == committedTransform);
    assert(controller.ViewportScene().instances.front().localToWorld == committedMatrix);
    assert(controller.ViewModel().documentRevision == committedRevision);
    assert(!controller.ViewModel().isDirty && !controller.ViewModel().canUndo);
    assert(documentEvents.empty());
    assert(controller.CurrentViewportRevision() == ViewportRevision{2});
    assert(viewportEvents.size() == 2);
    static_cast<void>(documentSubscription);
    static_cast<void>(viewportSubscription);
}

void NoOpGizmoCommitClearsPreviewWithoutCreatingHistory()
{
    TestWorkspaceController controller;
    const SceneObjectId object = controller.ViewModel().objects.front().id;
    const Math::Transform committedTransform = controller.ViewModel().objects.front().localTransform;
    Math::Transform movedTransform = committedTransform;
    movedTransform.translation = {3.0F, 0.0F, 0.0F};

    EditorWorkspaceViewCommandData previewMoved;
    previewMoved.command = EditorWorkspaceViewCommand::PreviewObjectTransform;
    previewMoved.objectPayload = object;
    previewMoved.transformPayload = movedTransform;
    controller.ProcessCommand(previewMoved);

    EditorWorkspaceViewCommandData previewRestored = previewMoved;
    previewRestored.transformPayload = committedTransform;
    controller.ProcessCommand(previewRestored);

    EditorWorkspaceViewCommandData commit;
    commit.command = EditorWorkspaceViewCommand::CommitObjectTransform;
    commit.objectPayload = object;
    commit.transformPayload = committedTransform;
    controller.ProcessCommand(commit);

    assert(controller.ViewModel().objects.front().localTransform == committedTransform);
    assert(!controller.ViewModel().isDirty && !controller.ViewModel().canUndo);
    assert(controller.CurrentViewportRevision() == ViewportRevision{3});
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
    SceneCommandsPublishCommittedEventsAndDriveUndoRedoState();
    CatalogCreationSelectsTheResultAndHonorsTheRequestedParent();
    StableSelectionDrivesInspectorProjectionAndReconcilesAfterDelete();
    ViewportPickingUsesTheAuthoritativeSelectionModel();
    TransformCommandsUpdateTheDocumentProjectionViewportAndHistory();
    ViewportNavigationUpdatesOnlyTheEditorCameraAuthority();
    GizmoPreviewIsTransientAndCommitCreatesOneUndoableDocumentChange();
    CancellingGizmoPreviewRestoresTheExactCommittedProjection();
    NoOpGizmoCommitClearsPreviewWithoutCreatingHistory();
    return 0;
}
