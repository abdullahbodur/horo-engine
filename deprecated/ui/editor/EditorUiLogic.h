/** @file EditorUiLogic.h
 *  @brief Pure UI-logic helpers for editor layout computation, status text, and input-event decisions. */
#pragma once

#include <functional>
#include <string>

#include "math/Vec3.h"

namespace Horo::Editor {
    /** @brief Axis-aligned screen rectangle used to identify panel regions and drop zones. */
    struct EditorViewportRect {
        float minX = 0.0f; /**< Left edge in screen (ImGui) coordinates. */
        float minY = 0.0f; /**< Top edge in screen (ImGui) coordinates. */
        float maxX = 0.0f; /**< Right edge in screen (ImGui) coordinates. */
        float maxY = 0.0f; /**< Bottom edge in screen (ImGui) coordinates. */

        /** @brief Returns true when the point (@p x, @p y) lies inside this rectangle (inclusive). */
        bool Contains(float x, float y) const {
            return x >= minX && x <= maxX && y >= minY && y <= maxY;
        }
    };

    /** @brief Screen-space rects for the three sub-regions of the viewport orientation gimbal. */
    struct EditorViewGimbalLayout {
        EditorViewportRect wireButtonRect; /**< Rectangle covering the wireframe-toggle button. */
        EditorViewportRect gimbalRect;     /**< Rectangle covering the axis-gimbal drawing area. */
        EditorViewportRect pickRect;       /**< Combined pick-exclusion rect (union of the above two). */
    };

    /** @brief Sizing and offset parameters used to lay out the viewport orientation gimbal widget. */
    struct EditorViewGimbalMetrics {
        float windowWidth = 128.0f;    /**< Total width of the gimbal overlay window in pixels. */
        float windowHeight = 138.0f;   /**< Total height of the gimbal overlay window in pixels. */
        float buttonSize = 28.0f;      /**< Visual size of the wireframe-toggle button in pixels. */
        float buttonFrameSize = 36.0f; /**< Hit-region size including button padding. */
        float buttonGap = 10.0f;       /**< Gap between the button and the gimbal circle. */
        float edgeMargin = 10.0f;      /**< Distance from the viewport edge to the gimbal window. */
        float titleOffsetX = 8.0f;     /**< Horizontal offset of the camera-mode title text. */
        float titleOffsetY = 8.0f;     /**< Vertical offset of the camera-mode title text. */
        float contentOffsetX = 10.0f;  /**< Horizontal offset of the gimbal drawing area. */
        float contentOffsetY = 26.0f;  /**< Vertical offset of the gimbal drawing area. */
        float hitRegionHeight = 94.0f; /**< Height of the axis hit-test region in pixels. */
        float pivotTextOffsetY = 18.0f;/**< Vertical offset of the pivot-mode label. */
    };

    /** @brief Returns true when the help popup toggle shortcut fires on this frame.
     *  @param currToggle    Current-frame key state.
     *  @param prevToggle    Previous-frame key state (used for edge detection).
     *  @param wantsTextInput True when ImGui has keyboard focus on a text field.
     *  @param anyItemActive  True when any ImGui item is active. */
    bool ShouldToggleHelpPopup(bool currToggle, bool prevToggle,
                               bool wantsTextInput, bool anyItemActive);

    /** @brief Returns true when the quick-open popup should open this frame.
     *  @param currToggle    Current-frame key state.
     *  @param prevToggle    Previous-frame key state.
     *  @param navActive     True when viewport navigation is active (shortcut suppressed).
     *  @param wantsTextInput True when ImGui captures keyboard input.
     *  @param anyItemActive  True when any ImGui item is active. */
    bool ShouldOpenQuickOpen(bool currToggle, bool prevToggle, bool navActive,
                             bool wantsTextInput, bool anyItemActive);

    /** @brief Returns true when the command palette should open this frame.
     *  @param currToggle    Current-frame key state.
     *  @param prevToggle    Previous-frame key state.
     *  @param navActive     True when viewport navigation is active.
     *  @param wantsTextInput True when ImGui captures keyboard input.
     *  @param anyItemActive  True when any ImGui item is active. */
    bool ShouldOpenCommandPalette(bool currToggle, bool prevToggle, bool navActive,
                                  bool wantsTextInput, bool anyItemActive);

