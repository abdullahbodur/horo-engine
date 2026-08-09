#include "editor/document/EditorViewportSceneExtractor.h"

#include "EditorRenderExtractionErrors.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

namespace Horo::Editor {
    namespace {
        [[nodiscard]] Error ExtractionError(const ErrorCodeDescriptor &descriptor, std::string message) {
            return MakeError(descriptor, std::move(message));
        }

        [[nodiscard]] Result<Math::Mat4> ResolveWorld(const Runtime::RuntimeSceneView scene, const Runtime::EntityRef entity,
                                                      const std::span<const SceneObjectTransformPreview> previews,
                                                      const std::size_t remainingDepth) {
            if (remainingDepth == 0)
                return Result<Math::Mat4>::Failure(
                    ExtractionError(ViewportSceneErrors::HierarchyCycle, "Runtime scene hierarchy contains a cycle."));
            const Result<Runtime::RuntimeEntityView> value = scene.Get(entity);
            if (value.HasError())
                return Result<Math::Mat4>::Failure(value.ErrorValue());

            const Runtime::RuntimeEntityView &view = value.Value();
            const Math::Transform *local = view.localTransform;
            if (view.authoredObject) {
                const auto preview =
                    std::ranges::find(previews, view.authoredObject->value, [](const SceneObjectTransformPreview &candidate) {
                    return candidate.object.value;
                });
                if (preview != previews.end())
                    local = &preview->localTransform;
            }
            const Math::Mat4 localToParent = local->ToMatrix();
            if (!view.parent)
                return Result<Math::Mat4>::Success(localToParent);
            Result<Math::Mat4> parent = ResolveWorld(scene, *view.parent, previews, remainingDepth - 1);
            if (parent.HasError())
                return parent;
            return Result<Math::Mat4>::Success(Math::Multiply(parent.Value(), localToParent));
        }

        [[nodiscard]] Render::RenderLightKind ToRenderLightKind(const Runtime::LightKind kind) noexcept {
            switch (kind) {
                case Runtime::LightKind::Directional:
                    return Render::RenderLightKind::Directional;
                case Runtime::LightKind::Point:
                    return Render::RenderLightKind::Point;
                case Runtime::LightKind::Spot:
                    return Render::RenderLightKind::Spot;
            }
            return Render::RenderLightKind::Directional;
        }

        [[nodiscard]] Result<Render::RenderLight> ExtractLight(const Runtime::LightComponent &authored, const Math::Mat4 &localToWorld) {
            const Result<Math::Vec3> direction = Math::TryNormalize(Math::TransformDirection(localToWorld, {0.0F, 0.0F, -1.0F}));
            if (direction.HasError()) {
                return Result<Render::RenderLight>::Failure(
                    ExtractionError(ViewportSceneErrors::InvalidResult, "Light world direction is degenerate."));
            }
            Render::RenderLight light{
                .kind = ToRenderLightKind(authored.kind),
                .position = Math::TransformPoint(localToWorld, {}),
                .direction = direction.Value(),
                .color = authored.color,
                .intensity = authored.intensity,
                .range = authored.range,
                .innerConeCosine = std::cos(authored.innerConeRadians),
                .outerConeCosine = std::cos(authored.outerConeRadians),
            };
            if (!light.IsValid()) {
                return Result<Render::RenderLight>::Failure(
                    ExtractionError(ViewportSceneErrors::InvalidResult, "Extracted viewport light is invalid."));
            }
            return Result<Render::RenderLight>::Success(light);
        }
    }  // namespace

    /** @copydoc EditorViewportSceneSnapshot::View */
    EditorViewportSceneView EditorViewportSceneSnapshot::View() const noexcept {
        return EditorViewportSceneView{camera, meshResources, instances, lights};
    }

    /** @copydoc EditorViewportSceneState::Replace */
    void EditorViewportSceneState::Replace(EditorViewportSceneSnapshot snapshot) {
        m_snapshot = std::move(snapshot);
    }

    /** @copydoc EditorViewportSceneState::Clear */
    void EditorViewportSceneState::Clear() noexcept {
        m_snapshot = {};
    }

    /** @copydoc EditorViewportSceneState::View */
    EditorViewportSceneView EditorViewportSceneState::View() const noexcept {
        return m_snapshot.View();
    }

