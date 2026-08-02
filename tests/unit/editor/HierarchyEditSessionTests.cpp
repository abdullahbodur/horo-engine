#include "editor/screens/workspace/panels/hierarchy/HierarchyEditSession.h"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("Hierarchy edit session projects scene objects without ImGui", "[unit][editor]") {
    using namespace Horo::Editor;

    EditorWorkspaceViewModel viewModel;
    viewModel.documentRevision = DocumentRevision{3};
    viewModel.objects = {
        SceneObject{
            .id = SceneObjectId{1},
            .name = "Root",
            .kind = SceneObjectKind::GameObject,
        },
        SceneObject{
            .id = SceneObjectId{2},
            .name = "Camera",
            .kind = SceneObjectKind::Camera,
            .parent = SceneObjectId{1},
        },
    };
    viewModel.primarySelection = SceneObjectId{2};
    viewModel.selectedObjects = {SceneObjectId{2}};

    HierarchyEditSession session;
    session.Synchronize(viewModel);

    const std::vector<HierarchyVisibleRow> &rows = session.VisibleRows("");
    REQUIRE((rows.size() == 2));
    REQUIRE((rows[0].node->name == "Root"));
    REQUIRE((rows[1].node->name == "Camera"));
    REQUIRE((rows[1].depth == 1));
    REQUIRE((session.SelectedId() == HierarchyNodeId{2}));

    const std::vector<HierarchyVisibleRow> &filtered = session.VisibleRows("camera");
    REQUIRE((filtered.size() == 2));
}

TEST_CASE("Hierarchy edit session reduces semantic actions to typed commands", "[unit][editor]") {
    using namespace Horo::Editor;

    EditorWorkspaceViewModel viewModel;
    viewModel.documentRevision = DocumentRevision{1};
    viewModel.objects = {
        SceneObject{.id = SceneObjectId{7}, .name = "First"},
        SceneObject{.id = SceneObjectId{8}, .name = "Second"},
        SceneObject{.id = SceneObjectId{9}, .name = "Third"},
    };
    viewModel.primarySelection = SceneObjectId{7};
    viewModel.selectedObjects = {SceneObjectId{7}};
    HierarchyEditSession session;
    session.Synchronize(viewModel);
    static_cast<void>(session.VisibleRows(""));

    const EditorWorkspaceViewCommandData selected = session.SelectCommand(8, HierarchySelectionGesture::Replace);
    REQUIRE((selected.command == EditorWorkspaceViewCommand::SelectObject));
    REQUIRE((selected.objectSelection->objects == std::vector{SceneObjectId{8}}));
    REQUIRE((selected.objectSelection->primary == SceneObjectId{8}));

    const EditorWorkspaceViewCommandData toggled = session.SelectCommand(8, HierarchySelectionGesture::Toggle);
    REQUIRE((toggled.objectSelection->objects == std::vector({SceneObjectId{7}, SceneObjectId{8}})));
    REQUIRE((toggled.objectSelection->primary == SceneObjectId{8}));

    const EditorWorkspaceViewCommandData ranged = session.SelectCommand(9, HierarchySelectionGesture::Range);
    REQUIRE((ranged.objectSelection->objects == std::vector({SceneObjectId{8}, SceneObjectId{9}})));
    REQUIRE((ranged.objectSelection->primary == SceneObjectId{9}));

    const EditorWorkspaceViewCommandData renamed = HierarchyEditSession::RenameCommand(7, "Gameplay Camera");
    REQUIRE((renamed.command == EditorWorkspaceViewCommand::UpdateObjectName));
    REQUIRE((renamed.stringPayload == "Gameplay Camera"));
    REQUIRE((HierarchyEditSession::RenameCommand(7, "").command == EditorWorkspaceViewCommand::None));

    const EditorWorkspaceViewCommandData duplicated = HierarchyEditSession::DuplicateCommand(7);
    REQUIRE((duplicated.command == EditorWorkspaceViewCommand::DuplicateObject));

    const EditorWorkspaceViewCommandData deleted = HierarchyEditSession::DeleteCommand(7);
    REQUIRE((deleted.command == EditorWorkspaceViewCommand::DeleteObject));
}
