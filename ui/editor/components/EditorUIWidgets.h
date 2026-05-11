/** @file EditorUIWidgets.h
 *  @brief Transient overlays, toast notifications, and confirmation modals for the editor. */
#pragma once

#include "renderer/Camera.h"
#include "ui/editor/ViewSnap.h"
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace Horo {
    class Registry;
}

namespace Horo::Editor {

class EditorLayer;
struct SceneDocument;

/** @brief Draws and manages short-lived editor UI elements: toasts, overlays, and modal dialogs. */
class EditorUIWidgets {
public:
    /** @brief Callback table used to dispatch editor-level actions from modals. */
    struct Callbacks {
        std::function<void(const std::vector<int>&)> onConfirmDeleteObjects; /**< Called when the user confirms deletion of the listed scene objects. */
        std::function<void(const std::string&)> onConfirmDeleteAsset;        /**< Called when the user confirms deletion of the named asset. */
        std::function<void()> onConfirmExit;                                  /**< Called when the user confirms the exit action. */
        std::function<bool(int, const std::string&)> onApplyRenameObject;    /**< Called to apply a rename; returns true on success. */
        std::function<std::string()> getStatusBarText;                        /**< Returns the string to display in the status bar. */
    };

    /** @brief Constructs widget state for editor overlays and modal helpers. */
    EditorUIWidgets() = default;

    /** @brief Binds the widget set to its owning editor layer.
     *  @param editor Non-owning pointer to the editor layer. Must outlive this object. */
    void Initialize(EditorLayer* editor);

    /** @brief Installs the callback table used by modal dialogs.
     *  @param callbacks Callable table; individual fields may be left empty. */
    void SetCallbacks(Callbacks callbacks) { m_callbacks = std::move(callbacks); }

    // Overlays & toasts
    /** @brief Draws the clipboard action toast if one is currently active. */
    void DrawClipboardToast() const;

    /** @brief Draws the hot-reload progress overlay while a reload is in progress. */
    void DrawHotReloadOverlay() const;

    /** @brief Draws the status bar at the bottom of the editor window. */
    void DrawStatusBar() const;

    /** @brief Draws the view orientation gimbal based on the supplied camera.
     *  @param cam Current scene camera used to derive axis orientations. */
    void DrawViewGimbal(const Camera& cam);

    // Confirmation modals
    /** @brief Draws the "confirm delete objects" modal dialog if it is open. */
    void DrawConfirmDeleteObjectsModal();

    /** @brief Draws the "confirm delete asset" modal dialog if it is open. */
    void DrawConfirmDeleteAssetModal();

    /** @brief Draws the "confirm exit" modal dialog if it is open. */
    void DrawExitConfirmModal();

    // Input modals
    /** @brief Draws the "rename object" input modal if it is open. */
    void DrawRenameObjectModal();

    // Callbacks for state updates
    /** @brief Records a clipboard action and begins showing the toast.
     *  @param label  Human-readable label displayed in the toast.
     *  @param duration Seconds the toast remains visible. */
    void OnClipboardAction(std::string_view label = {}, float duration = 1.5f);

    /** @brief Activates the hot-reload overlay with an initial label.
     *  @param duration  Expected total duration of the reload in seconds.
     *  @param label     Optional text shown in the overlay. */
    void OnHotReloadStart(float duration, std::string_view label = {});

    /** @brief Updates the hot-reload overlay's progress and spinner values.
     *  @param progress Normalised completion value in [0, 1].
     *  @param spinner  Current spinner animation phase in seconds. */
    void OnHotReloadProgress(float progress, float spinner);

    /** @brief Deactivates and hides the hot-reload overlay. */
    void OnHotReloadEnd();

    /** @brief Opens the delete-objects confirmation modal for the given scene object indices.
     *  @param indices Scene indices of the objects pending deletion. */
    void OpenConfirmDeleteObjects(const std::vector<int>& indices);

    /** @brief Opens the delete-asset confirmation modal for the given asset identifier.
     *  @param assetId Unique identifier of the asset pending deletion. */
    void OpenConfirmDeleteAsset(std::string_view assetId);

    /** @brief Opens the exit-confirmation modal. */
    void OpenConfirmExit();

    /** @brief Opens the rename-object input modal for the given scene object.
     *  @param objectIndex Scene index of the object to rename. */
    void OpenRenameObject(int objectIndex);

    // Getters for state needed by EditorLayer
    /** @brief Returns the pending view-snap direction selected via the gimbal.
     *  @return The pending ViewSnap value, or ViewSnap::None if nothing is pending. */
    ViewSnap GetPendingViewSnap() const { return m_pendingViewSnap; }

