#include <catch2/catch_test_macros.hpp>

#include "editor/screens/workspace/panels/inspector/InspectorEditSession.h"

namespace
{
    [[nodiscard]] Horo::Editor::SceneObject MakeCameraObject(
        const Horo::Editor::SceneObjectId id)
    {
        Horo::Editor::SceneObject object{
            .id = id,
            .name = "Camera",
            .kind = Horo::Editor::SceneObjectKind::Camera,
        };
        object.components.camera = Horo::Runtime::CameraComponent{};
        return object;
    }
} // namespace

TEST_CASE(
    "Inspector edit session reduces name edits without ImGui",
    "[unit][editor]")
{
    using namespace Horo::Editor;
    InspectorEditSession session;
    const SceneObject object = MakeCameraObject(SceneObjectId{11});
    REQUIRE((
        session.BeginObject(object, DocumentRevision{3}).command ==
        EditorWorkspaceViewCommand::None));

    session.Draft().name = "Gameplay Camera";
    const EditorWorkspaceViewCommandData rename =
        session.ApplyNameEdit(
            InspectorNameEdit{.committed = true}, object, true);
    REQUIRE((rename.command == EditorWorkspaceViewCommand::UpdateObjectName));
    REQUIRE((rename.objectPayload == object.id));
    REQUIRE((rename.stringPayload == "Gameplay Camera"));

    session.Draft().name.clear();
    REQUIRE((
        session.ApplyNameEdit(
            InspectorNameEdit{.committed = true}, object, true).command ==
        EditorWorkspaceViewCommand::None));
    static_cast<void>(session.ApplyNameEdit(
        InspectorNameEdit{.cancelled = true}, object, true));
    REQUIRE((session.Draft().name == object.name));
}

TEST_CASE(
    "Inspector edit session owns transform preview lifecycle",
    "[unit][editor]")
{
    using namespace Horo::Editor;
    InspectorEditSession session;
    const SceneObject object = MakeCameraObject(SceneObjectId{21});
    static_cast<void>(session.BeginObject(object, DocumentRevision{1}));

    session.Draft().position[0] = 4.0F;
    const EditorWorkspaceViewCommandData preview =
        session.ApplyTransformEdit(
            InspectorTransformEdit{.changed = true}, object, true);
    REQUIRE((
        preview.command ==
        EditorWorkspaceViewCommand::PreviewObjectTransform));
    REQUIRE((session.HasTransformPreview()));
    REQUIRE((preview.transformPayload->translation.x == 4.0F));

    const EditorWorkspaceViewCommandData cancelled =
        session.ApplyTransformEdit(
            InspectorTransformEdit{.cancelRequested = true}, object, true);
    REQUIRE((
        cancelled.command ==
        EditorWorkspaceViewCommand::CancelObjectTransformPreview));
    REQUIRE((!session.HasTransformPreview()));
    REQUIRE((session.Draft().position[0] == object.localTransform.translation.x));

    session.Draft().position[1] = 6.0F;
    const EditorWorkspaceViewCommandData committed =
        session.ApplyTransformEdit(
            InspectorTransformEdit{.committed = true}, object, true);
    REQUIRE((
        committed.command ==
        EditorWorkspaceViewCommand::CommitObjectTransform));
    REQUIRE((committed.transformPayload->translation.y == 6.0F));
}

TEST_CASE(
    "Inspector edit session validates Camera edits and selection reconciliation",
    "[unit][editor]")
{
    using namespace Horo::Editor;
    InspectorEditSession session;
    const SceneObject first = MakeCameraObject(SceneObjectId{31});
    const SceneObject second = MakeCameraObject(SceneObjectId{32});
    static_cast<void>(session.BeginObject(first, DocumentRevision{1}));

    session.Draft().camera->nearPlane = 0.5F;
    const EditorWorkspaceViewCommandData cameraCommand =
        session.ApplyCameraEdit(
            InspectorCameraEdit{.committed = true}, first, true);
    REQUIRE((
        cameraCommand.command ==
        EditorWorkspaceViewCommand::UpdateCameraComponent));
    REQUIRE((cameraCommand.cameraPayload->nearPlane == 0.5F));

    session.Draft().camera->farPlane = 0.25F;
    REQUIRE((!session.IsCameraValid()));
    REQUIRE((
        session.ApplyCameraEdit(
            InspectorCameraEdit{.committed = true}, first, true).command ==
        EditorWorkspaceViewCommand::None));

    session.Draft().position[0] = 2.0F;
    static_cast<void>(session.ApplyTransformEdit(
        InspectorTransformEdit{.changed = true}, first, true));
    const EditorWorkspaceViewCommandData selectionChange =
        session.BeginObject(second, DocumentRevision{1});
    REQUIRE((
        selectionChange.command ==
        EditorWorkspaceViewCommand::CancelObjectTransformPreview));
    REQUIRE((session.Draft().object == second.id));
    REQUIRE((session.IsCameraValid()));
}
