#include <catch2/catch_test_macros.hpp>

#include "editor/screens/workspace/panels/hierarchy/HierarchyEditSession.h"

TEST_CASE("Hierarchy edit session projects scene objects without ImGui", "[unit][editor]") {
    using namespace Horo::Editor;

    EditorWorkspaceViewModel viewModel;
    viewModel.documentRevision = DocumentRevision{3};
    viewModel.objects = {
        SceneObject{
            .id = SceneObjectId{1},
            .name = "Root",
            .kind = SceneObjectKind::Empty,
        },
        SceneObject{
            .id = SceneObjectId{2},
            .name = "Camera",
            .kind = SceneObjectKind::Camera,
            .parent = SceneObjectId{1},
        },
    };
    viewModel.primarySelection = SceneObjectId{2};

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

    const EditorWorkspaceViewCommandData selected = HierarchyEditSession::SelectCommand(7);
    REQUIRE((selected.command == EditorWorkspaceViewCommand::SelectObject));
    REQUIRE((selected.objectPayload == SceneObjectId{7}));

    const EditorWorkspaceViewCommandData renamed = HierarchyEditSession::RenameCommand(7, "Gameplay Camera");
    REQUIRE((renamed.command == EditorWorkspaceViewCommand::UpdateObjectName));
    REQUIRE((renamed.stringPayload == "Gameplay Camera"));
    REQUIRE((HierarchyEditSession::RenameCommand(7, "").command == EditorWorkspaceViewCommand::None));

    const EditorWorkspaceViewCommandData duplicated = HierarchyEditSession::DuplicateCommand(7);
    REQUIRE((duplicated.command == EditorWorkspaceViewCommand::DuplicateObject));

    const EditorWorkspaceViewCommandData deleted = HierarchyEditSession::DeleteCommand(7);
    REQUIRE((deleted.command == EditorWorkspaceViewCommand::DeleteObject));
}
