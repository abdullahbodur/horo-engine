#include <catch2/catch_test_macros.hpp>

#include "editor/document/EditorViewportPicking.h"
#include "editor/document/RuntimeSceneConversion.h"

#include <memory>

namespace
{
using namespace Horo;
using namespace Horo::Editor;

std::unique_ptr<Runtime::RuntimeScene> MakeRuntimeScene(const SceneDocument &document,
                                                        Runtime::SceneRuntimeId runtimeId = {1})
{
    auto definition = ConvertSceneDocumentToRuntime(document.Snapshot(), Runtime::SceneDefinitionId{1});
    REQUIRE((definition.HasValue()));
    auto scene = Runtime::RuntimeScene::Create(definition.Value(), runtimeId);
    REQUIRE((scene.HasValue()));
    return std::move(scene).Value();
}

TEST_CASE("Center Ray Returns Nearest Stable Object Identity", "[unit][editor]")
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
    REQUIRE((farObject.HasValue() && nearObject.HasValue()));
    const auto runtimeScene = MakeRuntimeScene(document);
    const auto scene = ExtractEditorViewportScene(runtimeScene->View(), document.Revision(), {}, meshCache);
    REQUIRE((scene.HasValue()));

    const auto picked = PickEditorViewportScene(
        scene.Value(), EditorViewportPickQuery{.normalizedX = 0.5F, .normalizedY = 0.5F, .aspect = 1.0F});
    REQUIRE((picked.HasValue()));
    REQUIRE((picked.Value().runtimeScene == Runtime::SceneRuntimeId{1}));
    REQUIRE((picked.Value().object == nearObject.Value().object));
}

TEST_CASE("Miss And Invalid Query Remain Explicit Results", "[unit][editor]")
{
    Runtime::PrimitiveMeshCache meshCache;
    SceneDocument document;
    EditorHistory history;
    SceneDocumentCommandExecutor commands{document, history};
    REQUIRE((commands
                 .Execute(CreateSceneObjectCommand{
                     .name = "Box",
                     .primitiveMesh = PrimitiveMeshDescriptor{},
                 })
                 .HasValue()));
    const auto runtimeScene = MakeRuntimeScene(document);
    const auto scene = ExtractEditorViewportScene(runtimeScene->View(), document.Revision(), {}, meshCache);
    REQUIRE((scene.HasValue()));

    const auto missed = PickEditorViewportScene(
        scene.Value(), EditorViewportPickQuery{.normalizedX = 0.0F, .normalizedY = 0.0F, .aspect = 1.0F});
    REQUIRE((missed.HasValue()));
    REQUIRE((!missed.Value().object.has_value()));

    const auto invalid = PickEditorViewportScene(
        scene.Value(), EditorViewportPickQuery{.normalizedX = -0.1F, .normalizedY = 0.5F, .aspect = 1.0F});
    REQUIRE((invalid.HasError()));
    REQUIRE((invalid.ErrorValue().code.Value() == "viewport_picking.invalid_query"));
}

TEST_CASE("Identity Mapping Mismatch Is Rejected", "[unit][editor]")
{
    Runtime::PrimitiveMeshCache meshCache;
    auto acquired = meshCache.Acquire(Runtime::PrimitiveMeshDescriptor::Defaults(Runtime::PrimitiveMeshType::Box));
    REQUIRE((acquired.HasValue()));
    Runtime::PrimitiveMeshLease lease = std::move(acquired).Value();
    const Render::MeshData &mesh = lease.Data();
    EditorViewportSceneSnapshot scene{
        .documentRevision = {},
        .runtimeSceneId = Runtime::SceneRuntimeId{8},
        .camera = {},
        .meshLeases = {lease},
        .meshResources = {{Render::RenderMeshHandle{lease.Id(), 1}, mesh.vertices, mesh.indices, mesh.localBounds}},
        .instances = {EditorViewportInstance{Render::RenderMeshHandle{lease.Id(), 1},
                                             Math::Mat4::Identity(),
                                             mesh.localBounds,
                                             Render::CoreDefaultMaterial,
                                             {}}},
    };
    const auto picked = PickEditorViewportScene(
        scene, EditorViewportPickQuery{.normalizedX = 0.5F, .normalizedY = 0.5F, .aspect = 1.0F});
    REQUIRE((picked.HasError()));
    REQUIRE((picked.ErrorValue().code.Value() == "viewport_picking.invalid_scene"));

    scene.instanceObjects = {SceneObjectId{}};
    const auto invalidIdentity = PickEditorViewportScene(
        scene, EditorViewportPickQuery{.normalizedX = 0.5F, .normalizedY = 0.5F, .aspect = 1.0F});
    REQUIRE((invalidIdentity.HasError()));
    REQUIRE((invalidIdentity.ErrorValue().code.Value() == "viewport_picking.invalid_identity"));
}

TEST_CASE("Orthographic Ray Picks Transformed Bounds", "[unit][editor]")
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
    REQUIRE((object.HasValue()));
    EditorViewportCamera camera;
    camera.projection = Runtime::CameraProjection::Orthographic;
    camera.orthographicHeight = 4.0F;
    const auto runtimeScene = MakeRuntimeScene(document);
    const auto scene = ExtractEditorViewportScene(runtimeScene->View(), document.Revision(), camera, meshCache);
    REQUIRE((scene.HasValue()));
    const auto picked = PickEditorViewportScene(
        scene.Value(), EditorViewportPickQuery{.normalizedX = 0.75F, .normalizedY = 0.5F, .aspect = 1.0F});
    REQUIRE((picked.HasValue() && picked.Value().object == object.Value().object));
}
} // namespace
