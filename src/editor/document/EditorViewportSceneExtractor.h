#pragma once

/**
 * @file EditorViewportSceneExtractor.h
 * @brief Immutable conversion from authored scene snapshots to editor viewport render snapshots.
 */

#include "Horo/Foundation/Result.h"
#include "editor/document/SceneDocument.h"
#include "editor/renderer/EditorViewportScene.h"

#include <vector>

namespace Horo::Editor
{
/** @brief Owning viewport render data extracted from one committed document revision. */
struct EditorViewportSceneSnapshot
{
    DocumentRevision documentRevision;
    EditorViewportCamera camera;
    std::vector<Runtime::PrimitiveMeshLease> meshLeases;
    std::vector<EditorViewportMeshResourceView> meshResources;
    std::vector<EditorViewportInstance> instances;
    std::vector<SceneObjectId> instanceObjects; /**< Stable identities aligned one-to-one with @ref instances. */

    /** @brief Returns a non-owning render view valid while this snapshot remains alive and unchanged. */
    [[nodiscard]] EditorViewportSceneView View() const noexcept;
};

/** @brief Composition-owned handoff of the active screen's latest immutable viewport snapshot. */
class EditorViewportSceneState final
{
  public:
    /** @brief Replaces the active render snapshot with @p snapshot. */
    void Replace(EditorViewportSceneSnapshot snapshot);

    /** @brief Clears all instances when no scene-producing screen is active. */
    void Clear() noexcept;

    /** @brief Returns the current non-owning render view. */
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

/**
 * @brief Resolves one scene object's world and parent matrices from an immutable document snapshot.
 * @param document Authoritative committed or explicit preview snapshot.
 * @param object Stable object identity to resolve.
 * @return Resolved transforms, or a typed hierarchy/identity error.
 */
[[nodiscard]] Result<SceneObjectWorldTransforms> ResolveSceneObjectWorldTransforms(
    const SceneDocumentSnapshot &document, SceneObjectId object);

/**
 * @brief Extracts world transforms and supported renderable instances from @p document.
 * @param document Immutable committed authoring snapshot.
 * @param camera Immutable editor camera used for this viewport snapshot.
 * @return Owning render snapshot, or a typed error for invalid hierarchy or unsupported renderable data.
 */
[[nodiscard]] Result<EditorViewportSceneSnapshot> ExtractEditorViewportScene(const SceneDocumentSnapshot &document,
                                                                             const EditorViewportCamera &camera,
                                                                             Runtime::PrimitiveMeshCache &meshCache);

/**
 * @brief Reprojects existing viewport instances from committed objects plus an optional transform overlay.
 * @param objects Borrowed immutable committed scene objects on the editor owner thread.
 * @param preview Optional transient local-transform override owned by viewport workspace state.
 * @param scene Existing owning render snapshot whose aligned instance transforms are updated in place.
 * @return Success without allocation, or a typed hierarchy/identity error leaving @p scene unchanged.
 */
[[nodiscard]] Result<void> ApplyEditorViewportTransformPreview(
    std::span<const SceneObjectSnapshot> objects, const std::optional<SceneObjectTransformPreview> &preview,
    EditorViewportSceneSnapshot &scene);
} // namespace Horo::Editor
