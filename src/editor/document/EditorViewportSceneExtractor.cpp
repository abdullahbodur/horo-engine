#include "editor/document/EditorViewportSceneExtractor.h"
#include "EditorRenderExtractionErrors.h"

#include <algorithm>
#include <string>
#include <utility>

namespace Horo::Editor
{
    namespace
    {
        [[nodiscard]] Error ExtractionError(const ErrorCodeDescriptor& descriptor, std::string message)
        {
            return MakeError(descriptor, std::move(message));
        }

        [[nodiscard]] Result<Math::Mat4> ResolveWorld(const Runtime::RuntimeSceneView scene,
                                                      const Runtime::EntityRef entity,
                                                      const std::optional<SceneObjectTransformPreview>& preview,
                                                      const std::size_t remainingDepth)
        {
            if (remainingDepth == 0)
                return Result<Math::Mat4>::Failure(
                    ExtractionError(ViewportSceneErrors::HierarchyCycle, "Runtime scene hierarchy contains a cycle."));
            const Result<Runtime::RuntimeEntityView> value = scene.Get(entity);
            if (value.HasError())
                return Result<Math::Mat4>::Failure(value.ErrorValue());

            const Runtime::RuntimeEntityView& view = value.Value();
            const Math::Transform* local = view.localTransform;
            if (preview && view.authoredObject && preview->object.value == view.authoredObject->value)
                local = &preview->localTransform;
            const Math::Mat4 localToParent = local->ToMatrix();
            if (!view.parent)
                return Result<Math::Mat4>::Success(localToParent);
            Result<Math::Mat4> parent = ResolveWorld(scene, *view.parent, preview, remainingDepth - 1);
            if (parent.HasError())
                return parent;
            return Result<Math::Mat4>::Success(Math::Multiply(parent.Value(), localToParent));
        }
    } // namespace

    /** @copydoc EditorViewportSceneSnapshot::View */
    EditorViewportSceneView EditorViewportSceneSnapshot::View() const noexcept
    {
        return EditorViewportSceneView{camera, meshResources, instances};
    }

    /** @copydoc EditorViewportSceneState::Replace */
    void EditorViewportSceneState::Replace(EditorViewportSceneSnapshot snapshot)
    {
        m_snapshot = std::move(snapshot);
    }

    /** @copydoc EditorViewportSceneState::Clear */
    void EditorViewportSceneState::Clear() noexcept
    {
        m_snapshot = {};
    }

    /** @copydoc EditorViewportSceneState::View */
    EditorViewportSceneView EditorViewportSceneState::View() const noexcept
    {
        return m_snapshot.View();
    }

    /** @copydoc ResolveSceneObjectWorldTransforms */
    Result<SceneObjectWorldTransforms> ResolveSceneObjectWorldTransforms(const Runtime::RuntimeSceneView scene,
                                                                         const SceneObjectId object)
    {
        const std::optional<Runtime::EntityRef> entity = scene.Find(Runtime::SceneObjectId{object.value});
        if (!entity)
            return Result<SceneObjectWorldTransforms>::Failure(
                ExtractionError(ViewportSceneErrors::ObjectNotFound, "Runtime scene object does not exist."));
        const Result<Runtime::RuntimeEntityView> value = scene.Get(*entity);
        if (value.HasError())
            return Result<SceneObjectWorldTransforms>::Failure(value.ErrorValue());

        Math::Mat4 parentToWorld = Math::Mat4::Identity();
        if (value.Value().parent)
        {
            Result<Math::Mat4> parent = ResolveWorld(scene, *value.Value().parent, {}, scene.SlotCount() + 1);
            if (parent.HasError())
                return Result<SceneObjectWorldTransforms>::Failure(parent.ErrorValue());
            parentToWorld = parent.Value();
        }
        const Math::Mat4 localToWorld = Math::Multiply(parentToWorld, value.Value().localTransform->ToMatrix());
        return Result<SceneObjectWorldTransforms>::Success({localToWorld, parentToWorld});
    }

