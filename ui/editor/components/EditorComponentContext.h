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

    struct EditorComponentContext {
        // Core document
        SceneDocument* document = nullptr;
        const SceneDocument* lastSavedDocument = nullptr;
        const EditorSchema* schema = nullptr;

        // Selection state
        std::vector<int>* selectedIndices = nullptr;
        std::string* selectedAssetId = nullptr;

        // Gizmo & transform
        TransformGizmo* gizmo = nullptr;
        GizmoMode* currentGizmoMode = nullptr;

        // Asset system
        AssetImportService* assetImportService = nullptr;

        // Workspace
        EditorWorkspaceDocument* workspaceDocument = nullptr;

        // Live scene
        Registry* liveRegistry = nullptr;

        // Callbacks
        std::function<void(const SceneObject&)>* transformCallback = nullptr;
        std::function<std::vector<std::string>()>* scriptBehaviorOptionsCallback = nullptr;

        // Flags
        bool* active = nullptr;
        bool* playMode = nullptr;
        bool* flyMode = nullptr;
    };
}
