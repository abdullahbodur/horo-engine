#include "Horo/Runtime/Scene/PrimitiveCatalog.h"
#include "editor/document/EditorViewportSceneExtractor.h"
#include "editor/document/RuntimeSceneConversion.h"
#include "editor/document/SceneDocument.h"

#include <array>
#include <cassert>
#include <cmath>
#include <limits>
#include <memory>

namespace
{
[[nodiscard]] std::unique_ptr<Horo::Runtime::RuntimeScene> MakeRuntimeScene(
    const Horo::Editor::SceneDocument &document)
{
    auto definition = Horo::Editor::ConvertSceneDocumentToRuntime(
        document.Snapshot(), Horo::Runtime::SceneDefinitionId{1});
    assert(definition.HasValue());
    auto scene = Horo::Runtime::RuntimeScene::Create(
        definition.Value(), Horo::Runtime::SceneRuntimeId{1});
    assert(scene.HasValue());
    return std::move(scene).Value();
}

[[nodiscard]] bool NearlyEqual(const float lhs, const float rhs) noexcept
{
    return std::fabs(lhs - rhs) < 0.0001F;
}

void CatalogOwnsStableCorePrimitiveIds()
{
    using namespace Horo::Runtime;
    const PrimitiveDescriptor *box = PrimitiveCatalog::Find("primitive.mesh.box");
    assert(box != nullptr);
    assert(box->id == PrimitiveId{"primitive.mesh.box"});
    assert(box->id.IsValid());
    assert(box->category == PrimitiveCategory::Mesh);
    assert(box->creationGroup == PrimitiveCreationGroup::Objects3D);
    assert(box->defaultObjectName == "Box");
    assert(!box->iconToken.empty());
    assert(box->displayName == "Cube / Box");
    assert(box->meshType == PrimitiveMeshType::Box);
    assert(box->defaultCollider == ColliderShapeType::Box);
    assert(box->isRenderable && box->isPhysicsSolidByDefault);
    assert(PrimitiveCatalog::Find(PrimitiveMeshType::Box) == box);
    assert(PrimitiveCatalog::All().size() == 18);
    assert(PrimitiveCatalog::Find("primitive.mesh.missing") == nullptr);
}

void CatalogCreationUseCaseCreatesEveryCoreHierarchyPrimitive()
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
        assert(created.HasValue());
        ++creatableCount;
        const SceneObjectSnapshot &object = document.Objects().back();
        assert(object.name == descriptor.defaultObjectName);
        assert(object.primitiveMesh.has_value() == descriptor.meshType.has_value());
        if (descriptor.sceneObjectType == SceneObjectPrimitiveType::Camera)
            assert(object.components.camera.has_value());
        if (descriptor.sceneObjectType == SceneObjectPrimitiveType::DirectionalLight)
            assert(object.components.light->kind == LightKind::Directional);
        if (descriptor.sceneObjectType == SceneObjectPrimitiveType::PointLight)
            assert(object.components.light->kind == LightKind::Point);
        if (descriptor.sceneObjectType == SceneObjectPrimitiveType::SpotLight)
            assert(object.components.light->kind == LightKind::Spot);
        if (descriptor.sceneObjectType == SceneObjectPrimitiveType::TriggerVolume)
            assert(object.components.triggerVolume.has_value());
        if (descriptor.sceneObjectType == SceneObjectPrimitiveType::AudioSource)
            assert(object.components.audioSource.has_value());
    }
    assert(creatableCount == 14);
    assert(document.Objects().size() == 14);

    const DocumentRevision revision = document.Revision();
    assert(create.Execute(PrimitiveCreationRequest{PrimitiveId{"primitive.collider.box"}, std::nullopt}).HasError());
    assert(create.Execute(PrimitiveCreationRequest{PrimitiveId{"primitive.missing"}, std::nullopt}).HasError());
    assert(document.Revision() == revision);
}

void CreationNamesAreUniquePerSiblingAndTypedComponentsSurviveHistory()
{
    using namespace Horo;
    using namespace Horo::Editor;
    using namespace Horo::Runtime;
    SceneDocument document;
    EditorHistory history;
    SceneDocumentCommandExecutor commands{document, history};
    CreateSceneObjectUseCase create{document, commands};

    const auto root = create.Execute({PrimitiveId{"primitive.object.empty"}, std::nullopt});
    assert(root.HasValue());
    const auto first = create.Execute({PrimitiveId{"primitive.object.camera"}, root.Value().object});
    const auto second = create.Execute({PrimitiveId{"primitive.object.camera"}, root.Value().object});
    const auto otherRoot = create.Execute({PrimitiveId{"primitive.object.camera"}, std::nullopt});
    assert(first.HasValue() && second.HasValue() && otherRoot.HasValue());
    assert(document.Objects()[1].name == "Camera");
    assert(document.Objects()[2].name == "Camera 2");
    assert(document.Objects()[3].name == "Camera");
    assert(document.Objects()[2].components.camera.has_value());

    assert(commands.Undo().HasValue());
    assert(commands.Redo().HasValue());
    assert(document.Objects().back().components.camera.has_value());
}

