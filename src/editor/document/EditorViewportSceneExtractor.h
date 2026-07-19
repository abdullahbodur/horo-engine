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

#include <vector>

namespace Horo::Editor
{
    /** @brief Owning viewport render data extracted from one activated runtime scene revision. */
    struct EditorViewportSceneSnapshot
    {
        DocumentRevision documentRevision;
        Runtime::SceneRuntimeId runtimeSceneId;
        EditorViewportCamera camera;
        std::vector<Runtime::PrimitiveMeshLease> meshLeases;
        std::vector<EditorViewportMeshResourceView> meshResources;
        std::vector<EditorViewportInstance> instances;
        std::vector<SceneObjectId> instanceObjects;

        [[nodiscard]] EditorViewportSceneView View() const noexcept;
    };

    /** @brief Composition-owned handoff of the active screen's latest immutable viewport snapshot. */
    class EditorViewportSceneState final
    {
    public:
        void Replace(EditorViewportSceneSnapshot snapshot);
        void Clear() noexcept;
        [[nodiscard]] EditorViewportSceneView View() const noexcept;

    private:
        EditorViewportSceneSnapshot m_snapshot{};
    };

    /** @brief Resolved object and parent world matrices used by editor manipulation tools. */
    struct SceneObjectWorldTransforms
    {
        Math::Mat4 localToWorld{Math::Mat4::Identity()};
        Math::Mat4 parentToWorld{Math::Mat4::Identity()};
    };

    /** @brief Resolves world and parent matrices from an immutable runtime scene view. */
    [[nodiscard]] Result<SceneObjectWorldTransforms> ResolveSceneObjectWorldTransforms(Runtime::RuntimeSceneView scene,
        SceneObjectId object);

    /** @brief Extracts supported renderable instances from an immutable runtime scene view. */
    [[nodiscard]] Result<EditorViewportSceneSnapshot> ExtractEditorViewportScene(Runtime::RuntimeSceneView scene,
        DocumentRevision documentRevision,
        const EditorViewportCamera& camera,
        Runtime::PrimitiveMeshCache& meshCache);

    /** @brief Applies one editor-owned transient transform overlay without mutating runtime state. */
    [[nodiscard]] Result<void> ApplyEditorViewportTransformPreview(
        Runtime::RuntimeSceneView scene, const std::optional<SceneObjectTransformPreview>& preview,
        EditorViewportSceneSnapshot& snapshot);
} // namespace Horo::Editor
