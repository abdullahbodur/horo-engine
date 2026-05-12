/** @file EditorToolbar.h
 *  @brief Menu bar and icon toolbar for the editor, with callback and state contracts. */
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "ui/editor/SceneDocument.h"

struct GLFWwindow;

namespace Horo::Editor {
    struct EditorComponentContext;

    /** @brief Callback table that EditorToolbar uses to trigger editor actions.
     *
     *  Each field is an optional callable; the toolbar checks validity before invoking. */
    struct EditorToolbarCallbacks {
        // Scene actions
        std::function<void(const std::string&)> requestSceneAction;       /**< Request a named scene action (e.g. "save", "new"). */
        std::function<void(SceneObjectType)> addObject;            /**< Add a new scene object of the given type. */
        std::function<void()> addObjectFromSelectedAsset;          /**< Instantiate the currently selected asset as a scene object. */

        // Edit menu
        std::function<bool()> canUndoHistory;                      /**< Returns true when there is a history state to undo. */
        std::function<bool()> canRedoHistory;                      /**< Returns true when there is a history state to redo. */
        std::function<void()> undoHistory;                         /**< Undo the last history step. */
        std::function<void()> redoHistory;                         /**< Redo the next history step. */
        std::function<void(int)> openRenameObjectModal;            /**< Open the rename modal for the object at the given scene index. */
        std::function<bool()> createPrefabFromSelection;           /**< Serialize the selected objects as a prefab; returns true on success. */
        std::function<void()> duplicateSelectedObjects;            /**< Duplicate all currently selected scene objects. */
        std::function<void()> requestDeleteSelectedObjects;        /**< Request deletion of all currently selected scene objects. */
        std::function<std::string(int)> buildSelectionRefCode;     /**< Build a code reference string for the object at the given scene index. */

        // View menu
        std::function<void()> openHelpPopup;                       /**< Open the keyboard-shortcuts help popup. */
        std::function<void()> openQuickOpen;                       /**< Open the quick-open file picker. */
        std::function<void()> openCommandPalette;                  /**< Open the command palette. */
        std::function<void(bool)> setFlyMode;                      /**< Enable or disable fly-cam mode. */
        std::function<void(bool)> setResetDockLayout;              /**< Request a dock-layout reset on the next frame. */

        // Settings & modals
        std::function<void()> openSettings;                        /**< Open the editor settings modal. */

        // File menu custom callback
        std::function<void()> fileMenuRenderCallback;              /**< Optional extra items rendered at the end of the File menu. */
    };

    /** @brief Read-only view of editor state that EditorToolbar reads to drive its UI. */
    struct EditorToolbarState {
        // Play mode
        bool* playMode = nullptr;             /**< Pointer to the current play-mode flag. */
        int* playModeEscPresses = nullptr;    /**< Pointer to the escape-press counter used to exit play mode. */

        // Fly mode
        bool* flyMode = nullptr;              /**< Pointer to the fly-cam enabled flag. */
        bool* flyCamInitialized = nullptr;    /**< Pointer to the fly-cam initialization flag. */
        bool* prevCursorInit = nullptr;       /**< Pointer to the previous-cursor-position initialization flag. */

        // View
        bool* quickOpenOpen = nullptr;              /**< Pointer to the quick-open visibility flag. */
        bool* commandPaletteOpen = nullptr;         /**< Pointer to the command-palette visibility flag. */
        std::string* commandPaletteQuery = nullptr; /**< Pointer to the command-palette search string. */

        // Gizmo mode
        void* currentGizmoMode = nullptr;  /**< Opaque pointer to GizmoMode; avoids a circular header dependency. */

        // Rendering
        bool* wireframeMode = nullptr;       /**< Pointer to the wireframe-rendering toggle. */

        // Layout
        bool* resetDockLayoutRequested = nullptr; /**< Pointer to the flag requesting a dock-layout reset. */

        // Window
        GLFWwindow* window = nullptr; /**< Host GLFW window; forwarded to sub-panels that need it. */

        // UI components to access (modals, etc.)
        void* settingsModal = nullptr;  /**< Opaque pointer to EditorSettingsModal; avoids a circular header dependency. */
        void* helpPopup = nullptr;      /**< Opaque pointer to EditorHelpPopup; avoids a circular header dependency. */
        void* uiWidgets = nullptr;      /**< Opaque pointer to EditorUIWidgets; avoids a circular header dependency. */
        void* mcpController = nullptr;  /**< Opaque pointer to Mcp::McpController; avoids a circular header dependency. */

        // Selection info
        std::vector<int>* selectedIndices = nullptr;  /**< Pointer to the list of selected scene-object indices. */
        std::string* selectedAssetId = nullptr;       /**< Pointer to the identifier of the selected asset. */
    };

    /** @brief Draws the editor menu bar and icon toolbar. */
    class EditorToolbar {
    public:
        /** @brief Draws the full toolbar for this frame.
         *  @param callbacks Callable table used to trigger editor actions.
         *  @param state     Read-only state used to reflect the current editor state in the UI. */
        void Draw(const EditorToolbarCallbacks& callbacks, const EditorToolbarState& state) const;

    private:
        /** @brief Draws the File drop-down menu.
         *  @param callbacks Editor action callbacks.
         *  @param state     Current editor state. */
        void DrawFileMenu(const EditorToolbarCallbacks& callbacks,
                         const EditorToolbarState& state) const;

        /** @brief Draws the Add-object drop-down menu.
         *  @param callbacks Editor action callbacks.
         *  @param state     Current editor state. */
        void DrawAddMenu(const EditorToolbarCallbacks& callbacks,
                        const EditorToolbarState& state) const;

        /** @brief Draws the Edit drop-down menu.
         *  @param callbacks Editor action callbacks.
         *  @param state     Current editor state. */
        void DrawEditMenu(const EditorToolbarCallbacks& callbacks,
                         const EditorToolbarState& state) const;

        /** @brief Draws the items inside the Edit menu (factored out for reuse).
         *  @param callbacks Editor action callbacks.
         *  @param state     Current editor state. */
        void DrawEditMenuItems(const EditorToolbarCallbacks& callbacks,
                              const EditorToolbarState& state) const;

        /** @brief Draws the View drop-down menu.
         *  @param callbacks Editor action callbacks.
         *  @param state     Current editor state. */
        void DrawViewMenu(const EditorToolbarCallbacks& callbacks,
                         const EditorToolbarState& state) const;

        /** @brief Draws the icon toolbar row beneath the menu bar.
         *  @param callbacks Editor action callbacks.
         *  @param state     Current editor state. */
        void DrawIconToolbar(const EditorToolbarCallbacks& callbacks,
                            const EditorToolbarState& state) const;
    };
}