    /** @copydoc ResolveSceneObjectWorldTransforms */
    Result<SceneObjectWorldTransforms> ResolveSceneObjectWorldTransforms(const Runtime::RuntimeSceneView scene,
                                                                         const SceneObjectId object) {
        const std::optional<Runtime::EntityRef> entity = scene.Find(Runtime::SceneObjectId{object.value});
        if (!entity)
            return Result<SceneObjectWorldTransforms>::Failure(
                ExtractionError(ViewportSceneErrors::ObjectNotFound, "Runtime scene object does not exist."));
        const Result<Runtime::RuntimeEntityView> value = scene.Get(*entity);
        if (value.HasError())
            return Result<SceneObjectWorldTransforms>::Failure(value.ErrorValue());

        Math::Mat4 parentToWorld = Math::Mat4::Identity();
        if (value.Value().parent) {
            Result<Math::Mat4> parent = ResolveWorld(scene, *value.Value().parent, {}, scene.SlotCount() + 1);
            if (parent.HasError())
                return Result<SceneObjectWorldTransforms>::Failure(parent.ErrorValue());
            parentToWorld = parent.Value();
        }
        const Math::Mat4 localToWorld = Math::Multiply(parentToWorld, value.Value().localTransform->ToMatrix());
        return Result<SceneObjectWorldTransforms>::Success({localToWorld, parentToWorld});
    }

    /** @copydoc ExtractEditorViewportScene */
    Result<EditorViewportSceneSnapshot> ExtractEditorViewportScene(
        const Runtime::RuntimeSceneView scene, const DocumentRevision documentRevision, const EditorViewportCamera &camera,
        Runtime::PrimitiveMeshCache &meshCache, const SceneDocumentSnapshot *document, const EditorAssetMeshCache *assetMeshes,
        const bool respectEditorVisibility) {
        if (!scene.RuntimeId().IsValid())
            return Result<EditorViewportSceneSnapshot>::Failure(
                ExtractionError(ViewportSceneErrors::InvalidResult, "Runtime scene view is invalid."));
        if (!camera.IsValid())
            return Result<EditorViewportSceneSnapshot>::Failure(
                ExtractionError(ViewportSceneErrors::InvalidCamera, "Editor viewport camera is invalid."));

        EditorViewportSceneSnapshot extracted{.documentRevision = documentRevision, .runtimeSceneId = scene.RuntimeId(), .camera = camera};
        extracted.instances.reserve(scene.SlotCount());
        extracted.instanceObjects.reserve(scene.SlotCount());
        extracted.instancePickable.reserve(scene.SlotCount());
        extracted.lights.reserve(std::min(scene.SlotCount(), Render::MaximumForwardLights));
        extracted.lightObjects.reserve(std::min(scene.SlotCount(), Render::MaximumForwardLights));

        for (std::size_t slot = 0; slot < scene.SlotCount(); ++slot) {
            const std::optional<Runtime::RuntimeEntityView> entity = scene.EntityAt(slot);
            if (!entity)
                continue;
            std::optional<ResolvedSceneObjectEditorState> editorState;
            if (document != nullptr && entity->authoredObject.has_value())
                editorState = ResolveSceneObjectEditorState(document->objects, SceneObjectId{entity->authoredObject->value});
            if (respectEditorVisibility && editorState.has_value() && !editorState->effectivelyVisible)
                continue;
            if (entity->components->light.has_value() && extracted.lights.size() < Render::MaximumForwardLights) {
                if (!entity->authoredObject)
                    return Result<EditorViewportSceneSnapshot>::Failure(
                        ExtractionError(ViewportSceneErrors::InvalidObjectId, "Viewport light has no authored object identity."));
                const Result<Math::Mat4> lightWorld = ResolveWorld(scene, entity->entity, {}, scene.SlotCount() + 1);
                if (lightWorld.HasError())
                    return Result<EditorViewportSceneSnapshot>::Failure(lightWorld.ErrorValue());
                Result<Render::RenderLight> light = ExtractLight(*entity->components->light, lightWorld.Value());
                if (light.HasError())
                    return Result<EditorViewportSceneSnapshot>::Failure(light.ErrorValue());
                extracted.lights.push_back(light.Value());
                extracted.lightObjects.push_back(SceneObjectId{entity->authoredObject->value});
            }
            if (!*entity->primitiveMesh)
                continue;
            if (!entity->authoredObject)
                return Result<EditorViewportSceneSnapshot>::Failure(
                    ExtractionError(ViewportSceneErrors::InvalidObjectId,
                                    "Renderable editor runtime entity has no authored object identity."));

            Result<Runtime::PrimitiveMeshLease> acquired = meshCache.Acquire(**entity->primitiveMesh);
            if (acquired.HasError()) {
                Error error = acquired.ErrorValue();
                error.message = "Scene object " + std::to_string(entity->authoredObject->value) + ": " + error.message;
                return Result<EditorViewportSceneSnapshot>::Failure(std::move(error));
            }
            Runtime::PrimitiveMeshLease lease = std::move(acquired).Value();
            const Render::RenderMeshHandle handle{lease.Id(), 1};
            if (std::ranges::find(extracted.meshResources, handle, &EditorViewportMeshResourceView::handle) ==
                extracted.meshResources.end()) {
                const Render::MeshData &mesh = lease.Data();
                extracted.meshResources.emplace_back(handle, mesh.vertices, mesh.indices, mesh.localBounds);
                extracted.meshLeases.push_back(std::move(lease));
            }
            const auto resource = std::ranges::find(extracted.meshResources, handle, &EditorViewportMeshResourceView::handle);
            const Result<Math::Mat4> world = ResolveWorld(scene, entity->entity, {}, scene.SlotCount() + 1);
            if (world.HasError())
                return Result<EditorViewportSceneSnapshot>::Failure(world.ErrorValue());
            extracted.instances.push_back(
                EditorViewportInstance{handle, world.Value(), resource->localBounds, Render::CoreDefaultMaterial, {}});
            extracted.instanceObjects.push_back(SceneObjectId{entity->authoredObject->value});
            extracted.instancePickable.push_back(editorState.has_value() && editorState->effectivelyLocked ? 0U : 1U);
        }
        if (document != nullptr && assetMeshes != nullptr) {
            for (const SceneObjectSnapshot &object : document->objects) {
                if (!object.meshAsset.has_value())
                    continue;
                const std::optional<ResolvedSceneObjectEditorState> editorState =
                    ResolveSceneObjectEditorState(document->objects, object.id);
                if (respectEditorVisibility && editorState.has_value() && !editorState->effectivelyVisible)
                    continue;
                const std::optional<EditorAssetMeshView> assetMesh = assetMeshes->Find(*object.meshAsset);
                if (!assetMesh.has_value() || assetMesh->mesh == nullptr)
                    continue;
                const std::optional<Runtime::EntityRef> entity = scene.Find(Runtime::SceneObjectId{object.id.value});
                if (!entity.has_value())
                    return Result<EditorViewportSceneSnapshot>::Failure(
                        ExtractionError(ViewportSceneErrors::ObjectNotFound, "Imported mesh scene object does not exist."));
                if (std::ranges::find(extracted.meshResources, assetMesh->handle, &EditorViewportMeshResourceView::handle) ==
                    extracted.meshResources.end()) {
                    extracted.meshResources.emplace_back(assetMesh->handle, assetMesh->mesh->vertices, assetMesh->mesh->indices,
                                                         assetMesh->mesh->localBounds);
                }
                const Result<Math::Mat4> world = ResolveWorld(scene, *entity, {}, scene.SlotCount() + 1);
                if (world.HasError())
                    return Result<EditorViewportSceneSnapshot>::Failure(world.ErrorValue());
                extracted.instances.push_back(EditorViewportInstance{assetMesh->handle,
                                                                     world.Value(),
                                                                     assetMesh->mesh->localBounds,
                                                                     Render::CoreDefaultMaterial,
                                                                     {}});
                extracted.instanceObjects.push_back(object.id);
                extracted.instancePickable.push_back(editorState.has_value() && editorState->effectivelyLocked ? 0U : 1U);
            }
        }
        if (!extracted.View().IsValid())
            return Result<EditorViewportSceneSnapshot>::Failure(
                ExtractionError(ViewportSceneErrors::InvalidResult, "Extracted runtime viewport scene is invalid."));
        return Result<EditorViewportSceneSnapshot>::Success(std::move(extracted));
    }