void FailedCommandsDoNotMutateOrAdvanceRevision()
{
    using namespace Horo::Editor;
    SceneDocument document;
    EditorHistory history;
    SceneDocumentCommandExecutor commands{document, history};
    CreateSceneObjectCommand invalid{.name = "Invalid"};
    invalid.localTransform.translation.x = std::numeric_limits<float>::quiet_NaN();

    const auto result = commands.Execute(invalid);
    assert(result.HasError());
    assert(document.Revision() == DocumentRevision{});
    assert(document.Snapshot().objects.empty());
    assert(!document.IsDirty());
}

void EulerConversionPreservesTheComposedRotation()
{
    using namespace Horo::Math;
    const Vec3 authored{0.31F, -0.47F, 0.22F};
    const Quaternion rotation = Quaternion::FromEulerRadians(authored);
    const Vec3 recovered = rotation.ToEulerRadians();
    assert(NearlyEqual(recovered.x, authored.x));
    assert(NearlyEqual(recovered.y, authored.y));
    assert(NearlyEqual(recovered.z, authored.z));

    const Mat4 originalMatrix = Transform{.rotation = rotation}.ToMatrix();
    const Mat4 recoveredMatrix = Transform{.rotation = Quaternion::FromEulerRadians(recovered)}.ToMatrix();
    for (std::size_t index = 0; index < originalMatrix.values.size(); ++index)
    {
        assert(NearlyEqual(originalMatrix.values[index], recoveredMatrix.values[index]));
    }
}

void AffineInverseRestoresTransformedPoints()
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
    assert(inverse.HasValue());
    const Vec3 point{0.7F, -1.2F, 2.4F};
    const Vec3 restored = TransformAffinePoint(inverse.Value(), TransformAffinePoint(matrix, point));
    assert(NearlyEqual(restored.x, point.x));
    assert(NearlyEqual(restored.y, point.y));
    assert(NearlyEqual(restored.z, point.z));
    assert(TryInverseAffine(Transform{.scale = {0.0F, 1.0F, 1.0F}}.ToMatrix()).HasError());
}

void CommandsCreateStableObjectsAndTrackSavedRevision()
{
    using namespace Horo::Editor;
    SceneDocument document;
    EditorHistory history;
    SceneDocumentCommandExecutor commands{document, history};
    const auto created = commands.Execute(CreateSceneObjectCommand{
        .name = "Box",
        .primitiveMesh = PrimitiveMeshDescriptor{},
    });
    assert(created.HasValue());
    assert(created.Value().object.IsValid());
    assert(document.Revision().value == 1);
    assert(document.IsDirty());
    assert(document.MarkSaved(document.Revision(), document.State()).HasValue());
    assert(!document.IsDirty());

    const auto renamed = commands.Execute(RenameSceneObjectCommand{created.Value().object, "Hero Box"});
    assert(renamed.HasValue());
    assert(document.Revision().value == 2);
    assert(document.IsDirty());
    assert(document.Snapshot().objects.front().name == "Hero Box");
}

void UnchangedTransformDoesNotCreateAHistoryEntry()
{
    using namespace Horo::Editor;
    SceneDocument document;
    EditorHistory history;
    SceneDocumentCommandExecutor commands{document, history};
    const auto created = commands.Execute(CreateSceneObjectCommand{.name = "Box"});
    assert(created.HasValue());
    history.Clear();
    const SceneDocumentSnapshot before = document.Snapshot();

    const auto unchanged =
        commands.Execute(SetSceneObjectTransformCommand{created.Value().object, before.objects.front().localTransform});
    assert(unchanged.HasValue());
    assert(!unchanged.Value().committed);
    assert(document.Revision() == before.revision);
    assert(document.State() == before.state);
    assert(!history.CanUndo());
}

