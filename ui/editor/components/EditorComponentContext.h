/** @file EditorComponentContext.h
 *  @brief Shared context passed to all editor UI components to provide access to core systems. */
#pragma once

#include <functional>
#include <string>
#include <vector>

namespace Horo {
    class Registry;
}

namespace Horo::Editor {
    class SceneDocument;
    class EditorSchema;
    class TransformGizmo;
    enum class GizmoMode;
    class AssetImportService;
    struct EditorWorkspaceDocument;

    /** @brief Aggregates all core editor pointers and callbacks needed by UI sub-components.
     *
     *  Passed by const reference to Draw methods so that each component can read the state
     *  it needs without holding independent back-pointers to the editor layer. */
    struct EditorComponentContext {
        // Core document
        SceneDocument* document = nullptr;               /**< Non-owning pointer to the active scene document. */
        const SceneDocument* lastSavedDocument = nullptr;/**< Non-owning pointer to the last persisted document snapshot; used for dirty detection. */
        const EditorSchema* schema = nullptr;            /**< Non-owning pointer to the component-type schema registry. */

        // Selection state
        std::vector<int>* selectedIndices = nullptr; /**< Pointer to the list of currently selected scene-object indices. */
        std::string* selectedAssetId = nullptr;      /**< Pointer to the identifier of the currently selected asset. */

        // Gizmo & transform
        TransformGizmo* gizmo = nullptr;             /**< Non-owning pointer to the active transform gizmo. */
        GizmoMode* currentGizmoMode = nullptr;       /**< Pointer to the current gizmo operation mode (translate/rotate/scale). */

        // Asset system
        AssetImportService* assetImportService = nullptr; /**< Non-owning pointer to the asset import service. */

        // Workspace
        EditorWorkspaceDocument* workspaceDocument = nullptr; /**< Non-owning pointer to the editor workspace layout document. */

        // Live scene
        Registry* liveRegistry = nullptr; /**< Non-owning pointer to the live ECS registry; null when not in play mode. */

        // Callbacks
        std::function<void(const SceneObject&)>* transformCallback = nullptr;                           /**< Pointer to the callback invoked when a scene object's transform is modified. */
        std::function<std::vector<std::string>()>* scriptBehaviorOptionsCallback = nullptr;             /**< Pointer to the callback that returns the list of available script-behavior class names. */

        // Flags
        bool* active = nullptr;   /**< Pointer to the editor-active flag; false while the engine is shutting down. */
        bool* playMode = nullptr; /**< Pointer to the play-mode flag; true while the scene is simulating. */
        bool* flyMode = nullptr;  /**< Pointer to the fly-cam mode flag. */
    };
}
