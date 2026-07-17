#include "editor/document/EditorViewportPicking.h"

#include <cassert>

namespace
{
using namespace Horo;
using namespace Horo::Editor;

void CenterRayReturnsNearestStableObjectIdentity()
{
    Runtime::PrimitiveMeshCache meshCache;
    SceneDocument document;
    EditorHistory history;
    SceneDocumentCommandExecutor commands{document, history};
    const auto farObject = commands.Execute(CreateSceneObjectCommand{
        .name = "Far",
        .primitiveMesh = PrimitiveMeshDescriptor{},
    });
    const auto nearObject = commands.Execute(CreateSceneObjectCommand{
        .name = "Near",
        .localTransform = Math::Transform{.translation = {0.0F, 0.0F, 2.0F}},
        .primitiveMesh = PrimitiveMeshDescriptor{},
    });
    assert(farObject.HasValue() && nearObject.HasValue());
    const auto scene = ExtractEditorViewportScene(document.Snapshot(), {}, meshCache);
    assert(scene.HasValue());

    const auto picked = PickEditorViewportScene(
        scene.Value(), EditorViewportPickQuery{.normalizedX = 0.5F, .normalizedY = 0.5F, .aspect = 1.0F});
    assert(picked.HasValue());
    assert(picked.Value() == nearObject.Value().object);
}

void MissAndInvalidQueryRemainExplicitResults()
{
    Runtime::PrimitiveMeshCache meshCache;
    SceneDocument document;
    EditorHistory history;
    SceneDocumentCommandExecutor commands{document, history};
    assert(commands
               .Execute(CreateSceneObjectCommand{
                   .name = "Box",
                   .primitiveMesh = PrimitiveMeshDescriptor{},
               })
               .HasValue());
    const auto scene = ExtractEditorViewportScene(document.Snapshot(), {}, meshCache);
    assert(scene.HasValue());

    const auto missed = PickEditorViewportScene(
        scene.Value(), EditorViewportPickQuery{.normalizedX = 0.0F, .normalizedY = 0.0F, .aspect = 1.0F});
    assert(missed.HasValue());
    assert(!missed.Value().has_value());

    const auto invalid = PickEditorViewportScene(
        scene.Value(), EditorViewportPickQuery{.normalizedX = -0.1F, .normalizedY = 0.5F, .aspect = 1.0F});
    assert(invalid.HasError());
    assert(invalid.ErrorValue().code.Value() == "viewport_picking.invalid_query");
}

void IdentityMappingMismatchIsRejected()
{
    Runtime::PrimitiveMeshCache meshCache;
    auto acquired = meshCache.Acquire(Runtime::PrimitiveMeshDescriptor::Defaults(Runtime::PrimitiveMeshType::Box));
    assert(acquired.HasValue());
    Runtime::PrimitiveMeshLease lease = std::move(acquired).Value();
    const Render::MeshData &mesh = lease.Data();
    EditorViewportSceneSnapshot scene{
        .documentRevision = {},
        .camera = {},
        .meshLeases = {lease},
        .meshResources = {
            {Render::RenderMeshHandle{lease.Id(), 1}, mesh.vertices, mesh.indices, mesh.localBounds}},
        .instances = {EditorViewportInstance{Render::RenderMeshHandle{lease.Id(), 1}, Math::Mat4::Identity(),
                                              mesh.localBounds, Render::CoreDefaultMaterial, {}}},
    };
    const auto picked = PickEditorViewportScene(
        scene, EditorViewportPickQuery{.normalizedX = 0.5F, .normalizedY = 0.5F, .aspect = 1.0F});
    assert(picked.HasError());
    assert(picked.ErrorValue().code.Value() == "viewport_picking.invalid_scene");

    scene.instanceObjects = {SceneObjectId{}};
    const auto invalidIdentity = PickEditorViewportScene(
        scene, EditorViewportPickQuery{.normalizedX = 0.5F, .normalizedY = 0.5F, .aspect = 1.0F});
    assert(invalidIdentity.HasError());
    assert(invalidIdentity.ErrorValue().code.Value() == "viewport_picking.invalid_identity");
}

void OrthographicRayPicksTransformedBounds()
{
    Runtime::PrimitiveMeshCache meshCache;
    SceneDocument document;
    EditorHistory history;
    SceneDocumentCommandExecutor commands{document, history};
    const auto object = commands.Execute(CreateSceneObjectCommand{
        .name = "Scaled",
        .localTransform =
            Math::Transform{
                .translation = {1.0F, 0.0F, 0.0F},
                .rotation = Math::Quaternion::FromAxisAngle({0.0F, 1.0F, 0.0F}, Math::Pi * 0.25F),
                .scale = {-2.0F, 1.0F, 0.5F},
            },
        .primitiveMesh = PrimitiveMeshDescriptor{},
    });
    assert(object.HasValue());
    EditorViewportCamera camera;
    camera.projection = Runtime::CameraProjection::Orthographic;
    camera.orthographicHeight = 4.0F;
    const auto scene = ExtractEditorViewportScene(document.Snapshot(), camera, meshCache);
    assert(scene.HasValue());
    const auto picked = PickEditorViewportScene(
        scene.Value(), EditorViewportPickQuery{.normalizedX = 0.75F, .normalizedY = 0.5F, .aspect = 1.0F});
    assert(picked.HasValue() && picked.Value() == object.Value().object);
}
} // namespace

int main()
{
    CenterRayReturnsNearestStableObjectIdentity();
    MissAndInvalidQueryRemainExplicitResults();
    IdentityMappingMismatchIsRejected();
    OrthographicRayPicksTransformedBounds();
    return 0;
}