    /** @brief Returns true when RMB should start viewport navigation this frame. */
    bool ShouldStartViewportNav(bool rmbDown, bool alreadyActive,
                                float mouseX, float mouseY,
                                const EditorViewportRect &viewportRect);

    /** @brief Returns true when the "copy selection ref" shortcut fires this frame.
     *  @param currCopyRef        Current-frame key state.
     *  @param prevCopyRef        Previous-frame key state.
     *  @param wantsTextInput     True when ImGui captures keyboard input.
     *  @param anyItemActive      True when any ImGui item is active.
     *  @param hasPrimarySelection True when at least one object is selected. */
    bool ShouldCopySelectionRef(bool currCopyRef, bool prevCopyRef,
                                bool wantsTextInput, bool anyItemActive,
                                bool hasPrimarySelection);

    /** @brief Returns true when the Delete key should delete the current selection.
     *  @param currDelete    Current-frame Delete key state.
     *  @param prevDelete    Previous-frame Delete key state.
     *  @param hasSelection  True when at least one object is selected.
     *  @param wantsTextInput True when ImGui captures keyboard input.
     *  @param anyItemActive  True when any ImGui item is active. */
    bool ShouldRequestDeleteSelection(bool currDelete, bool prevDelete,
                                      bool hasSelection,
                                      bool wantsTextInput,
                                      bool anyItemActive);

    /** @brief Returns true when the Escape key should be handled by the editor this frame.
     *  @param currEsc         Current-frame Escape key state.
     *  @param prevEsc         Previous-frame Escape key state.
     *  @param wantsTextInput  True when ImGui captures keyboard input.
     *  @param anyItemActive   True when any ImGui item is active.
     *  @param hasBlockingPopup True when a modal popup is open. */
    bool ShouldHandleEditorEscape(bool currEsc, bool prevEsc, bool wantsTextInput,
                                  bool anyItemActive, bool hasBlockingPopup);

    /** @brief Returns true when a single-object properties section can be shown.
     *  @param selectionCount Number of currently selected objects.
     *  @param primaryIndex   Index of the primary selection (-1 when none).
     *  @param objectCount    Total number of objects in the scene. */
    bool CanEditSingleSelection(int selectionCount, int primaryIndex,
                                int objectCount);

    /** @brief Snapshot of editor boolean flags used to build the status-bar text. */
    struct EditorStatusSnapshot {
        int selectionCount = 0;   /**< Number of currently selected scene objects. */
        bool dirty = false;       /**< True when the scene has unsaved changes. */
        bool navActive = false;   /**< True when viewport navigation is active. */
        bool reloadPending = false;/**< True when a scene reload is queued. */
    };

    /** @brief Pre-built string fragments for the editor status bar. */
    struct EditorStatusText {
        int selectionCount = 0;          /**< Number of selected objects (pass-through). */
        const char *dirtyText = "no";    /**< "yes" or "no" for the unsaved-changes indicator. */
        const char *navText = "idle";    /**< "active" or "idle" for the viewport-nav indicator. */
        const char *reloadText = "idle"; /**< "pending" or "idle" for the reload indicator. */
    };

    /** @brief Signature of a callback invoked when an asset drag-drop lands on the viewport. */
    using EditorViewportAssetDropHandler =
    std::function<bool(void *, const char *)>;

    /** @brief Result returned by DrawViewportAssetDropTarget. */
    struct EditorViewportAssetDropResult {
        bool targetVisible = false;  /**< True when the ImGui drop-target region was drawn. */
        bool payloadMatched = false; /**< True when a matching drag payload was detected. */
        bool delivered = false;      /**< True when the payload was delivered to the target. */
        bool accepted = false;       /**< True when the handler accepted the drop. */
    };

    /** @brief Decision returned by ResolveEditorExitDecision. */
    enum class EditorExitDecision {
        ExitImmediately,      /**< No unsaved changes; close the editor immediately. */
        PromptUnsavedConfirm, /**< Unsaved changes exist; show a confirmation dialog first. */
    };