    /** @copydoc ApplyEditorViewportTransformPreview */
    Result<void> ApplyEditorViewportTransformPreview(const Runtime::RuntimeSceneView scene,
                                                     const std::span<const SceneObjectTransformPreview> previews,
                                                     EditorViewportSceneSnapshot &snapshot) {
        if (snapshot.runtimeSceneId != scene.RuntimeId() || snapshot.instances.size() != snapshot.instanceObjects.size())
            return Result<void>::Failure(ExtractionError(ViewportSceneErrors::InstanceIdentityMismatch,
                                                         "Viewport snapshot does not match the active runtime scene."));
        for (std::size_t index = 0; index < previews.size(); ++index) {
            if (!scene.Find(Runtime::SceneObjectId{previews[index].object.value}))
                return Result<void>::Failure(
                    ExtractionError(ViewportSceneErrors::ObjectNotFound, "Preview runtime scene object does not exist."));
            if (std::ranges::find(previews.subspan(index + 1), previews[index].object, &SceneObjectTransformPreview::object) !=
                previews.subspan(index + 1).end())
                return Result<void>::Failure(
                    ExtractionError(ViewportSceneErrors::InvalidObjectId, "Preview object identities must be unique."));
        }

        for (std::size_t index = 0; index < snapshot.instanceObjects.size(); ++index) {
            const std::optional<Runtime::EntityRef> entity = scene.Find(Runtime::SceneObjectId{snapshot.instanceObjects[index].value});
            if (!entity)
                return Result<void>::Failure(
                    ExtractionError(ViewportSceneErrors::ObjectNotFound, "Viewport runtime object does not exist."));
            const Result<Math::Mat4> world = ResolveWorld(scene, *entity, previews, scene.SlotCount() + 1);
            if (world.HasError() || !Math::IsFinite(world.HasValue() ? world.Value() : Math::Mat4{}))
                return Result<void>::Failure(world.HasError()
                                                 ? world.ErrorValue()
                                                 : ExtractionError(ViewportSceneErrors::InvalidResult, "Preview transform is invalid."));
            snapshot.instances[index].localToWorld = world.Value();
        }
        if (snapshot.lights.size() != snapshot.lightObjects.size())
            return Result<void>::Failure(ExtractionError(ViewportSceneErrors::InstanceIdentityMismatch,
                                                         "Viewport light identities do not match the extracted lights."));
        for (std::size_t index = 0; index < snapshot.lightObjects.size(); ++index) {
            const std::optional<Runtime::EntityRef> entity = scene.Find(Runtime::SceneObjectId{snapshot.lightObjects[index].value});
            if (!entity)
                return Result<void>::Failure(ExtractionError(ViewportSceneErrors::ObjectNotFound, "Viewport light object does not exist."));
            const Result<Runtime::RuntimeEntityView> value = scene.Get(*entity);
            if (value.HasError())
                return Result<void>::Failure(value.ErrorValue());
            const Result<Math::Mat4> world = ResolveWorld(scene, *entity, previews, scene.SlotCount() + 1);
            if (world.HasError())
                return Result<void>::Failure(world.ErrorValue());
            Result<Render::RenderLight> light = ExtractLight(*value.Value().components->light, world.Value());
            if (light.HasError())
                return Result<void>::Failure(light.ErrorValue());
            snapshot.lights[index] = light.Value();
        }
        return Result<void>::Success();
    }