void ExtractionResolvesHierarchyIntoWorldMatrices()
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
    assert(parent.HasValue());
    const auto child = commands.Execute(CreateSceneObjectCommand{
        .name = "Box",
        .parent = parent.Value().object,
        .localTransform = Math::Transform{.translation = {0.0F, 3.0F, 0.0F}},
        .primitiveMesh = PrimitiveMeshDescriptor{},
    });
    assert(child.HasValue());

    const auto runtimeScene = MakeRuntimeScene(document);
    const auto extracted = ExtractEditorViewportScene(
        runtimeScene->View(), document.Revision(), EditorViewportCamera{}, meshCache);
    assert(extracted.HasValue());
    assert(extracted.Value().documentRevision == document.Revision());
    assert(extracted.Value().instances.size() == 1);
    assert(extracted.Value().instanceObjects == std::vector{child.Value().object});
    const Math::Vec3 worldOrigin = Math::TransformPoint(extracted.Value().instances.front().localToWorld, {});
    assert(NearlyEqual(worldOrigin.x, 2.0F));
    assert(NearlyEqual(worldOrigin.y, 3.0F));
    assert(NearlyEqual(worldOrigin.z, 0.0F));
    const auto resolved = ResolveSceneObjectWorldTransforms(runtimeScene->View(), child.Value().object);
    assert(resolved.HasValue());
    const Math::Vec3 resolvedOrigin = Math::TransformPoint(resolved.Value().localToWorld, {});
    const Math::Vec3 resolvedParentOrigin = Math::TransformPoint(resolved.Value().parentToWorld, {});
    assert(NearlyEqual(resolvedOrigin.x, 2.0F) && NearlyEqual(resolvedOrigin.y, 3.0F));
    assert(NearlyEqual(resolvedParentOrigin.x, 2.0F) && NearlyEqual(resolvedParentOrigin.y, 0.0F));

    EditorViewportSceneSnapshot previewScene = extracted.Value();
    const SceneObjectTransformPreview preview{
        .object = parent.Value().object,
        .localTransform = Math::Transform{.translation = {5.0F, 0.0F, 0.0F}},
    };
    assert(ApplyEditorViewportTransformPreview(runtimeScene->View(), preview, previewScene).HasValue());
    const Math::Vec3 previewOrigin = Math::TransformPoint(previewScene.instances.front().localToWorld, {});
    assert(NearlyEqual(previewOrigin.x, 5.0F) && NearlyEqual(previewOrigin.y, 3.0F));
    assert(document.Revision() == extracted.Value().documentRevision);

    assert(ApplyEditorViewportTransformPreview(runtimeScene->View(), {}, previewScene).HasValue());
    const Math::Vec3 restoredOrigin = Math::TransformPoint(previewScene.instances.front().localToWorld, {});
    assert(NearlyEqual(restoredOrigin.x, 2.0F) && NearlyEqual(restoredOrigin.y, 3.0F));
}

void DeleteRemovesCompleteSubtree()
{
    using namespace Horo::Editor;
    SceneDocument document;
    EditorHistory history;
    SceneDocumentCommandExecutor commands{document, history};
    const auto parent = commands.Execute(CreateSceneObjectCommand{.name = "Parent"});
    assert(parent.HasValue());
    assert(commands.Execute(CreateSceneObjectCommand{.name = "Child", .parent = parent.Value().object}).HasValue());
    assert(commands.Execute(DeleteSceneObjectCommand{parent.Value().object}).HasValue());
    assert(document.Snapshot().objects.empty());
}

void UndoRedoPreserveMonotonicRevisionAndSavedStateIdentity()
{
    using namespace Horo::Editor;
    SceneDocument document;
    EditorHistory history;
    SceneDocumentCommandExecutor commands{document, history};
    const auto created = commands.Execute(CreateSceneObjectCommand{.name = "Box"});
    assert(created.HasValue());
    const DocumentStateId savedState = document.State();
    assert(document.MarkSaved(document.Revision(), savedState).HasValue());

    assert(commands.Execute(RenameSceneObjectCommand{created.Value().object, "Renamed"}).HasValue());
    const DocumentStateId renamedState = document.State();
    assert(document.Revision().value == 2);
    assert(document.IsDirty());
    assert(history.CanUndo() && !history.CanRedo());

    const auto undone = commands.Undo();
    assert(undone.HasValue() && undone.Value().kind == DocumentChangeKind::Undone);
    assert(document.Revision().value == 3);
    assert(document.State() == savedState);
    assert(!document.IsDirty());
    assert(document.Snapshot().objects.front().name == "Box");
    assert(history.CanRedo());

    const auto redone = commands.Redo();
    assert(redone.HasValue() && redone.Value().kind == DocumentChangeKind::Redone);
    assert(document.Revision().value == 4);
    assert(document.State() == renamedState);
    assert(document.IsDirty());
    assert(document.Snapshot().objects.front().name == "Renamed");
}

