#pragma once

#include <functional>
#include <string>
#include <vector>

#include "ui/editor/SceneDocument.h"

struct GLFWwindow;

namespace Horo::Editor {
    struct EditorComponentContext;

    struct EditorToolbarCallbacks {
        // Scene actions
        std::function<void(std::string)> requestSceneAction;  // param: action name
        std::function<void(SceneObjectType)> addObject;
        std::function<void()> addObjectFromSelectedAsset;

        // Edit menu
        std::function<bool()> canUndoHistory;
        std::function<bool()> canRedoHistory;
        std::function<void()> undoHistory;
        std::function<void()> redoHistory;
        std::function<void(int)> openRenameObjectModal;
        std::function<bool()> createPrefabFromSelection;
        std::function<void()> duplicateSelectedObjects;
        std::function<void()> requestDeleteSelectedObjects;
        std::function<std::string(int)> buildSelectionRefCode;

        // View menu
        std::function<void()> openHelpPopup;
        std::function<void()> openQuickOpen;
        std::function<void()> openCommandPalette;
        std::function<void(bool)> setFlyMode;
        std::function<void(bool)> setResetDockLayout;

        // Settings & modals
        std::function<void()> openSettings;

        // File menu custom callback
        std::function<void()> fileMenuRenderCallback;
    };

    struct EditorToolbarState {
        // Play mode
        bool* playMode = nullptr;
        int* playModeEscPresses = nullptr;

        // Fly mode
        bool* flyMode = nullptr;
        bool* flyCamInitialized = nullptr;
        bool* prevCursorInit = nullptr;

        // View
        bool* quickOpenOpen = nullptr;
        bool* commandPaletteOpen = nullptr;
        std::string* commandPaletteQuery = nullptr;

        // Gizmo mode
        void* currentGizmoMode = nullptr;  // GizmoMode* - avoid header circular dependency

        // Rendering
        bool* wireframeMode = nullptr;

        // Layout
        bool* resetDockLayoutRequested = nullptr;

        // Window
        GLFWwindow* window = nullptr;

        // UI components to access (modals, etc.)
        void* settingsModal = nullptr;  // EditorSettingsModal*
        void* helpPopup = nullptr;      // EditorHelpPopup*
        void* uiWidgets = nullptr;      // EditorUIWidgets*
        void* mcpController = nullptr;  // Mcp::McpController*

        // Selection info
        std::vector<int>* selectedIndices = nullptr;
        std::string* selectedAssetId = nullptr;
    };

    class EditorToolbar {
    public:
        void Draw(const EditorToolbarCallbacks& callbacks, const EditorToolbarState& state);

    private:
        void DrawFileMenu(const EditorToolbarCallbacks& callbacks,
                         const EditorToolbarState& state);
        void DrawAddMenu(const EditorToolbarCallbacks& callbacks,
                        const EditorToolbarState& state);
        void DrawEditMenu(const EditorToolbarCallbacks& callbacks,
                         const EditorToolbarState& state);
        void DrawEditMenuItems(const EditorToolbarCallbacks& callbacks,
                              const EditorToolbarState& state);
        void DrawViewMenu(const EditorToolbarCallbacks& callbacks,
                         const EditorToolbarState& state);
        void DrawIconToolbar(const EditorToolbarCallbacks& callbacks,
                            const EditorToolbarState& state);
    };
}
