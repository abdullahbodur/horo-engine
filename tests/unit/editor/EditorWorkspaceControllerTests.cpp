#include <catch2/catch_test_macros.hpp>

#include "editor/screens/workspace/EditorWorkspaceController.h"

namespace
{
    using namespace Horo;
    using namespace Horo::Editor;
    namespace Math = Math;

    class TestWorkspaceController final
    {
    public:
        TestWorkspaceController() : controller_("test-project", runtimeScene_)
        {
            REQUIRE((runtimeScene_.Startup(cancellation_.Token()).HasValue()));
            PumpLifecycleCommit();
        }

        void ProcessCommand(const EditorWorkspaceViewCommandData& command)
        {
            controller_.ProcessCommand(command);
            PumpLifecycleCommit();
        }

        [[nodiscard]] const EditorWorkspaceViewModel& ViewModel() const noexcept
        {
            return controller_.ViewModel();
        }

        [[nodiscard]] EditorDataBus& DataBus() noexcept
        {
            return controller_.DataBus();
        }

        [[nodiscard]] ViewportRevision CurrentViewportRevision() const noexcept
        {
            return controller_.CurrentViewportRevision();
        }

        [[nodiscard]] const EditorViewportSceneSnapshot& ViewportScene() const noexcept
        {
            return controller_.ViewportScene();
        }

    private:
        void PumpLifecycleCommit()
        {
            const Runtime::FrameContext context{1, {}, 0.0, 0, {}, false, cancellation_.Token()};
            REQUIRE((runtimeScene_.OnPhase(Runtime::RuntimePhase::CommitDeferredLifecycleChanges, context).HasValue()));
            controller_.SynchronizeRuntimeScenePreview();
        }

        Runtime::RuntimeSceneService runtimeScene_;
        CancellationSource cancellation_;
        EditorWorkspaceController controller_;
    };

    TEST_CASE("Moving An Active Panel Across Areas Updates Its Runtime Placement", "[unit][editor]")
    {
        TestWorkspaceController controller;

        EditorWorkspaceViewCommandData command;
        command.command = EditorWorkspaceViewCommand::ChangeActivePanel;
        command.targetIndex = 2;
        command.stringPayload = "horo.hierarchy";
        command.activityBarSlot = ActivityBarSlot{ActivityBarRail::Left, 2, 1};
        controller.ProcessCommand(command);

        const auto& viewModel = controller.ViewModel();
        REQUIRE((viewModel.activeLeftPanelId.empty()));
        REQUIRE((viewModel.activeBottomPanelId == "horo.hierarchy"));
        REQUIRE((viewModel.panelDockAreas.at("horo.hierarchy") == WorkspaceDockArea::Bottom));
        const auto slot = viewModel.activityBarLayout.FindSlot("horo.hierarchy");
        REQUIRE((slot.has_value()));
        REQUIRE((*slot == ActivityBarSlot{ActivityBarRail::Left, 2, 1}));
        REQUIRE((viewModel.activityBarLayout.ItemAt(ActivityBarRail::Left, 2, 0) == "horo.content_browser"));
    }

    TEST_CASE("Replacing A Target Area Preserves The Displaced Panels Placement", "[unit][editor]")
    {
        TestWorkspaceController controller;

        EditorWorkspaceViewCommandData command;
        command.command = EditorWorkspaceViewCommand::ChangeActivePanel;
        command.targetIndex = 2;
        command.stringPayload = "horo.hierarchy";
        controller.ProcessCommand(command);

        const auto& viewModel = controller.ViewModel();
        REQUIRE((viewModel.activeBottomPanelId == "horo.hierarchy"));
        REQUIRE((viewModel.panelDockAreas.at("horo.content_browser") == WorkspaceDockArea::Bottom));
    }