    /** @brief Returns the appropriate exit decision given the current dirty state.
     *  @param hasUnsavedChanges True when the scene has modifications not yet saved. */
    EditorExitDecision ResolveEditorExitDecision(bool hasUnsavedChanges);

    /** @brief Returns true when the editor close sequence should be finalised this frame.
     *  @param closeRequested   True when the user confirmed or requested a close.
     *  @param hasPendingReload True when a scene reload is still in flight. */
    bool ShouldFinalizeEditorClose(bool closeRequested, bool hasPendingReload);

    /** @brief Builds status-bar text strings from an editor state snapshot.
     *  @param snapshot Current editor boolean state.
     *  @return Populated EditorStatusText ready for rendering. */
    EditorStatusText BuildEditorStatusText(const EditorStatusSnapshot &snapshot);

    /** @brief Returns the default width of the left dock panel for a given display width.
     *  @param displayWidth Full display (framebuffer) width in pixels. */
    float ComputeEditorLeftDockWidth(float displayWidth);

    /** @brief Returns the default width of the right properties panel for a given display width.
     *  @param displayWidth Full display (framebuffer) width in pixels. */
    float ComputeEditorRightPanelWidth(float displayWidth);

    /** @brief Returns the default height of the bottom dock for a given display height.
     *  @param displayHeight Full display (framebuffer) height in pixels. */
    float ComputeEditorBottomDockHeight(float displayHeight);

    /** @brief Draws the ImGui drag-and-drop accept zone over the viewport and invokes the handler.
     *  @param playMode     True when the editor is in play-in-editor mode.
     *  @param targetWidth  Width of the drop target in pixels.
     *  @param targetHeight Height of the drop target in pixels.
     *  @param userData     Opaque pointer forwarded verbatim to @p onDrop.
     *  @param onDrop       Handler called with (userData, assetIdText) when a drop lands.
     *  @return Drop result flags. */
    EditorViewportAssetDropResult
    DrawViewportAssetDropTarget(bool playMode, float targetWidth,
                                float targetHeight, void *userData,
                                const EditorViewportAssetDropHandler &onDrop);

    /** @brief Computes the screen rect occupied by the 3D viewport panel.
     *  @param displayWidth    Full display width in pixels.
     *  @param displayHeight   Full display height in pixels.
     *  @param toolbarHeight   Height of the top toolbar in pixels.
     *  @param statusHeight    Height of the status bar in pixels.
     *  @param bottomDockHeight Height of the bottom dock panel in pixels.
     *  @param leftDockWidth   Width of the left hierarchy/project dock in pixels.
     *  @param rightPanelWidth Width of the right properties panel in pixels.
     *  @return The computed viewport rectangle. */
    EditorViewportRect
    BuildEditorViewportRect(float displayWidth, float displayHeight,
                            float toolbarHeight, float statusHeight,
                            float bottomDockHeight, float leftDockWidth,
                            float rightPanelWidth);

    /** @brief Returns a reference to the singleton EditorViewGimbalMetrics constants. */
    const EditorViewGimbalMetrics &GetEditorViewGimbalMetrics();

    /** @brief Computes the screen-space layout for the viewport orientation gimbal.
     *  @param viewportRect    Current viewport screen rect.
     *  @param displayWidth    Full display width in pixels.
     *  @param rightPanelWidth Width of the right panel (gimbal is anchored to the right edge).
     *  @param toolbarHeight   Height of the top toolbar (gimbal is anchored below it).
     *  @return Layout containing wire-button, gimbal, and combined pick rects. */
    EditorViewGimbalLayout
    BuildEditorViewGimbalLayout(const EditorViewportRect &viewportRect,
                                float displayWidth, float rightPanelWidth,
                                float toolbarHeight);

    /** @brief Parses a comma-separated "x,y,z" string into a Vec3.
     *  @param text     Source string in "x,y,z" format.
     *  @param outValue Receives the parsed Vec3 on success.
     *  @return True when exactly three numeric components were parsed. */
    bool TryParseVec3Csv(const std::string &text, Vec3 *outValue);
} // namespace Horo::Editor
