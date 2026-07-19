#include <catch2/catch_test_macros.hpp>

#include "Horo/Runtime/Scene/PrimitiveCatalog.h"
#include "editor/document/EditorViewportSceneExtractor.h"
#include "editor/document/RuntimeSceneConversion.h"
#include "editor/document/SceneDocument.h"

#include <array>
#include <cmath>
#include <limits>
#include <memory>

namespace
{
[[nodiscard]] std::unique_ptr<Horo::Runtime::RuntimeScene> MakeRuntimeScene(const Horo::Editor::SceneDocument &document)
{
    auto definition =
        Horo::Editor::ConvertSceneDocumentToRuntime(document.Snapshot(), Horo::Runtime::SceneDefinitionId{1});
    REQUIRE((definition.HasValue()));
    auto scene = Horo::Runtime::RuntimeScene::Create(definition.Value(), Horo::Runtime::SceneRuntimeId{1});
    REQUIRE((scene.HasValue()));
    return std::move(scene).Value();
}

[[nodiscard]] bool NearlyEqual(const float lhs, const float rhs) noexcept
{
    return std::fabs(lhs - rhs) < 0.0001F;
}

TEST_CASE("Catalog Owns Stable Core Primitive Ids", "[unit][editor]")
{
    using namespace Horo::Runtime;
    const PrimitiveDescriptor *box = PrimitiveCatalog::Find("primitive.mesh.box");
    REQUIRE((box != nullptr));
    REQUIRE((box->id == PrimitiveId{"primitive.mesh.box"}));
    REQUIRE((box->id.IsValid()));
    REQUIRE((box->category == PrimitiveCategory::Mesh));
    REQUIRE((box->creationGroup == PrimitiveCreationGroup::Objects3D));
    REQUIRE((box->defaultObjectName == "Box"));
    REQUIRE((!box->iconToken.empty()));
    REQUIRE((box->displayName == "Cube / Box"));
    REQUIRE((box->meshType == PrimitiveMeshType::Box));
    REQUIRE((box->defaultCollider == ColliderShapeType::Box));
    REQUIRE((box->isRenderable && box->isPhysicsSolidByDefault));
    REQUIRE((PrimitiveCatalog::Find(PrimitiveMeshType::Box) == box));
    REQUIRE((PrimitiveCatalog::All().size() == 18));
    REQUIRE((PrimitiveCatalog::Find("primitive.mesh.missing") == nullptr));
}

TEST_CASE("Catalog Creation Use Case Creates Every Core Hierarchy Primitive", "[unit][editor]")
{
    using namespace Horo;
    using namespace Horo::Editor;
    using namespace Horo::Runtime;
    SceneDocument document;
    EditorHistory history;
    SceneDocumentCommandExecutor commands{document, history};
    CreateSceneObjectUseCase create{document, commands};

    std::size_t creatableCount = 0;
    for (const PrimitiveDescriptor &descriptor : PrimitiveCatalog::All())
    {
        if (descriptor.creationGroup == PrimitiveCreationGroup::NotCreatable)
        {
            continue;
        }
        const auto created = create.Execute(PrimitiveCreationRequest{descriptor.id, std::nullopt});
        REQUIRE((created.HasValue()));
        ++creatableCount;
        const SceneObjectSnapshot &object = document.Objects().back();
        REQUIRE((object.name == descriptor.defaultObjectName));
        REQUIRE((object.primitiveMesh.has_value() == descriptor.meshType.has_value()));
        if (descriptor.sceneObjectType == SceneObjectPrimitiveType::Camera)
            REQUIRE((object.components.camera.has_value()));
        if (descriptor.sceneObjectType == SceneObjectPrimitiveType::DirectionalLight)
            REQUIRE((object.components.light->kind == LightKind::Directional));
        if (descriptor.sceneObjectType == SceneObjectPrimitiveType::PointLight)
            REQUIRE((object.components.light->kind == LightKind::Point));
        if (descriptor.sceneObjectType == SceneObjectPrimitiveType::SpotLight)
            REQUIRE((object.components.light->kind == LightKind::Spot));
        if (descriptor.sceneObjectType == SceneObjectPrimitiveType::TriggerVolume)
            REQUIRE((object.components.triggerVolume.has_value()));
        if (descriptor.sceneObjectType == SceneObjectPrimitiveType::AudioSource)
            REQUIRE((object.components.audioSource.has_value()));
    }
    REQUIRE((creatableCount == 14));
    REQUIRE((document.Objects().size() == 14));

    const DocumentRevision revision = document.Revision();
    REQUIRE((create.Execute(PrimitiveCreationRequest{PrimitiveId{"primitive.collider.box"}, std::nullopt}).HasError()));
    REQUIRE((create.Execute(PrimitiveCreationRequest{PrimitiveId{"primitive.missing"}, std::nullopt}).HasError()));
    REQUIRE((document.Revision() == revision));
}

TEST_CASE("Creation Names Are Unique Per Sibling And Typed Components Survive History", "[unit][editor]")
{
    using namespace Horo;
    using namespace Horo::Editor;
    using namespace Horo::Runtime;
    SceneDocument document;
    EditorHistory history;
    SceneDocumentCommandExecutor commands{document, history};
    CreateSceneObjectUseCase create{document, commands};

    const auto root = create.Execute({PrimitiveId{"primitive.object.empty"}, std::nullopt});
    REQUIRE((root.HasValue()));
    const auto first = create.Execute({PrimitiveId{"primitive.object.camera"}, root.Value().object});
    const auto second = create.Execute({PrimitiveId{"primitive.object.camera"}, root.Value().object});
    const auto otherRoot = create.Execute({PrimitiveId{"primitive.object.camera"}, std::nullopt});
    REQUIRE((first.HasValue() && second.HasValue() && otherRoot.HasValue()));
    REQUIRE((document.Objects()[1].name == "Camera"));
    REQUIRE((document.Objects()[2].name == "Camera 2"));
    REQUIRE((document.Objects()[3].name == "Camera"));
    REQUIRE((document.Objects()[2].components.camera.has_value()));

    REQUIRE((commands.Undo().HasValue()));
    REQUIRE((commands.Redo().HasValue()));
    REQUIRE((document.Objects().back().components.camera.has_value()));
}

TEST_CASE("Failed Commands Do Not Mutate Or Advance Revision", "[unit][editor]")
{
    using namespace Horo::Editor;
    SceneDocument document;
    EditorHistory history;
    SceneDocumentCommandExecutor commands{document, history};
    CreateSceneObjectCommand invalid{.name = "Invalid"};
    invalid.localTransform.translation.x = std::numeric_limits<float>::quiet_NaN();

    const auto result = commands.Execute(invalid);
    REQUIRE((result.HasError()));
    REQUIRE((document.Revision() == DocumentRevision{}));
    REQUIRE((document.Snapshot().objects.empty()));
    REQUIRE((!document.IsDirty()));
}

TEST_CASE("Euler Conversion Preserves The Composed Rotation", "[unit][editor]")
{
    using namespace Horo::Math;
    const Vec3 authored{0.31F, -0.47F, 0.22F};
    const Quaternion rotation = Quaternion::FromEulerRadians(authored);
    const Vec3 recovered = rotation.ToEulerRadians();
    REQUIRE((NearlyEqual(recovered.x, authored.x)));
    REQUIRE((NearlyEqual(recovered.y, authored.y)));
    REQUIRE((NearlyEqual(recovered.z, authored.z)));

    const Mat4 originalMatrix = Transform{.rotation = rotation}.ToMatrix();
    const Mat4 recoveredMatrix = Transform{.rotation = Quaternion::FromEulerRadians(recovered)}.ToMatrix();
    for (std::size_t index = 0; index < originalMatrix.values.size(); ++index)
    {
        REQUIRE((NearlyEqual(originalMatrix.values[index], recoveredMatrix.values[index])));
    }
}

TEST_CASE("Affine Inverse Restores Transformed Points", "[unit][editor]")
{
    using namespace Horo::Math;
    const Mat4 matrix =
        Transform{
            .translation = {2.0F, -3.0F, 4.0F},
            .rotation = Quaternion::FromEulerRadians({0.2F, -0.4F, 0.3F}),
            .scale = {2.0F, 1.5F, 0.5F},
        }
            .ToMatrix();
    const Horo::Result<Mat4> inverse = TryInverseAffine(matrix);
    REQUIRE((inverse.HasValue()));
    const Vec3 point{0.7F, -1.2F, 2.4F};
    const Vec3 restored = TransformAffinePoint(inverse.Value(), TransformAffinePoint(matrix, point));
    REQUIRE((NearlyEqual(restored.x, point.x)));
    REQUIRE((NearlyEqual(restored.y, point.y)));
    REQUIRE((NearlyEqual(restored.z, point.z)));
    REQUIRE((TryInverseAffine(Transform{.scale = {0.0F, 1.0F, 1.0F}}.ToMatrix()).HasError()));
}

TEST_CASE("Commands Create Stable Objects And Track Saved Revision", "[unit][editor]")
{
    using namespace Horo::Editor;
    SceneDocument document;
    EditorHistory history;
    SceneDocumentCommandExecutor commands{document, history};
    const auto created = commands.Execute(CreateSceneObjectCommand{
        .name = "Box",
        .primitiveMesh = PrimitiveMeshDescriptor{},
    });
    REQUIRE((created.HasValue()));
    REQUIRE((created.Value().object.IsValid()));
    REQUIRE((document.Revision().value == 1));
    REQUIRE((document.IsDirty()));
    REQUIRE((document.MarkSaved(document.Revision(), document.State()).HasValue()));
    REQUIRE((!document.IsDirty()));

    const auto renamed = commands.Execute(RenameSceneObjectCommand{created.Value().object, "Hero Box"});
    REQUIRE((renamed.HasValue()));
    REQUIRE((document.Revision().value == 2));
    REQUIRE((document.IsDirty()));
    REQUIRE((document.Snapshot().objects.front().name == "Hero Box"));
}

TEST_CASE("Unchanged Transform Does Not Create A History Entry", "[unit][editor]")
{
    using namespace Horo::Editor;
    SceneDocument document;
    EditorHistory history;
    SceneDocumentCommandExecutor commands{document, history};
    const auto created = commands.Execute(CreateSceneObjectCommand{.name = "Box"});
    REQUIRE((created.HasValue()));
    history.Clear();
    const SceneDocumentSnapshot before = document.Snapshot();

    const auto unchanged =
        commands.Execute(SetSceneObjectTransformCommand{created.Value().object, before.objects.front().localTransform});
    REQUIRE((unchanged.HasValue()));
    REQUIRE((!unchanged.Value().committed));
    REQUIRE((document.Revision() == before.revision));
    REQUIRE((document.State() == before.state));
    REQUIRE((!history.CanUndo()));
}

TEST_CASE("Extraction Resolves Hierarchy Into World Matrices", "[unit][editor]")
{
    using namespace Horo;
    using namespace Horo::Editor;
    SceneDocument document;
    EditorHistory history;
    SceneDocumentCommandExecutor commands{document, history};
    Runtime::PrimitiveMeshCache meshCache;
    const auto parent = commands.Execute(CreateSceneObjectCommand{
        .name = "Parent",
        .localTransform = Math::Transform{.translation = {2.0F, 0.0F, 0.0F}},
    });
    REQUIRE((parent.HasValue()));
    const auto child = commands.Execute(CreateSceneObjectCommand{
        .name = "Box",
        .parent = parent.Value().object,
        .localTransform = Math::Transform{.translation = {0.0F, 3.0F, 0.0F}},
        .primitiveMesh = PrimitiveMeshDescriptor{},
    });
    REQUIRE((child.HasValue()));

    const auto runtimeScene = MakeRuntimeScene(document);
    const auto extracted =
        ExtractEditorViewportScene(runtimeScene->View(), document.Revision(), EditorViewportCamera{}, meshCache);
    REQUIRE((extracted.HasValue()));
    REQUIRE((extracted.Value().documentRevision == document.Revision()));
    REQUIRE((extracted.Value().instances.size() == 1));
    REQUIRE((extracted.Value().instanceObjects == std::vector{child.Value().object}));
    const Math::Vec3 worldOrigin = Math::TransformPoint(extracted.Value().instances.front().localToWorld, {});
    REQUIRE((NearlyEqual(worldOrigin.x, 2.0F)));
    REQUIRE((NearlyEqual(worldOrigin.y, 3.0F)));
    REQUIRE((NearlyEqual(worldOrigin.z, 0.0F)));
    const auto resolved = ResolveSceneObjectWorldTransforms(runtimeScene->View(), child.Value().object);
    REQUIRE((resolved.HasValue()));
    const Math::Vec3 resolvedOrigin = Math::TransformPoint(resolved.Value().localToWorld, {});
    const Math::Vec3 resolvedParentOrigin = Math::TransformPoint(resolved.Value().parentToWorld, {});
    REQUIRE((NearlyEqual(resolvedOrigin.x, 2.0F) && NearlyEqual(resolvedOrigin.y, 3.0F)));
    REQUIRE((NearlyEqual(resolvedParentOrigin.x, 2.0F) && NearlyEqual(resolvedParentOrigin.y, 0.0F)));

    EditorViewportSceneSnapshot previewScene = extracted.Value();
    const SceneObjectTransformPreview preview{
        .object = parent.Value().object,
        .localTransform = Math::Transform{.translation = {5.0F, 0.0F, 0.0F}},
    };
    REQUIRE((ApplyEditorViewportTransformPreview(runtimeScene->View(), preview, previewScene).HasValue()));
    const Math::Vec3 previewOrigin = Math::TransformPoint(previewScene.instances.front().localToWorld, {});
    REQUIRE((NearlyEqual(previewOrigin.x, 5.0F) && NearlyEqual(previewOrigin.y, 3.0F)));
    REQUIRE((document.Revision() == extracted.Value().documentRevision));

    REQUIRE((ApplyEditorViewportTransformPreview(runtimeScene->View(), {}, previewScene).HasValue()));
    const Math::Vec3 restoredOrigin = Math::TransformPoint(previewScene.instances.front().localToWorld, {});
    REQUIRE((NearlyEqual(restoredOrigin.x, 2.0F) && NearlyEqual(restoredOrigin.y, 3.0F)));
}

TEST_CASE("Delete Removes Complete Subtree", "[unit][editor]")
{
    using namespace Horo::Editor;
    SceneDocument document;
    EditorHistory history;
    SceneDocumentCommandExecutor commands{document, history};
    const auto parent = commands.Execute(CreateSceneObjectCommand{.name = "Parent"});
    REQUIRE((parent.HasValue()));
    REQUIRE((commands.Execute(CreateSceneObjectCommand{.name = "Child", .parent = parent.Value().object}).HasValue()));
    REQUIRE((commands.Execute(DeleteSceneObjectCommand{parent.Value().object}).HasValue()));
    REQUIRE((document.Snapshot().objects.empty()));
}

TEST_CASE("Undo Redo Preserve Monotonic Revision And Saved State Identity", "[unit][editor]")
{
    using namespace Horo::Editor;
    SceneDocument document;
    EditorHistory history;
    SceneDocumentCommandExecutor commands{document, history};
    const auto created = commands.Execute(CreateSceneObjectCommand{.name = "Box"});
    REQUIRE((created.HasValue()));
    const DocumentStateId savedState = document.State();
    REQUIRE((document.MarkSaved(document.Revision(), savedState).HasValue()));

    REQUIRE((commands.Execute(RenameSceneObjectCommand{created.Value().object, "Renamed"}).HasValue()));
    const DocumentStateId renamedState = document.State();
    REQUIRE((document.Revision().value == 2));
    REQUIRE((document.IsDirty()));
    REQUIRE((history.CanUndo() && !history.CanRedo()));

    const auto undone = commands.Undo();
    REQUIRE((undone.HasValue() && undone.Value().kind == DocumentChangeKind::Undone));
    REQUIRE((document.Revision().value == 3));
    REQUIRE((document.State() == savedState));
    REQUIRE((!document.IsDirty()));
    REQUIRE((document.Snapshot().objects.front().name == "Box"));
    REQUIRE((history.CanRedo()));

    const auto redone = commands.Redo();
    REQUIRE((redone.HasValue() && redone.Value().kind == DocumentChangeKind::Redone));
    REQUIRE((document.Revision().value == 4));
    REQUIRE((document.State() == renamedState));
    REQUIRE((document.IsDirty()));
    REQUIRE((document.Snapshot().objects.front().name == "Renamed"));
}

TEST_CASE("Undo Delete Restores Subtree And New Edit Clears Redo", "[unit][editor]")
{
    using namespace Horo::Editor;
    SceneDocument document;
    EditorHistory history;
    SceneDocumentCommandExecutor commands{document, history};
    const auto parent = commands.Execute(CreateSceneObjectCommand{.name = "Parent"});
    REQUIRE((parent.HasValue()));
    const auto child = commands.Execute(CreateSceneObjectCommand{.name = "Child", .parent = parent.Value().object});
    REQUIRE((child.HasValue()));
    REQUIRE((commands.Execute(DeleteSceneObjectCommand{parent.Value().object}).HasValue()));
    REQUIRE((document.Snapshot().objects.empty()));
    REQUIRE((commands.Undo().HasValue()));
    const SceneDocumentSnapshot restored = document.Snapshot();
    REQUIRE((restored.objects.size() == 2));
    REQUIRE((restored.objects[0].id == parent.Value().object));
    REQUIRE((restored.objects[1].parent == parent.Value().object));

    REQUIRE((commands.Execute(RenameSceneObjectCommand{child.Value().object, "Edited Child"}).HasValue()));
    REQUIRE((!history.CanRedo()));
    const auto redo = commands.Redo();
    REQUIRE((redo.HasError()));
    REQUIRE((redo.ErrorValue().code.Value() == "scene_document.nothing_to_redo"));
}

TEST_CASE("Extraction Uses All Primitive Meshes And Deduplicates Descriptors", "[unit][editor]")
{
    using namespace Horo;
    using namespace Horo::Editor;
    constexpr std::array types{Runtime::PrimitiveMeshType::Box,     Runtime::PrimitiveMeshType::Sphere,
                               Runtime::PrimitiveMeshType::Capsule, Runtime::PrimitiveMeshType::Cylinder,
                               Runtime::PrimitiveMeshType::Cone,    Runtime::PrimitiveMeshType::Plane,
                               Runtime::PrimitiveMeshType::Quad};
    SceneDocument document;
    EditorHistory history;
    SceneDocumentCommandExecutor commands{document, history};
    for (std::size_t index = 0; index < types.size(); ++index)
    {
        REQUIRE((commands
                     .Execute(CreateSceneObjectCommand{
                         .name = "Primitive " + std::to_string(index),
                         .primitiveMesh = Runtime::PrimitiveMeshDescriptor::Defaults(types[index]),
                     })
                     .HasValue()));
    }
    REQUIRE((commands
                 .Execute(CreateSceneObjectCommand{
                     .name = "Second Box",
                     .primitiveMesh = Runtime::PrimitiveMeshDescriptor::Defaults(Runtime::PrimitiveMeshType::Box),
                 })
                 .HasValue()));
    Runtime::PrimitiveMeshCache meshCache;
    const auto runtimeScene = MakeRuntimeScene(document);
    const auto extracted = ExtractEditorViewportScene(runtimeScene->View(), document.Revision(), {}, meshCache);
    REQUIRE((extracted.HasValue()));
    REQUIRE((extracted.Value().instances.size() == 8));
    REQUIRE((extracted.Value().meshResources.size() == 7));
    REQUIRE((extracted.Value().meshLeases.size() == 7));
    REQUIRE((extracted.Value().instances.front().mesh == extracted.Value().instances.back().mesh));
    REQUIRE((extracted.Value().View().IsValid()));
}

TEST_CASE("Primitive Parameters Survive Snapshot Duplicate And History", "[unit][editor]")
{
    using namespace Horo;
    using namespace Horo::Editor;
    SceneDocument document;
    EditorHistory history;
    SceneDocumentCommandExecutor commands{document, history};
    Runtime::PrimitiveMeshDescriptor sphere =
        Runtime::PrimitiveMeshDescriptor::Defaults(Runtime::PrimitiveMeshType::Sphere);
    sphere.parameters = Runtime::SphereMeshParameters{.radius = 1.25F, .slices = 12, .stacks = 6};
    const auto created = commands.Execute(CreateSceneObjectCommand{.name = "Sphere", .primitiveMesh = sphere});
    REQUIRE((created.HasValue()));
    const auto duplicated = commands.Execute(DuplicateSceneObjectCommand{created.Value().object, "Sphere Copy"});
    REQUIRE((duplicated.HasValue()));
    SceneDocumentSnapshot snapshot = document.Snapshot();
    REQUIRE((snapshot.objects.size() == 2));
    REQUIRE((snapshot.objects[0].primitiveMesh == sphere));
    REQUIRE((snapshot.objects[1].primitiveMesh == sphere));
    REQUIRE((commands.Undo().HasValue()));
    REQUIRE((document.Snapshot().objects.size() == 1));
    REQUIRE((commands.Redo().HasValue()));
    snapshot = document.Snapshot();
    REQUIRE((snapshot.objects.size() == 2 && snapshot.objects[1].primitiveMesh == sphere));
}
} // namespace
