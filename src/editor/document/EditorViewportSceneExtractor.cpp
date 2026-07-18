#include "editor/document/EditorViewportSceneExtractor.h"
#include "EditorRenderExtractionErrors.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>

namespace Horo::Editor {
    namespace {

        enum class VisitState : std::uint8_t {
            Unvisited,
            Visiting,
            Complete,
        };

        [[nodiscard]] Error MakeExtractionError(const ErrorCodeDescriptor &descriptor, std::string message) {
            return MakeError(descriptor, std::move(message));
        }

        [[nodiscard]] Result<Math::Mat4> ResolveWorldTransform(const std::span<const SceneObjectSnapshot> objects,
                                                               const SceneObjectId object,
                                                               const std::optional<SceneObjectTransformPreview> &
                                                               preview,
                                                               const std::size_t remainingDepth) {
            if (remainingDepth == 0) {
                return Result<Math::Mat4>::Failure(
                    MakeExtractionError(ViewportSceneErrors::HierarchyCycle, "Scene object hierarchy contains a cycle."));
            }
            const auto found = std::ranges::find(objects, object, &SceneObjectSnapshot::id);
            if (found == objects.end()) {
                return Result<Math::Mat4>::Failure(
                    MakeExtractionError(ViewportSceneErrors::ObjectNotFound,
                                        "Scene object or one of its parents is missing."));
            }

            const Math::Transform &localTransform =
                    preview.has_value() && preview->object == object ? preview->localTransform : found->localTransform;
            const Math::Mat4 localToParent = localTransform.ToMatrix();
            if (!found->parent.has_value()) {
                return Result<Math::Mat4>::Success(localToParent);
            }
            Result<Math::Mat4> parentToWorld = ResolveWorldTransform(objects, *found->parent, preview,
                                                                     remainingDepth - 1);
            if (parentToWorld.HasError()) {
                return parentToWorld;
            }
            return Result<Math::Mat4>::Success(Math::Multiply(parentToWorld.Value(), localToParent));
        }
    } // namespace