    TEST_CASE("Dropping Into Bottom Right Splits The Full Bottom Dock", "[unit][editor]")
    {
        TestWorkspaceController controller;

        EditorWorkspaceViewCommandData command;
        command.command = EditorWorkspaceViewCommand::ChangeActivePanel;
        command.targetIndex = 2;
        command.stringPayload = "horo.inspector";
        command.bottomDockSlot = BottomDockSlot::Right;
        command.activityBarSlot = ActivityBarSlot{ActivityBarRail::Right, 2, 0};
        controller.ProcessCommand(command);

        const auto& viewModel = controller.ViewModel();
        REQUIRE((viewModel.bottomDockMode == BottomDockMode::Split));
        REQUIRE((viewModel.activeBottomPanelId.empty()));
        REQUIRE((viewModel.activeBottomLeftPanelId == "horo.content_browser"));
        REQUIRE((viewModel.activeBottomRightPanelId == "horo.inspector"));
        REQUIRE(
            (viewModel.activityBarLayout.FindSlot("horo.inspector") == ActivityBarSlot{ActivityBarRail::Right, 2, 0}));
    }

    TEST_CASE("Clicking A Split Panel Expands It To The Full Bottom Dock", "[unit][editor]")
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

        const auto& viewModel = controller.ViewModel();
        REQUIRE((viewModel.bottomDockMode == BottomDockMode::Full));
        REQUIRE((viewModel.activeBottomPanelId == "horo.inspector"));
        REQUIRE((viewModel.activeBottomLeftPanelId.empty()));
        REQUIRE((viewModel.activeBottomRightPanelId.empty()));
    }

    TEST_CASE("Dropping A Left Rail Icon Into Bottom Right Moves It To The Right Rail", "[unit][editor]")
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

        const auto& viewModel = controller.ViewModel();
        REQUIRE((viewModel.bottomDockMode == BottomDockMode::Split));
        REQUIRE((viewModel.activeBottomLeftPanelId == "horo.inspector"));
        REQUIRE((viewModel.activeBottomRightPanelId == "horo.hierarchy"));
        REQUIRE(
            (viewModel.activityBarLayout.FindSlot("horo.hierarchy") == ActivityBarSlot{ActivityBarRail::Right, 2, 0}));
    }

    TEST_CASE("Places The Viewport In The Document Top Rail By Default", "[unit][editor]")
    {
        TestWorkspaceController controller;

        REQUIRE((controller.ViewModel().activityBarLayout.FindSlot("horo.viewport") ==
            ActivityBarSlot{ActivityBarRail::DocumentTop, 0, 0}));
    }

    TEST_CASE("Dropping Into The Lower Half Splits The Left Dock", "[unit][editor]")
    {
        TestWorkspaceController controller;

        EditorWorkspaceViewCommandData command;
        command.command = EditorWorkspaceViewCommand::ChangeActivePanel;
        command.targetIndex = 0;
        command.stringPayload = "horo.inspector";
        command.sideDockSlot = SideDockSlot::Bottom;
        command.activityBarSlot = ActivityBarSlot{ActivityBarRail::Left, 1, 0};
        controller.ProcessCommand(command);

        const auto& viewModel = controller.ViewModel();
        REQUIRE((viewModel.leftDockMode == SideDockMode::Split));
        REQUIRE((viewModel.activeLeftPanelId.empty()));
        REQUIRE((viewModel.activeLeftTopPanelId == "horo.hierarchy"));
        REQUIRE((viewModel.activeLeftBottomPanelId == "horo.inspector"));
        REQUIRE((viewModel.activeRightPanelId.empty()));
        REQUIRE(
            (viewModel.activityBarLayout.FindSlot("horo.inspector") == ActivityBarSlot{ActivityBarRail::Left, 1, 0}));
    }

    TEST_CASE("Dropping Into The Lower Half Splits The Right Dock", "[unit][editor]")
    {
        TestWorkspaceController controller;

        EditorWorkspaceViewCommandData command;
        command.command = EditorWorkspaceViewCommand::ChangeActivePanel;
        command.targetIndex = 1;
        command.stringPayload = "horo.hierarchy";
        command.sideDockSlot = SideDockSlot::Bottom;
        command.activityBarSlot = ActivityBarSlot{ActivityBarRail::Right, 1, 0};
        controller.ProcessCommand(command);

        const auto& viewModel = controller.ViewModel();
        REQUIRE((viewModel.rightDockMode == SideDockMode::Split));
        REQUIRE((viewModel.activeRightPanelId.empty()));
        REQUIRE((viewModel.activeRightTopPanelId == "horo.inspector"));
        REQUIRE((viewModel.activeRightBottomPanelId == "horo.hierarchy"));
        REQUIRE((viewModel.activeLeftPanelId.empty()));
        REQUIRE(
            (viewModel.activityBarLayout.FindSlot("horo.hierarchy") == ActivityBarSlot{ActivityBarRail::Right, 1, 0}));
    }

    TEST_CASE("Clicking A Split Side Panel Expands It To The Full Dock", "[unit][editor]")
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

        const auto& viewModel = controller.ViewModel();
        REQUIRE((viewModel.leftDockMode == SideDockMode::Full));
        REQUIRE((viewModel.activeLeftPanelId == "horo.hierarchy"));
        REQUIRE((viewModel.activeLeftTopPanelId.empty()));
        REQUIRE((viewModel.activeLeftBottomPanelId.empty()));
    }

    TEST_CASE("Moving One Half Away Expands The Remaining Side Panel", "[unit][editor]")
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

        const auto& viewModel = controller.ViewModel();
        REQUIRE((viewModel.leftDockMode == SideDockMode::Full));
        REQUIRE((viewModel.activeLeftPanelId == "horo.hierarchy"));
        REQUIRE((viewModel.activeLeftTopPanelId.empty()));
        REQUIRE((viewModel.activeLeftBottomPanelId.empty()));
    }

    TEST_CASE("Reordering An Active Icon Moves Its Panel And Activates A Source Fallback", "[unit][editor]")
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

        const auto& viewModel = controller.ViewModel();
        REQUIRE((viewModel.leftDockMode == SideDockMode::Split));
        REQUIRE((viewModel.activeLeftTopPanelId == "horo.content_browser"));
        REQUIRE((viewModel.activeLeftBottomPanelId == "horo.hierarchy"));
        REQUIRE((viewModel.activeLeftBottomPanelId != "horo.inspector"));
        REQUIRE((viewModel.panelDockAreas.at("horo.hierarchy") == WorkspaceDockArea::Left));
        REQUIRE((viewModel.panelDockAreas.at("horo.content_browser") == WorkspaceDockArea::Left));
        REQUIRE(
            (viewModel.activityBarLayout.FindSlot("horo.hierarchy") == ActivityBarSlot{ActivityBarRail::Left, 1, 1}));
    }

    TEST_CASE("Reordering The Only Bottom Icon Does Not Leave A Half Empty Split", "[unit][editor]")
    {
        TestWorkspaceController controller;

        EditorWorkspaceViewCommandData command;
        command.command = EditorWorkspaceViewCommand::ReorderActivityBarItem;
        command.stringPayload = "horo.content_browser";
        command.activityBarSlot = ActivityBarSlot{ActivityBarRail::Right, 2, 0};
        controller.ProcessCommand(command);

        const auto& viewModel = controller.ViewModel();
        REQUIRE((viewModel.bottomDockMode == BottomDockMode::Full));
        REQUIRE((viewModel.activeBottomPanelId == "horo.content_browser"));
        REQUIRE((viewModel.activeBottomLeftPanelId.empty()));
        REQUIRE((viewModel.activeBottomRightPanelId.empty()));
    }

    TEST_CASE("Dropping On A Side Merge Target Expands The Panel Without Moving Its Icon", "[unit][editor]")
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

        const auto& viewModel = controller.ViewModel();
        REQUIRE((viewModel.leftDockMode == SideDockMode::Full));
        REQUIRE((viewModel.activeLeftPanelId == "horo.hierarchy"));
        REQUIRE((viewModel.activeLeftTopPanelId.empty()));
        REQUIRE((viewModel.activeLeftBottomPanelId.empty()));
        REQUIRE(
            (viewModel.activityBarLayout.FindSlot("horo.hierarchy") == ActivityBarSlot{ActivityBarRail::Left, 0, 0}));
        REQUIRE(
            (viewModel.activityBarLayout.FindSlot("horo.inspector") == ActivityBarSlot{ActivityBarRail::Left, 1, 0}));

        EditorWorkspaceViewCommandData replaceFullCommand;
        replaceFullCommand.command = EditorWorkspaceViewCommand::ChangeActivePanel;
        replaceFullCommand.targetIndex = 0;
        replaceFullCommand.stringPayload = "horo.inspector";
        controller.ProcessCommand(replaceFullCommand);

        REQUIRE((viewModel.leftDockMode == SideDockMode::Full));
        REQUIRE((viewModel.activeLeftPanelId == "horo.inspector"));
        REQUIRE(
            (viewModel.activityBarLayout.FindSlot("horo.inspector") == ActivityBarSlot{ActivityBarRail::Left, 1, 0}));
    }

    TEST_CASE("Dropping On The Bottom Merge Target Preserves Its Activity Group", "[unit][editor]")
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

        const auto& viewModel = controller.ViewModel();
        REQUIRE((viewModel.bottomDockMode == BottomDockMode::Full));
        REQUIRE((viewModel.activeBottomPanelId == "horo.content_browser"));
        REQUIRE((viewModel.activeBottomLeftPanelId.empty()));
        REQUIRE((viewModel.activeBottomRightPanelId.empty()));
        REQUIRE(
            (viewModel.activityBarLayout.FindSlot("horo.content_browser") == ActivityBarSlot{ActivityBarRail::Left, 2, 0
                }));
        REQUIRE(
            (viewModel.activityBarLayout.FindSlot("horo.inspector") == ActivityBarSlot{ActivityBarRail::Right, 2, 0}));
    }

    TEST_CASE("Reordering An Active Bottom Panel To The Left Does Not Render It Twice", "[unit][editor]")
    {
        TestWorkspaceController controller;

        EditorWorkspaceViewCommandData command;
        command.command = EditorWorkspaceViewCommand::ReorderActivityBarItem;
        command.stringPayload = "horo.content_browser";
        command.activityBarSlot = ActivityBarSlot{ActivityBarRail::Left, 1, 0};
        controller.ProcessCommand(command);

        const auto& viewModel = controller.ViewModel();
        REQUIRE((viewModel.leftDockMode == SideDockMode::Split));
        REQUIRE((viewModel.activeLeftTopPanelId == "horo.hierarchy"));
        REQUIRE((viewModel.activeLeftBottomPanelId == "horo.content_browser"));
        REQUIRE((viewModel.activeBottomPanelId.empty()));
        REQUIRE((viewModel.activeBottomLeftPanelId.empty()));
        REQUIRE((viewModel.activeBottomRightPanelId.empty()));
    }

    TEST_CASE("Scene Commands Publish Committed Events And Drive Undo Redo State", "[unit][editor]")
    {
        TestWorkspaceController controller;
        std::vector<SceneDocumentChangedEvent> events;
        auto subscription = controller.DataBus().Subscribe<SceneDocumentChangedEvent>(
            [&events](const SceneDocumentChangedEvent& event) { events.push_back(event); });
        REQUIRE((controller.ViewModel().objects.size() == 1));
        REQUIRE((!controller.ViewModel().isDirty));
        REQUIRE((!controller.ViewModel().canUndo));

        EditorWorkspaceViewCommandData add;
        add.command = EditorWorkspaceViewCommand::CreatePrimitive;
        add.primitivePayload = Runtime::PrimitiveId{"primitive.mesh.box"};
        controller.ProcessCommand(add);
        REQUIRE((controller.ViewModel().objects.size() == 2));
        REQUIRE((controller.ViewModel().isDirty));
        REQUIRE((controller.ViewModel().canUndo));
        REQUIRE((events.size() == 1 && events.back().kind == DocumentChangeKind::Created));

        EditorWorkspaceViewCommandData undo;
        undo.command = EditorWorkspaceViewCommand::UndoScene;
        controller.ProcessCommand(undo);
        REQUIRE((controller.ViewModel().objects.size() == 1));
        REQUIRE((!controller.ViewModel().isDirty));
        REQUIRE((controller.ViewModel().canRedo));
        REQUIRE((events.size() == 2 && events.back().kind == DocumentChangeKind::Undone));

        EditorWorkspaceViewCommandData redo;
        redo.command = EditorWorkspaceViewCommand::RedoScene;
        controller.ProcessCommand(redo);
        REQUIRE((controller.ViewModel().objects.size() == 2));
        REQUIRE((controller.ViewModel().isDirty));
        REQUIRE((events.size() == 3 && events.back().kind == DocumentChangeKind::Redone));
    }

    TEST_CASE("Catalog Creation Selects The Result And Honors The Requested Parent", "[unit][editor]")
    {
        TestWorkspaceController controller;
        EditorWorkspaceViewCommandData createRoot;
        createRoot.command = EditorWorkspaceViewCommand::CreatePrimitive;
        createRoot.primitivePayload = Runtime::PrimitiveId{"primitive.object.empty"};
        controller.ProcessCommand(createRoot);
        const SceneObject root = controller.ViewModel().objects.back();
        REQUIRE((root.kind == SceneObjectKind::Empty));
        REQUIRE((!root.parent.has_value()));
        REQUIRE((controller.ViewModel().primarySelection == root.id));

        EditorWorkspaceViewCommandData createCamera;
        createCamera.command = EditorWorkspaceViewCommand::CreatePrimitive;
        createCamera.primitivePayload = Runtime::PrimitiveId{"primitive.object.camera"};
        createCamera.objectPayload = root.id;
        controller.ProcessCommand(createCamera);
        const SceneObject camera = controller.ViewModel().objects.back();
        REQUIRE((camera.kind == SceneObjectKind::Camera));
        REQUIRE((camera.parent == root.id));
        REQUIRE((controller.ViewModel().primarySelection == camera.id));
        REQUIRE((controller.ViewModel().hierarchyRevealObject == camera.id));
    }

    TEST_CASE("Stable Selection Drives Inspector Projection And Reconciles After Delete", "[unit][editor]")
    {
        TestWorkspaceController controller;
        const SceneObjectId object = controller.ViewModel().objects.front().id;
        std::vector<SelectionChangedEvent> events;
        auto subscription = controller.DataBus().Subscribe<SelectionChangedEvent>(
            [&events](const SelectionChangedEvent& event) { events.push_back(event); });

        EditorWorkspaceViewCommandData select;
        select.command = EditorWorkspaceViewCommand::SelectObject;
        select.objectPayload = object;
        controller.ProcessCommand(select);
        REQUIRE((controller.ViewModel().primarySelection == object));
        REQUIRE((controller.ViewportScene().instances.front().presentation.tintStrength > 0.0F));
        REQUIRE((events.size() == 1 && events.back().kind == SelectionChangeKind::ObjectsChanged));

        EditorWorkspaceViewCommandData remove;
        remove.command = EditorWorkspaceViewCommand::DeleteObject;
        remove.objectPayload = object;
        controller.ProcessCommand(remove);
        REQUIRE((controller.ViewModel().objects.empty()));
        REQUIRE((!controller.ViewModel().primarySelection.has_value()));
        REQUIRE((events.size() == 2 && events.back().kind == SelectionChangeKind::Cleared));

        EditorWorkspaceViewCommandData undo;
        undo.command = EditorWorkspaceViewCommand::UndoScene;
        controller.ProcessCommand(undo);
        REQUIRE((controller.ViewModel().objects.size() == 1));
        REQUIRE((controller.ViewModel().objects.front().id == object));
        REQUIRE((!controller.ViewModel().primarySelection.has_value()));
        REQUIRE((events.size() == 2));
        static_cast<void>(subscription);
    }

    TEST_CASE("Viewport Picking Uses The Authoritative Selection Model", "[unit][editor]")
    {
        TestWorkspaceController controller;
        const SceneObjectId object = controller.ViewModel().objects.front().id;
        std::vector<SelectionChangedEvent> events;
        auto subscription = controller.DataBus().Subscribe<SelectionChangedEvent>(
            [&events](const SelectionChangedEvent& event) { events.push_back(event); });

        EditorWorkspaceViewCommandData hit;
        hit.command = EditorWorkspaceViewCommand::PickViewport;
        hit.viewportPickPayload = ViewportPickRequest{.normalizedX = 0.5F, .normalizedY = 0.5F, .aspect = 1.0F};
        controller.ProcessCommand(hit);
        REQUIRE((controller.ViewModel().primarySelection == object));
        REQUIRE((controller.ViewportScene().instances.front().presentation.tintStrength > 0.0F));
        REQUIRE((events.size() == 1 && events.back().kind == SelectionChangeKind::ObjectsChanged));

        EditorWorkspaceViewCommandData miss;
        miss.command = EditorWorkspaceViewCommand::PickViewport;
        miss.viewportPickPayload = ViewportPickRequest{.normalizedX = 0.0F, .normalizedY = 0.0F, .aspect = 1.0F};
        controller.ProcessCommand(miss);
        REQUIRE((!controller.ViewModel().primarySelection.has_value()));
        REQUIRE((controller.ViewportScene().instances.front().presentation.tintStrength == 0.0F));
        REQUIRE((events.size() == 2 && events.back().kind == SelectionChangeKind::Cleared));
        static_cast<void>(subscription);
    }

    TEST_CASE("Transform Commands Update The Document Projection Viewport And History", "[unit][editor]")
    {
        TestWorkspaceController controller;
        const SceneObjectId object = controller.ViewModel().objects.front().id;
        const Math::Transform original = controller.ViewModel().objects.front().localTransform;
        std::vector<SceneDocumentChangedEvent> events;
        auto subscription = controller.DataBus().Subscribe<SceneDocumentChangedEvent>(
            [&events](const SceneDocumentChangedEvent& event) { events.push_back(event); });

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

        REQUIRE((controller.ViewModel().objects.front().localTransform == edited));
        REQUIRE((controller.ViewModel().isDirty));
        REQUIRE((controller.ViewModel().canUndo));
        REQUIRE((events.size() == 1));
        REQUIRE((events.back().kind == DocumentChangeKind::TransformChanged));
        REQUIRE((events.back().affectedObjects == std::vector{object}));
        const Math::Vec3 worldOrigin = Math::TransformPoint(controller.ViewportScene().instances.front().localToWorld,
                                                            {});
        REQUIRE((worldOrigin == edited.translation));

        EditorWorkspaceViewCommandData undo;
        undo.command = EditorWorkspaceViewCommand::UndoScene;
        controller.ProcessCommand(undo);
        REQUIRE((controller.ViewModel().objects.front().localTransform == original));
        REQUIRE((!controller.ViewModel().isDirty));
        REQUIRE((events.size() == 2 && events.back().kind == DocumentChangeKind::Undone));
        static_cast<void>(subscription);
    }

    TEST_CASE("Viewport Navigation Updates Only The Editor Camera Authority", "[unit][editor]")
    {
        TestWorkspaceController controller;
        const EditorViewportCamera before = controller.ViewportScene().camera;
        const DocumentRevision documentRevision = controller.ViewModel().documentRevision;
        std::vector<ViewportChangedEvent> events;
        auto subscription = controller.DataBus().Subscribe<ViewportChangedEvent>(
            [&events](const ViewportChangedEvent& event) { events.push_back(event); });

        EditorWorkspaceViewCommandData navigate;
        navigate.command = EditorWorkspaceViewCommand::NavigateViewport;
        navigate.viewportNavigationPayload = EditorViewportNavigationDelta{.yawRadians = 0.2F, .moveForward = 0.5F};
        controller.ProcessCommand(navigate);

        REQUIRE((controller.CurrentViewportRevision() == ViewportRevision{1}));
        REQUIRE((controller.ViewportScene().camera.IsValid()));
        REQUIRE((controller.ViewportScene().camera.position != before.position));
        REQUIRE((controller.ViewModel().documentRevision == documentRevision));
        REQUIRE((!controller.ViewModel().isDirty));
        REQUIRE((!controller.ViewModel().canUndo));
        REQUIRE((events.size() == 1 && events.front().kind == ViewportChangeKind::CameraMoved));
        static_cast<void>(subscription);
    }

    TEST_CASE("Gizmo Preview Is Transient And Commit Creates One Undoable Document Change", "[unit][editor]")
    {
        TestWorkspaceController controller;
        const SceneObjectId object = controller.ViewModel().objects.front().id;
        const DocumentRevision initialRevision = controller.ViewModel().documentRevision;
        Math::Transform edited = controller.ViewModel().objects.front().localTransform;
        edited.translation = {1.0F, 2.0F, -0.5F};
        std::vector<SceneDocumentChangedEvent> documentEvents;
        std::vector<ViewportChangedEvent> viewportEvents;
        auto documentSubscription = controller.DataBus().Subscribe<SceneDocumentChangedEvent>(
            [&documentEvents](const SceneDocumentChangedEvent& event) { documentEvents.push_back(event); });
        auto viewportSubscription = controller.DataBus().Subscribe<ViewportChangedEvent>(
            [&viewportEvents](const ViewportChangedEvent& event) { viewportEvents.push_back(event); });

        EditorWorkspaceViewCommandData preview;
        preview.command = EditorWorkspaceViewCommand::PreviewObjectTransform;
        preview.objectPayload = object;
        preview.transformPayload = edited;
        controller.ProcessCommand(preview);
        REQUIRE((controller.ViewModel().objects.front().localTransform != edited));
        REQUIRE((controller.ViewModel().documentRevision == initialRevision));
        REQUIRE((!controller.ViewModel().isDirty && !controller.ViewModel().canUndo));
        const Math::Vec3 previewOrigin =
            Math::TransformPoint(controller.ViewportScene().instances.front().localToWorld, {});
        REQUIRE((previewOrigin == edited.translation));
        REQUIRE((documentEvents.empty()));
        REQUIRE((viewportEvents.size() == 1 && viewportEvents.front().kind == ViewportChangeKind::ScenePreviewChanged));

        EditorWorkspaceViewCommandData commit;
        commit.command = EditorWorkspaceViewCommand::CommitObjectTransform;
        commit.objectPayload = object;
        commit.transformPayload = edited;
        controller.ProcessCommand(commit);
        REQUIRE((controller.ViewModel().objects.front().localTransform == edited));
        REQUIRE((controller.ViewModel().documentRevision.value == initialRevision.value + 1));
        REQUIRE((controller.ViewModel().isDirty && controller.ViewModel().canUndo));
        REQUIRE((documentEvents.size() == 1 && documentEvents.front().kind == DocumentChangeKind::TransformChanged));
        REQUIRE((controller.CurrentViewportRevision() == ViewportRevision{2}));
        REQUIRE((viewportEvents.size() == 2 && viewportEvents.back().kind == ViewportChangeKind::ScenePreviewChanged));

        EditorWorkspaceViewCommandData undo;
        undo.command = EditorWorkspaceViewCommand::UndoScene;
        controller.ProcessCommand(undo);
        REQUIRE((!controller.ViewModel().isDirty));
        static_cast<void>(documentSubscription);
        static_cast<void>(viewportSubscription);
    }

    TEST_CASE("Cancelling Gizmo Preview Restores The Exact Committed Projection", "[unit][editor]")
    {
        TestWorkspaceController controller;
        const SceneObjectId object = controller.ViewModel().objects.front().id;
        const Math::Transform committedTransform = controller.ViewModel().objects.front().localTransform;
        const Math::Mat4 committedMatrix = controller.ViewportScene().instances.front().localToWorld;
        const DocumentRevision committedRevision = controller.ViewModel().documentRevision;
        std::vector<SceneDocumentChangedEvent> documentEvents;
        std::vector<ViewportChangedEvent> viewportEvents;
        auto documentSubscription = controller.DataBus().Subscribe<SceneDocumentChangedEvent>(
            [&documentEvents](const SceneDocumentChangedEvent& event) { documentEvents.push_back(event); });
        auto viewportSubscription = controller.DataBus().Subscribe<ViewportChangedEvent>(
            [&viewportEvents](const ViewportChangedEvent& event) { viewportEvents.push_back(event); });

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

        REQUIRE((controller.ViewModel().objects.front().localTransform == committedTransform));
        REQUIRE((controller.ViewportScene().instances.front().localToWorld == committedMatrix));
        REQUIRE((controller.ViewModel().documentRevision == committedRevision));
        REQUIRE((!controller.ViewModel().isDirty && !controller.ViewModel().canUndo));
        REQUIRE((documentEvents.empty()));
        REQUIRE((controller.CurrentViewportRevision() == ViewportRevision{2}));
        REQUIRE((viewportEvents.size() == 2));
        static_cast<void>(documentSubscription);
        static_cast<void>(viewportSubscription);
    }

    TEST_CASE("No Op Gizmo Commit Clears Preview Without Creating History", "[unit][editor]")
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

        REQUIRE((controller.ViewModel().objects.front().localTransform == committedTransform));
        REQUIRE((!controller.ViewModel().isDirty && !controller.ViewModel().canUndo));
        REQUIRE((controller.CurrentViewportRevision() == ViewportRevision{3}));
    }
} // namespace