    /** @copydoc ExtractEditorViewportScene */
    Result<EditorViewportSceneSnapshot> ExtractEditorViewportScene(const Runtime::RuntimeSceneView scene,
                                                                   const DocumentRevision documentRevision,
                                                                   const EditorViewportCamera& camera,
                                                                   Runtime::PrimitiveMeshCache& meshCache)
    {
        if (!scene.RuntimeId().IsValid())
            return Result<EditorViewportSceneSnapshot>::Failure(
                ExtractionError(ViewportSceneErrors::InvalidResult, "Runtime scene view is invalid."));
        if (!camera.IsValid())
            return Result<EditorViewportSceneSnapshot>::Failure(
                ExtractionError(ViewportSceneErrors::InvalidCamera, "Editor viewport camera is invalid."));

        EditorViewportSceneSnapshot extracted{
            .documentRevision = documentRevision, .runtimeSceneId = scene.RuntimeId(), .camera = camera
        };
        extracted.instances.reserve(scene.SlotCount());
        extracted.instanceObjects.reserve(scene.SlotCount());

        for (std::size_t slot = 0; slot < scene.SlotCount(); ++slot)
        {
            const std::optional<Runtime::RuntimeEntityView> entity = scene.EntityAt(slot);
            if (!entity || !*entity->primitiveMesh)
                continue;
            if (!entity->authoredObject)
                return Result<EditorViewportSceneSnapshot>::Failure(
                    ExtractionError(ViewportSceneErrors::InvalidObjectId,
                                    "Renderable editor runtime entity has no authored object identity."));

            Result<Runtime::PrimitiveMeshLease> acquired = meshCache.Acquire(**entity->primitiveMesh);
            if (acquired.HasError())
            {
                Error error = acquired.ErrorValue();
                error.message = "Scene object " + std::to_string(entity->authoredObject->value) + ": " + error.message;
                return Result<EditorViewportSceneSnapshot>::Failure(std::move(error));
            }
            Runtime::PrimitiveMeshLease lease = std::move(acquired).Value();
            const Render::RenderMeshHandle handle{lease.Id(), 1};
            if (std::ranges::find(extracted.meshResources, handle, &EditorViewportMeshResourceView::handle) ==
                extracted.meshResources.end())
            {
                const Render::MeshData& mesh = lease.Data();
                extracted.meshResources.emplace_back(handle, mesh.vertices, mesh.indices, mesh.localBounds);
                extracted.meshLeases.push_back(std::move(lease));
            }
            const auto resource =
                std::ranges::find(extracted.meshResources, handle, &EditorViewportMeshResourceView::handle);
            const Result<Math::Mat4> world = ResolveWorld(scene, entity->entity, {}, scene.SlotCount() + 1);
            if (world.HasError())
                return Result<EditorViewportSceneSnapshot>::Failure(world.ErrorValue());
            extracted.instances.push_back(
                EditorViewportInstance{handle, world.Value(), resource->localBounds, Render::CoreDefaultMaterial, {}});
            extracted.instanceObjects.push_back(SceneObjectId{entity->authoredObject->value});
        }
        if (!extracted.View().IsValid())
            return Result<EditorViewportSceneSnapshot>::Failure(
                ExtractionError(ViewportSceneErrors::InvalidResult, "Extracted runtime viewport scene is invalid."));
        return Result<EditorViewportSceneSnapshot>::Success(std::move(extracted));
    }

    /** @copydoc ApplyEditorViewportTransformPreview */
    Result<void> ApplyEditorViewportTransformPreview(const Runtime::RuntimeSceneView scene,
                                                     const std::optional<SceneObjectTransformPreview>& preview,
                                                     EditorViewportSceneSnapshot& snapshot)
    {
        if (snapshot.runtimeSceneId != scene.RuntimeId() || snapshot.instances.size() != snapshot.instanceObjects.
            size())
            return Result<void>::Failure(ExtractionError(ViewportSceneErrors::InstanceIdentityMismatch,
                                                         "Viewport snapshot does not match the active runtime scene."));
        if (preview && !scene.Find(Runtime::SceneObjectId{preview->object.value}))
            return Result<void>::Failure(
                ExtractionError(ViewportSceneErrors::ObjectNotFound, "Preview runtime scene object does not exist."));

        for (std::size_t index = 0; index < snapshot.instanceObjects.size(); ++index)
        {
            const std::optional<Runtime::EntityRef> entity =
                scene.Find(Runtime::SceneObjectId{snapshot.instanceObjects[index].value});
            if (!entity)
                return Result<void>::Failure(
                    ExtractionError(ViewportSceneErrors::ObjectNotFound, "Viewport runtime object does not exist."));
            const Result<Math::Mat4> world = ResolveWorld(scene, *entity, preview, scene.SlotCount() + 1);
            if (world.HasError() || !Math::IsFinite(world.HasValue() ? world.Value() : Math::Mat4{}))
                return Result<void>::Failure(world.HasError()
                                                 ? world.ErrorValue()
                                                 : ExtractionError(ViewportSceneErrors::InvalidResult,
                                                                   "Preview transform is invalid."));
            snapshot.instances[index].localToWorld = world.Value();
        }
        return Result<void>::Success();
    }
} // namespace Horo::Editor