void UndoDeleteRestoresSubtreeAndNewEditClearsRedo()
{
    using namespace Horo::Editor;
    SceneDocument document;
    EditorHistory history;
    SceneDocumentCommandExecutor commands{document, history};
    const auto parent = commands.Execute(CreateSceneObjectCommand{.name = "Parent"});
    assert(parent.HasValue());
    const auto child = commands.Execute(CreateSceneObjectCommand{.name = "Child", .parent = parent.Value().object});
    assert(child.HasValue());
    assert(commands.Execute(DeleteSceneObjectCommand{parent.Value().object}).HasValue());
    assert(document.Snapshot().objects.empty());
    assert(commands.Undo().HasValue());
    const SceneDocumentSnapshot restored = document.Snapshot();
    assert(restored.objects.size() == 2);
    assert(restored.objects[0].id == parent.Value().object);
    assert(restored.objects[1].parent == parent.Value().object);

    assert(commands.Execute(RenameSceneObjectCommand{child.Value().object, "Edited Child"}).HasValue());
    assert(!history.CanRedo());
    const auto redo = commands.Redo();
    assert(redo.HasError());
    assert(redo.ErrorValue().code.Value() == "scene_document.nothing_to_redo");
}

void ExtractionUsesAllPrimitiveMeshesAndDeduplicatesDescriptors()
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
        assert(commands
                   .Execute(CreateSceneObjectCommand{
                       .name = "Primitive " + std::to_string(index),
                       .primitiveMesh = Runtime::PrimitiveMeshDescriptor::Defaults(types[index]),
                   })
                   .HasValue());
    }
    assert(commands
               .Execute(CreateSceneObjectCommand{
                   .name = "Second Box",
                   .primitiveMesh = Runtime::PrimitiveMeshDescriptor::Defaults(Runtime::PrimitiveMeshType::Box),
               })
               .HasValue());
    Runtime::PrimitiveMeshCache meshCache;
    const auto runtimeScene = MakeRuntimeScene(document);
    const auto extracted = ExtractEditorViewportScene(
        runtimeScene->View(), document.Revision(), {}, meshCache);
    assert(extracted.HasValue());
    assert(extracted.Value().instances.size() == 8);
    assert(extracted.Value().meshResources.size() == 7);
    assert(extracted.Value().meshLeases.size() == 7);
    assert(extracted.Value().instances.front().mesh == extracted.Value().instances.back().mesh);
    assert(extracted.Value().View().IsValid());
}

void PrimitiveParametersSurviveSnapshotDuplicateAndHistory()
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
    assert(created.HasValue());
    const auto duplicated = commands.Execute(DuplicateSceneObjectCommand{created.Value().object, "Sphere Copy"});
    assert(duplicated.HasValue());
    SceneDocumentSnapshot snapshot = document.Snapshot();
    assert(snapshot.objects.size() == 2);
    assert(snapshot.objects[0].primitiveMesh == sphere);
    assert(snapshot.objects[1].primitiveMesh == sphere);
    assert(commands.Undo().HasValue());
    assert(document.Snapshot().objects.size() == 1);
    assert(commands.Redo().HasValue());
    snapshot = document.Snapshot();
    assert(snapshot.objects.size() == 2 && snapshot.objects[1].primitiveMesh == sphere);
}
} // namespace

int main()
{
    CatalogOwnsStableCorePrimitiveIds();
    CatalogCreationUseCaseCreatesEveryCoreHierarchyPrimitive();
    CreationNamesAreUniquePerSiblingAndTypedComponentsSurviveHistory();
    FailedCommandsDoNotMutateOrAdvanceRevision();
    EulerConversionPreservesTheComposedRotation();
    AffineInverseRestoresTransformedPoints();
    CommandsCreateStableObjectsAndTrackSavedRevision();
    UnchangedTransformDoesNotCreateAHistoryEntry();
    ExtractionResolvesHierarchyIntoWorldMatrices();
    DeleteRemovesCompleteSubtree();
    UndoRedoPreserveMonotonicRevisionAndSavedStateIdentity();
    UndoDeleteRestoresSubtreeAndNewEditClearsRedo();
    ExtractionUsesAllPrimitiveMeshesAndDeduplicatesDescriptors();
    PrimitiveParametersSurviveSnapshotDuplicateAndHistory();
    return 0;
}