    /** @copydoc EditorViewportSceneSnapshot::View */
    EditorViewportSceneView EditorViewportSceneSnapshot::View() const noexcept {
        return EditorViewportSceneView{camera, meshResources, instances};
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
    Result<SceneObjectWorldTransforms> ResolveSceneObjectWorldTransforms(const SceneDocumentSnapshot &document,
                                                                         const SceneObjectId object) {
        std::vector<const SceneObjectSnapshot *> chain;
        std::optional current = object;
        while (current.has_value()) {
            if (!current->IsValid() ||
                std::ranges::any_of(
                    chain, [current](const SceneObjectSnapshot *entry) { return entry->id == *current; })) {
                return Result<SceneObjectWorldTransforms>::Failure(
                    MakeExtractionError(ViewportSceneErrors::HierarchyCycle,
                                        "Scene snapshot hierarchy contains a cycle."));
            }
            const auto found = std::ranges::find(document.objects, *current, &SceneObjectSnapshot::id);
            if (found == document.objects.end()) {
                return Result<SceneObjectWorldTransforms>::Failure(MakeExtractionError(
                    ViewportSceneErrors::ObjectNotFound, "Scene object or one of its parents does not exist."));
            }
            chain.push_back(&*found);
            current = found->parent;
        }

        Math::Mat4 parentToWorld = Math::Mat4::Identity();
        for (auto entry = chain.rbegin(); entry != chain.rend(); ++entry) {
            if ((*entry)->id == object) {
                break;
            }
            parentToWorld = Math::Multiply(parentToWorld, (*entry)->localTransform.ToMatrix());
        }
        const Math::Mat4 localToWorld = Math::Multiply(parentToWorld, chain.front()->localTransform.ToMatrix());
        return Result<SceneObjectWorldTransforms>::Success({localToWorld, parentToWorld});
    }

    /** @copydoc ExtractEditorViewportScene */
    Result<EditorViewportSceneSnapshot> ExtractEditorViewportScene(const SceneDocumentSnapshot &document,
                                                                   const EditorViewportCamera &camera,
                                                                   Runtime::PrimitiveMeshCache &meshCache) {
        if (!camera.IsValid()) {
            return Result<EditorViewportSceneSnapshot>::Failure(
                MakeExtractionError(ViewportSceneErrors::InvalidCamera, "Editor viewport camera is invalid."));
        }

        std::unordered_map<std::uint64_t, std::size_t> objectIndices;
        objectIndices.reserve(document.objects.size());
        for (std::size_t index = 0; index < document.objects.size(); ++index) {
            const SceneObjectId id = document.objects[index].id;
            if (!id.IsValid() || !objectIndices.emplace(id.value, index).second) {
                return Result<EditorViewportSceneSnapshot>::Failure(
                    MakeExtractionError(ViewportSceneErrors::InvalidObjectId, "Scene snapshot contains an invalid ID."));
            }
        }

        std::vector<Math::Mat4> worldTransforms(document.objects.size());
        std::vector<VisitState> visits(document.objects.size(), VisitState::Unvisited);
        std::function<Result<void>(std::size_t)> resolveWorldTransform;
        resolveWorldTransform = [&](const std::size_t index) -> Result<void> {
            if (visits[index] == VisitState::Complete) {
                return Result<void>::Success();
            }
            if (visits[index] == VisitState::Visiting) {
                return Result<void>::Failure(
                    MakeExtractionError(ViewportSceneErrors::HierarchyCycle,
                                        "Scene snapshot hierarchy contains a cycle."));
            }

            visits[index] = VisitState::Visiting;
            const SceneObjectSnapshot &object = document.objects[index];
            const Math::Mat4 localTransform = object.localTransform.ToMatrix();
            if (!object.parent.has_value()) {
                worldTransforms[index] = localTransform;
            } else {
                const auto parent = objectIndices.find(object.parent->value);
                if (parent == objectIndices.end()) {
                    return Result<void>::Failure(MakeExtractionError(ViewportSceneErrors::ParentNotFound,
                                                                     "Scene snapshot references a missing parent object."));
                }
                const Result<void> parentResult = resolveWorldTransform(parent->second);
                if (parentResult.HasError()) {
                    return parentResult;
                }
                worldTransforms[index] = Math::Multiply(worldTransforms[parent->second], localTransform);
            }
            visits[index] = VisitState::Complete;
            return Result<void>::Success();
        };

        EditorViewportSceneSnapshot extracted{.documentRevision = document.revision, .camera = camera};
        extracted.instances.reserve(document.objects.size());
        for (std::size_t index = 0; index < document.objects.size(); ++index) {
            const Result<void> transformResult = resolveWorldTransform(index);
            if (transformResult.HasError()) {
                return Result<EditorViewportSceneSnapshot>::Failure(transformResult.ErrorValue());
            }
            const std::optional<PrimitiveMeshDescriptor> &primitive = document.objects[index].primitiveMesh;
            if (!primitive.has_value()) {
                continue;
            }
            Result<Runtime::PrimitiveMeshLease> acquired = meshCache.Acquire(*primitive);
            if (acquired.HasError()) {
                Error error = acquired.ErrorValue();
                error.message = "Scene object " + std::to_string(document.objects[index].id.value) + ": " + error.
                                message;
                return Result<EditorViewportSceneSnapshot>::Failure(std::move(error));
            }
            Runtime::PrimitiveMeshLease lease = std::move(acquired).Value();
            const Render::MeshResourceId resourceId = lease.Id();
            const Render::RenderMeshHandle resourceHandle{resourceId, 1};
            if (std::ranges::find(extracted.meshResources, resourceHandle, &EditorViewportMeshResourceView::handle) ==
                extracted.meshResources.end()) {
                const Render::MeshData &mesh = lease.Data();
                extracted.meshResources.emplace_back(
                    resourceHandle, mesh.vertices, mesh.indices, mesh.localBounds);
                extracted.meshLeases.push_back(std::move(lease));
            }
            const auto resource =
                    std::ranges::find(extracted.meshResources, resourceHandle, &EditorViewportMeshResourceView::handle);
            extracted.instances.push_back(
                EditorViewportInstance{
                    resourceHandle, worldTransforms[index], resource->localBounds,
                    Render::CoreDefaultMaterial, {}
                });
            extracted.instanceObjects.push_back(document.objects[index].id);
        }

        if (!extracted.View().IsValid()) {
            return Result<EditorViewportSceneSnapshot>::Failure(MakeExtractionError(
                ViewportSceneErrors::InvalidResult, "Extracted viewport scene contains invalid numeric values."));
        }
        return Result<EditorViewportSceneSnapshot>::Success(std::move(extracted));
    }

    /** @copydoc ApplyEditorViewportTransformPreview */
    Result<void> ApplyEditorViewportTransformPreview(const std::span<const SceneObjectSnapshot> objects,
                                                     const std::optional<SceneObjectTransformPreview> &preview,
                                                     EditorViewportSceneSnapshot &scene) {
        if (scene.instances.size() != scene.instanceObjects.size()) {
            return Result<void>::Failure(MakeExtractionError(ViewportSceneErrors::InstanceIdentityMismatch,
                                                             "Viewport instance identities are not aligned."));
        }
        if (preview.has_value() && std::ranges::find(objects, preview->object, &SceneObjectSnapshot::id) == objects.
            end()) {
            return Result<void>::Failure(
                MakeExtractionError(ViewportSceneErrors::ObjectNotFound, "Preview scene object does not exist."));
        }

        for (const SceneObjectId object: scene.instanceObjects) {
            const Result<Math::Mat4> transform = ResolveWorldTransform(objects, object, preview, objects.size() + 1);
            if (transform.HasError()) {
                return Result<void>::Failure(transform.ErrorValue());
            }
            if (!Math::IsFinite(transform.Value())) {
                return Result<void>::Failure(MakeExtractionError(
                    ViewportSceneErrors::InvalidResult, "Viewport transform preview produced invalid numeric values."));
            }
        }
        for (std::size_t index = 0; index < scene.instanceObjects.size(); ++index) {
            const Result<Math::Mat4> transform =
                    ResolveWorldTransform(objects, scene.instanceObjects[index], preview, objects.size() + 1);
            scene.instances[index].localToWorld = transform.Value();
        }
        if (!scene.View().IsValid()) {
            return Result<void>::Failure(MakeExtractionError(
                ViewportSceneErrors::InvalidResult, "Viewport transform preview produced invalid numeric values."));
        }
        return Result<void>::Success();
    }
} // namespace Horo::Editor
