#pragma once

#include "renderer/Camera.h"
#include "ui/editor/ViewSnap.h"
#include <functional>
#include <vector>
#include <string>

namespace Horo {
    class Registry;
}

namespace Horo::Editor {

class EditorLayer;
struct SceneDocument;

class EditorUIWidgets {
public:
    EditorUIWidgets() = default;

    void Initialize(EditorLayer* editor);

    // Overlays & toasts
    void DrawClipboardToast() const;
    void DrawHotReloadOverlay() const;
    void DrawStatusBar() const;
    void DrawViewGimbal(const Camera& cam);

    // Confirmation modals
    void DrawConfirmDeleteObjectsModal();
    void DrawConfirmDeleteAssetModal();
    void DrawExitConfirmModal();

    // Input modals
    void DrawRenameObjectModal();

    // Callbacks for state updates
    void OnClipboardAction(const std::string& label = "", float duration = 1.5f);
    void OnHotReloadStart(float duration, const std::string& label = "");
    void OnHotReloadProgress(float progress, float spinner);
    void OnHotReloadEnd();

    void OpenConfirmDeleteObjects(const std::vector<int>& indices);
    void OpenConfirmDeleteAsset(const std::string& assetId);
    void OpenConfirmExit();
    void OpenRenameObject(int objectIndex);

    // Business logic callbacks - set by EditorLayer
    std::function<void(const std::vector<int>&)> onConfirmDeleteObjects;
    std::function<void(const std::string&)> onConfirmDeleteAsset;
    std::function<void()> onConfirmExit;
    std::function<bool(int, const std::string&)> onApplyRenameObject; // returns success
    std::function<std::string()> getStatusBarText;

    // Getters for state needed by EditorLayer
    ViewSnap GetPendingViewSnap() const { return m_pendingViewSnap; }
    void SetPendingViewSnap(ViewSnap snap) { m_pendingViewSnap = snap; }
    void ClearPendingViewSnap() { m_pendingViewSnap = ViewSnap::None; }

    // State helpers
    void ApplyRenameObject();
    void CancelRenameObject();
    void SetRenameObjectError(const std::string& error) { m_renameObjectError = error; }

    // State getters (for EditorLayer to update)
    bool IsConfirmDeleteObjectsOpen() const { return m_confirmDeleteObjectsOpen; }
    bool IsConfirmDeleteAssetOpen() const { return m_confirmDeleteAssetOpen; }
    bool IsConfirmExitOpen() const { return m_confirmExitOpen; }
    bool IsRenameObjectOpen() const { return m_renameObjectOpen; }

private:
    EditorLayer* m_editor = nullptr;

    // Toast/notification state
    mutable float m_clipboardToastTime = 0.0f;  // mutable for time updates in const functions
    std::string m_clipboardToastLabel;

    // Hot reload overlay state
    bool m_hotReloadOverlayActive = false;
    float m_hotReloadOverlayProgress = 0.0f;
    float m_hotReloadOverlaySpinner = 0.0f;
    std::string m_hotReloadOverlayLabel;

    // Delete confirmation modal state
    bool m_confirmDeleteObjectsOpen = false;
    bool m_confirmDeleteAssetOpen = false;
    std::vector<int> m_pendingDeleteObjectIndices;
    std::string m_pendingDeleteAssetId;
    std::string m_pendingDeleteAssetError;

    // Exit confirmation modal state
    bool m_confirmExitOpen = false;
    std::string m_exitConfirmError;

    // Rename object modal state
    bool m_renameObjectOpen = false;
    int m_renameObjectIndex = -1;
    std::string m_renameObjectDraft;
    std::string m_renameObjectError;

    // View gimbal state
    ViewSnap m_pendingViewSnap = ViewSnap::None;
    struct PickRect {
        bool valid = false;
        float minX = 0.0f;
        float minY = 0.0f;
        float maxX = 0.0f;
        float maxY = 0.0f;

        bool Contains(float x, float y, float margin = 0.0f) const {
            return x >= minX - margin && x <= maxX + margin &&
                   y >= minY - margin && y <= maxY + margin;
        }
    } m_viewGizmoPickRect;
};

}  // namespace Horo::Editor