    /** @brief Sets the pending view-snap direction.
     *  @param snap The desired snap direction. */
    void SetPendingViewSnap(ViewSnap snap) { m_pendingViewSnap = snap; }

    /** @brief Clears any pending view-snap direction. */
    void ClearPendingViewSnap() { m_pendingViewSnap = ViewSnap::None; }

    // State helpers
    /** @brief Commits the current rename draft and invokes the rename callback. */
    void ApplyRenameObject();

    /** @brief Discards the current rename draft and closes the rename modal. */
    void CancelRenameObject();

    /** @brief Sets an error string displayed inside the rename modal.
     *  @param error Human-readable error message. */
    void SetRenameObjectError(std::string_view error) { m_renameState.error = error; }

    // State getters (for EditorLayer to update)
    /** @brief Returns true if the delete-objects confirmation modal is currently open. */
    bool IsConfirmDeleteObjectsOpen() const { return m_confirmDeleteObjectsOpen; }

    /** @brief Returns true if the delete-asset confirmation modal is currently open. */
    bool IsConfirmDeleteAssetOpen() const { return m_confirmDeleteAssetOpen; }

    /** @brief Returns true if the exit-confirmation modal is currently open. */
    bool IsConfirmExitOpen() const { return m_confirmExitOpen; }

    /** @brief Returns true if the rename-object modal is currently open. */
    bool IsRenameObjectOpen() const { return m_renameState.open; }

private:
    /** @brief Grouped state for the rename-object modal. */
    struct RenameState {
        bool open = false;         /**< True while the rename modal is visible. */
        int index = -1;            /**< Scene index of the object being renamed; -1 when none. */
        std::string draft;         /**< In-progress text entered by the user. */
        std::string error;         /**< Validation error displayed inside the rename modal. */
    };

    EditorLayer* m_editor = nullptr;           /**< Non-owning pointer to the parent editor layer. */
    Callbacks m_callbacks;                     /**< Editor-level callbacks invoked from modal actions. */

    // Toast/notification state
    mutable float m_clipboardToastTime = 0.0f; /**< Remaining display time for the clipboard toast in seconds (mutable for const draw). */
    std::string m_clipboardToastLabel;          /**< Text shown in the active clipboard toast. */

    // Hot reload overlay state
    bool m_hotReloadOverlayActive = false;      /**< True while a hot reload is in progress. */
    float m_hotReloadOverlayProgress = 0.0f;   /**< Normalised reload progress in [0, 1]. */
    float m_hotReloadOverlaySpinner = 0.0f;    /**< Current spinner animation phase in seconds. */
    std::string m_hotReloadOverlayLabel;        /**< Text displayed inside the reload overlay. */

    // Delete confirmation modal state
    bool m_confirmDeleteObjectsOpen = false;             /**< True while the delete-objects modal is visible. */
    bool m_confirmDeleteAssetOpen = false;               /**< True while the delete-asset modal is visible. */
    std::vector<int> m_pendingDeleteObjectIndices;       /**< Scene indices queued for deletion. */
    std::string m_pendingDeleteAssetId;                  /**< Asset identifier queued for deletion. */
    std::string m_pendingDeleteAssetError;               /**< Error message from the last attempted asset deletion. */

    // Exit confirmation modal state
    bool m_confirmExitOpen = false;  /**< True while the exit-confirmation modal is visible. */
    std::string m_exitConfirmError;  /**< Error message shown inside the exit modal. */

    RenameState m_renameState;  /**< Grouped rename-modal state. */

    // View gimbal state
    ViewSnap m_pendingViewSnap = ViewSnap::None; /**< View-snap direction chosen via the gimbal, consumed by EditorLayer. */

    /** @brief Screen-space bounding rectangle used for interaction hit-testing. */
    struct PickRect {
        bool valid = false;   /**< True when the rectangle holds meaningful coordinates. */
        float minX = 0.0f;   /**< Left boundary in screen pixels. */
        float minY = 0.0f;   /**< Top boundary in screen pixels. */
        float maxX = 0.0f;   /**< Right boundary in screen pixels. */
        float maxY = 0.0f;   /**< Bottom boundary in screen pixels. */

        /** @brief Returns true if (x, y) falls within the rectangle, optionally expanded by margin.
         *  @param x      Screen X coordinate to test.
         *  @param y      Screen Y coordinate to test.
         *  @param margin Additional outward expansion applied to all four sides.
         *  @return True when the point is inside the (possibly expanded) rectangle. */
        bool Contains(float x, float y, float margin = 0.0f) const {
            return x >= minX - margin && x <= maxX + margin &&
                   y >= minY - margin && y <= maxY + margin;
        }
    };
    PickRect m_viewGizmoPickRect; /**< Hit-test rectangle for the view orientation gimbal. */
};

}  // namespace Horo::Editor