    /** @copydoc ApplyEditorViewportLightPreview */
    Result<void> ApplyEditorViewportLightPreview(const Runtime::RuntimeSceneView scene, const SceneObjectLightPreview *preview,
                                                 EditorViewportSceneSnapshot &snapshot) {
        if (snapshot.runtimeSceneId != scene.RuntimeId() || snapshot.lights.size() != snapshot.lightObjects.size()) {
            return Result<void>::Failure(ExtractionError(ViewportSceneErrors::InstanceIdentityMismatch,
                                                         "Viewport light snapshot does not match the active runtime scene."));
        }
        if (preview != nullptr && (!preview->object.IsValid() || !IsValidLightComponent(preview->light))) {
            return Result<void>::Failure(
                ExtractionError(ViewportSceneErrors::InvalidResult, "Viewport Light-component preview is invalid."));
        }

        bool previewApplied = preview == nullptr;
        for (std::size_t index = 0; index < snapshot.lightObjects.size(); ++index) {
            const SceneObjectId object = snapshot.lightObjects[index];
            const std::optional<Runtime::EntityRef> entity = scene.Find(Runtime::SceneObjectId{object.value});
            if (!entity) {
                return Result<void>::Failure(ExtractionError(ViewportSceneErrors::ObjectNotFound, "Viewport light object does not exist."));
            }
            const Result<Runtime::RuntimeEntityView> value = scene.Get(*entity);
            if (value.HasError())
                return Result<void>::Failure(value.ErrorValue());
            if (!value.Value().components->light.has_value()) {
                return Result<void>::Failure(
                    ExtractionError(ViewportSceneErrors::InvalidResult, "Viewport light object has no authored Light component."));
            }
            const Result<Math::Mat4> world = ResolveWorld(scene, *entity, {}, scene.SlotCount() + 1);
            if (world.HasError())
                return Result<void>::Failure(world.ErrorValue());

            const Runtime::LightComponent &lightComponent =
                preview != nullptr && preview->object == object ? preview->light : *value.Value().components->light;
            Result<Render::RenderLight> light = ExtractLight(lightComponent, world.Value());
            if (light.HasError())
                return Result<void>::Failure(light.ErrorValue());
            snapshot.lights[index] = light.Value();
            previewApplied = previewApplied || (preview != nullptr && preview->object == object);
        }
        if (!previewApplied) {
            return Result<void>::Failure(
                ExtractionError(ViewportSceneErrors::ObjectNotFound, "Preview Light object is not present in the viewport snapshot."));
        }
        return Result<void>::Success();
    }
}  // namespace Horo::Editor
