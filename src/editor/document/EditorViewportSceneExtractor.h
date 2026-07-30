#pragma once

/**
 * @file EditorViewportSceneExtractor.h
 * @brief Immutable conversion from runtime scene views to editor viewport render snapshots.
 */

#include "Horo/Foundation/Result.h"
#include "Horo/Runtime/Scene/PrimitiveMesh.h"
#include "Horo/Runtime/Scene/RuntimeScene.h"
#include "editor/document/SceneDocument.h"
#include "editor/renderer/EditorViewportScene.h"

#include <span>
#include <vector>

namespace Horo::Editor {
    /** @brief Owning viewport render data extracted from one activated runtime scene revision. */
    struct EditorViewportSceneSnapshot {
        DocumentRevision documentRevision;
        Runtime::SceneRuntimeId runtimeSceneId;
        EditorViewportCamera camera;
        std::vector<Runtime::PrimitiveMeshLease> meshLeases;
        std::vector<EditorViewportMeshResourceView> meshResources;
        std::vector<EditorViewportInstance> instances;
        std::vector<SceneObjectId> instanceObjects;
        std::vector<Render::RenderLight> lights;
        std::vector<SceneObjectId> lightObjects;

        [[nodiscard]] EditorViewportSceneView View() const noexcept;
    };

    /** @brief Composition-owned handoff of the active screen's latest immutable viewport snapshot. */
    class EditorViewportSceneState final {
    public:
        void Replace(EditorViewportSceneSnapshot snapshot);
        void Clear() noexcept;
        [[nodiscard]] EditorViewportSceneView View() const noexcept;

    private:
        EditorViewportSceneSnapshot m_snapshot{};
    };

    /** @brief Resolved object and parent world matrices used by editor manipulation tools. */
    struct SceneObjectWorldTransforms {
        Math::Mat4 localToWorld{Math::Mat4::Identity()};
        Math::Mat4 parentToWorld{Math::Mat4::Identity()};
    };

    /** @brief Resolves world and parent matrices from an immutable runtime scene view. */
    [[nodiscard]] Result<SceneObjectWorldTransforms> ResolveSceneObjectWorldTransforms(Runtime::RuntimeSceneView scene,
                                                                                       SceneObjectId object);

    /** @brief Extracts supported renderable instances from an immutable runtime scene view. */
    [[nodiscard]] Result<EditorViewportSceneSnapshot> ExtractEditorViewportScene(Runtime::RuntimeSceneView scene,
                                                                                 DocumentRevision documentRevision,
                                                                                 const EditorViewportCamera &camera,
                                                                                 Runtime::PrimitiveMeshCache &meshCache);

    /** @brief Applies editor-owned transient transform overlays without mutating runtime state. */
    [[nodiscard]] Result<void> ApplyEditorViewportTransformPreview(Runtime::RuntimeSceneView scene,
                                                                   std::span<const SceneObjectTransformPreview> previews,
                                                                   EditorViewportSceneSnapshot &snapshot);

    /**
     * @brief Applies or clears one editor-owned transient Light-component overlay.
     * @param scene Active immutable runtime scene.
     * @param preview Light override, or null to restore authored runtime values.
     * @param snapshot Owning viewport snapshot updated in place.
     * @return Success, or a typed extraction error without a document mutation.
     */
    [[nodiscard]] Result<void> ApplyEditorViewportLightPreview(Runtime::RuntimeSceneView scene, const SceneObjectLightPreview *preview,
                                                               EditorViewportSceneSnapshot &snapshot);
}  // namespace Horo::Editor
