/** @file EditorLayer.h
 *  @brief In-game editor overlay: lifecycle, input, rendering, document management, and MCP command dispatch. */
#pragma once
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include <imgui.h>
#include <nlohmann/json.hpp>

#include "ui/editor/AssetImportService.h"
#include "ui/editor/EditorSchema.h"
#include "ui/editor/EditorUiLogic.h"
#include "ui/editor/EditorUserSettings.h"
#include "ui/editor/EditorWorkspaceSettings.h"
#include "ui/editor/SceneDocument.h"
#include "ui/editor/TransformGizmo.h"
#include "ui/editor/components/EditorAssetsPanel.h"
#include "ui/editor/components/EditorBottomDock.h"
#include "ui/editor/components/EditorHelpPopup.h"
#include "ui/editor/components/EditorSettingsModal.h"
#include "ui/editor/components/EditorToolbar.h"
#include "ui/editor/components/EditorUIWidgets.h"
#include "mcp/McpController.h"
#include "renderer/Camera.h"
#include "renderer/Shader.h"

struct GLFWwindow;

namespace Horo {
    class Registry;

    namespace Editor {
        /** @brief In-game editor overlay.
         *
         *  Typical usage: call Init(), LoadDocument(), Toggle() once, then each frame call
         *  OnUpdate() followed by Render(). Poll WantsSceneReload() to detect when the
         *  caller must rebuild the live scene.
         */
        class EditorLayer {
        public:
            /** @brief Initialises the ImGui backend and editor subsystems.
             *  @param window The GLFW window that owns the render context. */
            void Init(GLFWwindow *window);

            /** @brief Wires up all EditorUIWidgets callbacks (rename, delete, exit, status text). */
            void InitUiWidgetCallbacks();

            /** @brief Initialises ImGui context, fonts, theme, and persisted layout. */
            void InitImGuiContext(GLFWwindow *window);

            /** @brief Locates and loads the editor JSON schema from known paths. */
            void LoadEditorSchema();

            /** @brief Shuts down the editor and releases all ImGui/backend resources. */
            void Shutdown();

            /** @brief Menu handler: begin a new empty scene (prompts to save if dirty). */
            void OnMenuNewScene();
            /** @brief Menu handler: open a scene file via a native file dialog. */
            void OnMenuOpenScene();
            /** @brief Menu handler: reset the ImGui dock layout to its default arrangement. */
            void OnMenuResetLayout();
            /** @brief Menu handler: open the editor settings modal. */
            void OnMenuSettings();
            /** @brief Menu handler: close the editor (prompts to save if dirty). */
            void OnMenuCloseEditor();
            /** @brief Menu handler: add a new Panel object to the scene. */
            void OnMenuAddPanel();
            /** @brief Menu handler: add a new Prop object to the scene. */
            void OnMenuAddProp();
            /** @brief Menu handler: add a new Light object to the scene. */
            void OnMenuAddLight();
            /** @brief Menu handler: add a new Camera object to the scene. */
            void OnMenuAddCamera();
            /** @brief Menu handler: add a Prop created from the currently selected asset. */
            void OnMenuAddPropFromAsset();
            /** @brief Menu handler: undo the last history change. */
            void OnMenuUndo();
            /** @brief Menu handler: redo the last undone history change. */
            void OnMenuRedo();
            /** @brief Menu handler: open the rename dialog for the primary selection. */
            void OnMenuRename();
            /** @brief Menu handler: create a prefab from the current selection. */
            void OnMenuCreatePrefab();
            /** @brief Menu handler: duplicate the selected object(s). */
            void OnMenuDuplicate();
            /** @brief Menu handler: delete the selected object(s) after confirmation. */
            void OnMenuDelete();
            /** @brief Menu handler: toggle fly-camera mode. */
            void OnMenuFlyMode();
            /** @brief Menu handler: toggle the help/shortcuts popup. */
            void OnMenuHelp();
            /** @brief Menu handler: open the quick-open popup. */
            void OnMenuQuickOpen();
            /** @brief Menu handler: open the command palette popup. */
            void OnMenuCommandPalette();
            /** @brief Menu handler: reset the viewport dock layout to its default. */
            void OnMenuViewResetLayout();

            /** @brief Toggles editor mode on/off and updates the GLFW cursor visibility accordingly. */
            void Toggle();

            /** @brief Explicitly shows or hides the OS cursor.
             *  @param visible True to show the cursor; false to hide it. */
            void SetCursorVisible(bool visible);

            /** @brief Processes input and per-frame picking for the editor.
             *
             *  In fly mode the camera position and target are updated directly.
             *  @param dt      Frame delta time in seconds.
             *  @param cam     Live camera; modified in-place when in fly mode.
             *  @param screenW Framebuffer width in pixels.
             *  @param screenH Framebuffer height in pixels.
             *  @return True when ImGui is consuming mouse or keyboard input; the caller
             *          should suppress game input in that case. */
            bool OnUpdate(float dt, Camera &cam, int screenW, int screenH);

            /** @brief Renders ImGui panels, selection highlight, and the transform gizmo.
             *
             *  Must be called after the game 3D render pass and before the backend presents the frame.
             *  @param cam     Current camera (used for projection and view-gimbal).
             *  @param screenW Framebuffer width in pixels.
             *  @param screenH Framebuffer height in pixels. */
            void Render(const Camera &cam, int screenW, int screenH);

            /** @brief Replaces the current document with a live-scene snapshot.
             *
             *  Resets undo history and selection.  Called on editor open to synchronise
             *  the editor state with the running scene.
             *  @param doc Snapshot document to load. */
            void LoadDocument(SceneDocument doc);

            /** @brief Handles GLFW file-drop events forwarded from the application.
             *  @param pathCount  Number of dropped file paths.
             *  @param utf8Paths  Array of UTF-8 encoded absolute file paths.
             *  @param dropX      Cursor X in the same coordinate space as ImGui (glfwGetCursorPos).
             *  @param dropY      Cursor Y in the same coordinate space as ImGui. */
            void OnPathsDropped(int pathCount, const char **utf8Paths, float dropX,
                                float dropY);

            /** @brief Maps each Prop row to its runtime mesh entity after a full scene rebuild.
             *
             *  Must be called after the scene is rebuilt so that transform callbacks can
             *  update TransformComponent for runtime-only _eid props.
             *  @param registry The freshly-built ECS registry. */
            void SyncRuntimeEntityIds(const Registry &registry);

            /** @brief Provides a live ECS registry for accurate selection boxes and viewport picking.
             *  @param registry Pointer to the active registry; may be null to disable live queries. */
            void SetLiveRegistry(Registry *registry) { m_liveRegistry = registry; }

            /** @brief Registers a callback invoked when position, scale, or yaw is dragged in the properties panel.
             *
             *  Use this to update the live scene incrementally without triggering a full reload.
             *  Signature: void(const SceneObject& changedObj)
             *  @param cb Callback function; passing an empty function disables live transform updates. */
            void SetTransformCallback(std::function<void(const SceneObject &)> cb) {
                m_transformCb = std::move(cb);
            }

            /** @brief Registers a provider that returns the available script behaviour class names.
             *
             *  Called lazily by the script-component property widget to populate the drop-down.
             *  @param cb Provider callback returning a vector of fully-qualified behaviour names. */
            void SetScriptBehaviorOptionsProvider(
                std::function<std::vector<std::string>()> cb) {
                m_scriptBehaviorOptionsCb = std::move(cb);
            }

            /** @brief Sets the root directory shown in the project-browser panel.
             *  @param root Absolute path to the project root; an empty path disables the project tree. */
            void SetProjectBrowserRoot(std::filesystem::path root);

            /** @brief Overrides the additional filename blocklist used by the project browser.
             *  @param names Set of filenames or directory names to hide in the project tree. */
            void SetProjectBrowserExtraBlocklist(
                const std::unordered_set<std::string, StringHash, std::equal_to<> >& names);

            /** @brief Clears the project-browser directory-listing cache so the next frame re-reads from disk. */
            void InvalidateProjectBrowserCache();

            /** @brief Registers a callback invoked inside the File menu to inject application-specific items.
             *  @param cb Render callback; called once per frame while the File menu is open. */
            void SetFileMenuRenderCallback(std::function<void()> cb) {
                m_fileMenuRenderCallback = std::move(cb);
            }

            /** @brief Registers a callback invoked after all editor panels are drawn to render custom overlays.
             *  @param cb Render callback invoked at the end of each Render() call. */
            void SetOverlayRenderCallback(std::function<void()> cb) {
                m_overlayRenderCallback = std::move(cb);
            }

            /** @brief Immediately serialises the workspace state to disk regardless of dirty flag. */
            void SaveWorkspaceStateNow() { SaveWorkspaceStateIfNeeded(true); }
            /** @brief Reloads the workspace state from disk and applies it to the editor. */
            void ReloadWorkspaceStateFromDisk() { LoadWorkspaceState(); }

            /** @brief Returns true when the editor overlay is currently active (visible). */
            bool IsActive() const { return m_active; }
            /** @brief Returns true when play-in-editor mode is active.
             *  @note The game simulation runs inside the viewport; editor chrome remains visible. */
            bool IsPlayMode() const { return m_playMode; }
            /** @brief Returns true when the editor has queued a scene reload for the caller to process. */
            bool WantsSceneReload() const { return m_wantsReload; }

#ifdef HORO_STANDALONE_UI_AUTOMATION
            /** @brief UI-automation hook that adds a new object of the requested type. */
            void UiAutomationAddObject(SceneObjectType type) { AddObject(type); }
            /** @brief UI-automation hook that selects every object in the current document. */
            void UiAutomationSelectAllObjects() {
                m_selectedIndices.clear();
                for (int index = 0; index < static_cast<int>(m_document.objects.size());
                     ++index)
                    m_selectedIndices.push_back(index);
            }
#endif

            /** @brief Shows or hides the hot-reload progress overlay.
             *
             *  The overlay is rendered regardless of whether the editor is active,
             *  allowing the application to display reload status during transitions.
             *  @param active          True to show the overlay.
             *  @param progress01      Completion fraction in [0, 1].
             *  @param spinnerAngleRad Current spinner rotation in radians.
             *  @param label           Progress label text. */
            void SetHotReloadOverlay(bool active, float progress01, float spinnerAngleRad,
                                     std::string_view label);

            /** @brief Returns the document queued for the pending scene reload.
             *  @note Valid only while WantsSceneReload() returns true. */
            const SceneDocument &GetPendingDocument() const { return m_pendingDoc; }
            /** @brief Clears the reload-pending flag after the caller has processed the pending document. */
            void AcknowledgeReload() { m_wantsReload = false; }
            /** @brief Returns a const reference to the currently active scene document. */
            const SceneDocument &GetDocument() const { return m_document; }
            /** @brief Returns the asset ID of the currently selected asset in the assets panel. */
            const std::string &GetSelectedAssetId() const { return m_selectedAssetId; }

            /** @brief Returns the IDs of all currently selected scene objects.
             *  @return Vector of object ID strings; last element is the primary selection. */
            std::vector<std::string> GetSelectedObjectIds() const;

            /** @brief Dispatches an MCP tool command to the appropriate handler.
             *  @param toolName  Name of the MCP tool to execute.
             *  @param arguments JSON arguments object for the tool.
             *  @return Command result containing a success flag and response payload. */
            Mcp::McpCommandResult ExecuteMcpCommand(std::string_view toolName,
                                                    const nlohmann::json &arguments);

        private:
            /** @brief Action deferred until after the current scene operation completes. */
            enum class PendingSceneAction {
                None,              /**< No pending action. */
                NewScene,          /**< Create a new empty scene. */
                OpenSceneFile,     /**< Open a scene file via native dialog. */
                LoadSceneFromDisk, /**< Reload the scene from a known disk path. */
                ReloadSceneFromDisk,/**< Force-reload the scene from disk. */
                CloseEditor,       /**< Close the editor overlay. */
            };

            /** @brief Native file dialog type deferred to the next Render() tick so GLFW/ImGui is not mid-frame. */
            // Run native file dialogs on the next Render() tick so GLFW/ImGui is not
            // mid-frame.
            enum class DeferredFilePick {
                None,               /**< No file pick pending. */
                ImportObjBulk,      /**< Bulk OBJ import dialog. */
                NewAssetAlbedo,     /**< Albedo texture dialog for a new asset draft. */
                SelectedAssetAlbedo,/**< Albedo texture dialog for the selected asset. */
            };

            DeferredFilePick m_deferredFilePick = DeferredFilePick::None; /**< File dialog deferred from last frame. */

            // Exit confirmation modal state
            PendingSceneAction m_pendingSceneAction = PendingSceneAction::None; /**< Scene action waiting for confirmation. */
            std::string m_exitConfirmError; /**< Error string shown in the exit confirmation modal. */

            /** @brief Executes any file dialog request deferred from the previous frame. */
            void ProcessDeferredFilePicks();

            /** @brief Routes queued OS path-drop payloads to the relevant drop handlers. */
            void ProcessPendingPathDrops();

            /** @brief Applies pending texture drops to asset draft or selected asset targets. */
            void ProcessPendingTextureDrops();

            /** @brief Attempts to assign a dropped texture to the new-asset draft albedo field.
             *  @param path Absolute path of the dropped texture file.
             *  @return True when the drop was accepted and applied.
             */
            bool TryApplyDraftAlbedoDrop(const std::string &path);

            /** @brief Attempts to assign a dropped texture to the selected asset's albedo field.
             *  @param path Absolute path of the dropped texture file.
             *  @return True when the drop was accepted and applied.
             */
            bool TryApplySelectedAssetAlbedoDrop(const std::string &path);

            /** @brief Processes queued OBJ drops and imports accepted assets. */
            void ProcessPendingObjDrops();

            GLFWwindow *m_window = nullptr;           /**< GLFW window owning the render context. */
            bool m_active = false;                    /**< True when the editor overlay is shown. */
            bool m_imguiBackendInitialized = false;   /**< True after InitEditorImGuiBackend succeeds. */
            bool m_playMode = false;                  /**< True when play-in-editor mode is active. */
            int m_playModeEscPresses = 0;             /**< Counts Escape presses used to exit play mode. */
            bool m_wantsReload = false;               /**< True when the caller must process a pending scene reload. */
            bool m_prevMouseL = false;                /**< Previous-frame left-mouse-button state. */
            bool m_prevDel = false;                   /**< Previous-frame Delete key state. */
            bool m_prevCopyRef = false;               /**< Previous-frame copy-ref shortcut state. */
            bool m_prevEsc = false;                   /**< Previous-frame Escape key state. */
            bool m_prevGizmoW = false;                /**< Previous-frame gizmo-translate (W) key state. */
            bool m_prevGizmoE = false;                /**< Previous-frame gizmo-scale (E) key state. */
            bool m_prevGizmoR = false;                /**< Previous-frame gizmo-rotate (R) key state. */
            bool m_closeRequested = false;            /**< True when a confirmed editor-close is pending. */

            // Fly camera
            bool m_flyMode = false;           /**< True when fly-camera mode is active. */
            bool m_flyCamInitialized =
                    false; /**< True after fly yaw/pitch has been initialized from the live camera. */
            float m_flyYaw = 0.0f;            /**< Fly-camera yaw angle in radians. */
            float m_flyPitch = 0.0f;          /**< Fly-camera pitch angle in radians. */
            double m_prevCursorX = 0.0;       /**< Cursor X position from the previous frame (fly-mode mouse delta). */
            double m_prevCursorY = 0.0;       /**< Cursor Y position from the previous frame. */
            bool m_prevCursorInit = false;    /**< True after the fly-mode cursor baseline has been captured. */
            bool m_prevTab = false;           /**< Previous-frame Tab key state (fly-mode toggle). */

            /** @brief Toggles fly-camera mode and synchronizes fly state from the provided camera.
             *  @param cam Camera used to seed fly yaw/pitch when entering fly mode.
             */
            void ToggleFlyMode(const Camera &cam);

            /** @brief Updates fly-camera movement and orientation from input.
             *  @param dt  Frame delta time in seconds.
             *  @param cam Camera updated in place.
             */
            void UpdateFlyCamera(float dt, Camera &cam);

            /** @brief Handles global keyboard shortcuts that are active while the editor is open. */
            void HandleEditorKeyboardShortcuts(const Camera &cam);

            /** @brief Updates fly-camera input and keeps gizmo target state synchronized with selection. */
            void UpdateFlyCameraWithGizmoSync(float dt, Camera &cam);

            /** @brief Handles non-fly camera editor input such as picking and viewport interactions. */
            void UpdateNonFlyModeInput(const Camera &cam, int screenW, int screenH);

            /** @brief Handles gizmo mode hotkeys (translate/rotate/scale toggles). */
            void HandleGizmoModeHotkeys(const ImGuiIO &io);

            // Sets m_currentGizmoMode and drives m_gizmo in one place.
            // If mode is None the gizmo is deactivated; otherwise it is activated
            // for the current primary selection (no-op when there is none).
            /** @brief Requests a new gizmo mode and updates activation state accordingly. */
            void RequestGizmoMode(GizmoMode mode);

            /** @brief Synchronizes gizmo position/orientation with the current primary selection. */
            void SyncGizmoToSelection();

            /** @brief Applies translation snapping rules to a gizmo movement delta.
             *  @param dragAxis Active axis being dragged.
             *  @param primIdx  Index of the primary selected object.
             *  @param dPos     Translation delta updated in place.
             */
            void ApplyGizmoTranslateSnapping(GizmoAxis dragAxis, int primIdx, Vec3 &dPos);

            TransformGizmo m_gizmo;                          /**< Transform gizmo renderer and drag state. */
            GizmoMode m_currentGizmoMode = GizmoMode::None; /**< Active gizmo mode (translate/scale/rotate/none). */

            SceneDocument m_document;          /**< Currently active editable scene document. */
            SceneDocument m_lastSavedDocument; /**< Last document snapshot that was saved to disk (used for dirty detection). */
            SceneDocument m_pendingDoc;        /**< Document queued for the next caller-side scene reload. */

            // Additional scenes shown in the hierarchy panel (secondary; not actively
            // edited). The primary editable scene is always m_document.
            std::vector<SceneDocument> m_additionalScenes; /**< Read-only secondary scenes shown in the hierarchy. */
            EditorSchema m_schema;                          /**< Active schema describing available object fields. */
            AssetImportService m_assetImportService;        /**< Service that manages asset import pipelines. */
            std::vector<int>
            m_selectedIndices; /**< Selected object indices; last element is the primary selection. */
            std::function<void(const SceneObject &)> m_transformCb;       /**< Callback for live transform updates. */
            std::function<std::vector<std::string>()> m_scriptBehaviorOptionsCb; /**< Provider for script behaviour class names. */
            Registry *m_liveRegistry = nullptr; /**< Live ECS registry for picking and selection highlight. */

            // Helpers
            /** @brief Returns true when the object at index @p i is in the selection set. */
            bool IsSelected(int i) const;

            /** @brief Returns the last selected index, or -1 when the selection is empty. */
            int PrimaryIdx() const; // last selected index, or -1 if empty
            /** @brief Adds or removes @p i from the selection; clears other selections when Shift is not held. */
            void ToggleSelect(int i); // add or remove i; clears others if Shift not held
            /** @brief Snapshots the current document into m_pendingDoc and sets m_wantsReload. */
            void TriggerReload(); // snapshot document → pending and set wantsReload
            /** @brief Marks the document dirty and immediately triggers a reload. */
            void MarkDirtyAndReload(); // dirty = true + TriggerReload()

            /** @brief Draws the editor toolbar row and dispatches toolbar actions. */
            void DrawToolbar();

            /** @brief Draws the root docking host window and top-level panel layout. */
            void DrawDockspace();

            /** @brief Draws the viewport panel and its overlays. */
            void DrawViewportPanel(const Camera &cam, int screenW, int screenH);

            /** @brief Draws the orientation view-gimbal in the viewport corner. */
            void DrawViewGimbal(const Camera &cam);

            /** @brief Draws the viewport render target image into the current ImGui region.
             *  @param targetW Width in pixels for the drawn image.
             *  @param targetH Height in pixels for the drawn image.
             *  @return True when the image was drawn.
             */
            bool DrawViewportImage(float targetW, float targetH) const;

            /** @brief Handles drag-drop payloads from the assets panel into the viewport.
             *  @param cam         Camera used to compute placement position.
             *  @param screenW     Viewport width in pixels.
             *  @param screenH     Viewport height in pixels.
             *  @param assetIdText Drag payload asset ID string.
             *  @return True when a drop was accepted and created an object.
             */
            bool HandleViewportAssetDrop(const Camera &cam, int screenW, int screenH,
                                         const char *assetIdText);

            /** @brief Draws the hierarchy/object list panel. */
            void DrawObjectList();

            /** @brief Draws the project browser panel. */
            void DrawProjectPanel();

            /** @brief Draws the "+" popup menu for the project panel (new folder / new file). */
            void DrawProjectAddPopup();

            /** @brief Draws the "⋮" popup menu for the project panel (refresh / collapse all). */
            void DrawProjectMorePopup();

            /** @brief Draws the "Create Project Entry" modal dialog. */
            void DrawProjectCreateModal();

            /** @brief Validates and creates the requested project entry.
             *  Populates m_projectPanelError on failure; clears it on success. */
            void HandleProjectCreateSubmit();

            /** @brief Draws the Favorites and root nodes of the project tree.
             *  @param theme Active editor theme. */
            void DrawProjectTree(const Ui::EditorTheme& theme);

            /** @brief Draws the draggable splitter handles between docked editor panels.
             *  @param io Current ImGui IO snapshot used to read pointer state and drag intent.
             *  @note Must be called once per frame after all panels have been drawn.
             */
            void DrawEditorSplitters(const ImGuiIO &io);

            /** @brief Draws the assets panel in its standalone dock host. */
            void DrawAssetsPanel();
            /** @brief Draws assets panel content without creating an outer window.
             *  Used for bottom-dock embedding.
             */
            void DrawAssetsPanelInline();

            /** @brief Draws the properties panel for object and asset editing. */
            void DrawPropertiesPanel();

            // DrawPropertiesPanel sub-sections
            /** @brief Draws multi-selection batch-edit controls in the properties panel. */
            void DrawPropertiesMultiSelect();

            /** @brief Applies a batch asset assignment to the current multi-selection. */
            void ApplyBatchAssetChange(const std::vector<std::string> &sortedAssetIds);

            /** @brief Applies batched transform edits to all selected objects. */
            void ApplyBatchTransform();

            /** @brief Draws the properties-panel section for the currently selected asset definition.
             *  @param assetIt Iterator pointing to the selected asset in @c m_document.assets.
             */
            void DrawPropertiesSelectedAsset(
                std::unordered_map<std::string, AssetDef, StringHash,
                    std::equal_to<> >::iterator assetIt);

            /** @brief Draws import diagnostics for the currently selected asset. */
            void DrawAssetDiagnosticsSection(const AssetMetadata &metadata) const;

            /** @brief Draws object identity fields (name/id and related metadata). */
            void DrawPropertiesIdentitySection(SceneObject &obj, int primaryIdx);

            /** @brief Draws camera-specific property controls. */
            void DrawPropertiesCameraSection(SceneObject &obj, int primaryIdx);

            /** @brief Draws transform controls (position/rotation/scale). */
            void DrawPropertiesTransformSection(SceneObject &obj, int primaryIdx);

            /** @brief Draws asset binding and import-related controls for an object. */
            void DrawPropertiesAssetSection(SceneObject &obj);

            /** @brief Draws schema-driven custom property fields for an object. */
            void DrawPropertiesSchemaFields(SceneObject &obj);

            /** @brief Draws one schema-defined widget and mutates @p val when edited. */
            void DrawSchemaFieldWidget(const SceneObject &obj, const FieldDef &fd,
                                       std::string &val);

            /** @brief Draws the attached components list and per-component editors. */
            void DrawPropertiesComponentsList(SceneObject &obj);

            /** @brief Draws editable fields for a light component. */
            void DrawLightComponentFields(ComponentDesc &comp);

            /** @brief Draws editable fields for a rigidbody component. */
            void DrawRigidBodyComponentFields(ComponentDesc &comp);

            /** @brief Draws editable fields for a script component. */
            void DrawScriptComponentField(ComponentDesc &comp);

            /** @brief Draws the add-component popup menu container. */
            void DrawPropertiesAddComponentMenu(SceneObject &obj);

            /** @brief Draws schema-backed add-component menu entries. */
            void DrawAddComponentMenuItems(SceneObject &obj);

            /** @brief Draws fallback add-component menu entries when schema data is unavailable. */
            void DrawFallbackAddComponentMenuItems(SceneObject &obj);

            /** @brief Draws and processes command-palette UI state. */
            void DrawCommandPalettePopup();

            /** @brief Draws and processes quick-open UI state. */
            void DrawQuickOpenPopup();

            /** @brief Recursively draws a project tree subtree rooted at @p absPath. */
            void DrawProjectTreeRecursive(const std::filesystem::path &absPath,
                                          const std::filesystem::path &displayRoot);

            /** @brief Returns cached/sorted directory entries for @p absPath, populating cache on miss. */
            const std::vector<std::pair<std::filesystem::path, bool>>*
            GetProjectDirListing(const std::filesystem::path& absPath);

            /** @brief Draws modal dialogs for destructive actions (delete object/asset). */
            void DrawDeleteConfirmModals();

            /** @brief Performs viewport picking and updates selection state. */
            void HandlePicking(const Camera &cam, int screenW, int screenH);

            /** @brief Draws selection highlight overlays for selected objects. */
            void DrawSelectionHighlight();

            /** @brief Draws optional wireframe overlay in the viewport. */
            void DrawWireframeOverlay(const Camera &cam);

            /** @brief Applies any pending view-snap command to the camera. */
            void ApplyPendingViewSnap(Camera &cam);

            /** @brief Loads persisted workspace UI state from disk. */
            void LoadWorkspaceState();

            /** @brief Persists workspace UI state when dirty or when forced.
             *  @param force True to save regardless of dirty tracking.
             */
            void SaveWorkspaceStateIfNeeded(bool force);

            /** @brief Marks workspace state dirty so it will be persisted on next save point. */
            void MarkWorkspaceStateDirty();

            /** @brief Refreshes cached viewport panel bounds used by picking/drop routing. */
            void RefreshViewportPanelRect();

            /** @brief Builds a compact textual reference for the selected object.
             *  @param obj Object to encode.
             *  @param idx Object index in the document.
             *  @return Selection reference string copied by the copy-ref command.
             */
            std::string BuildSelectionRefCode(const SceneObject &obj, int idx) const;

            /** @brief Queues deletion for currently selected objects and opens confirmation UI. */
            void RequestDeleteSelectedObjects();

            /** @brief Queues deletion of a specific asset definition and opens confirmation UI. */
            void RequestDeleteAsset(std::string_view assetId);

            /** @brief Records a pending scene action that may require confirmation flow. */
            void RequestSceneAction(PendingSceneAction action);

            /** @brief Executes the currently pending scene action.
             *  @param outError Optional output error string on failure.
             *  @return True when the action completed successfully.
             */
            bool ExecutePendingSceneAction(std::string *outError);

            /** @brief Executes a command selected from the command palette. */
            void ExecuteCommandPaletteAction(std::string_view commandId);

            /** @brief Creates a prefab file from the current selection.
             *  @param outError      Optional output error string on failure.
             *  @param outPrefabPath Optional output with written prefab path on success.
             *  @return True when prefab creation succeeds.
             */
            bool CreatePrefabFromSelection(std::string *outError = nullptr,
                                           std::string *outPrefabPath = nullptr);

            /** @brief Opens rename UI for the object at @p index. */
            void OpenRenameObjectModal(int index);

            /** @brief Creates and inserts a new object of the requested type. */
            void AddObject(SceneObjectType type, std::string_view parentId = {});

            /** @brief Creates and inserts a Prop object from the selected asset. */
            void AddObjectFromSelectedAsset(std::string_view parentId = {});

            /** @brief Duplicates all selected objects, preserving relative hierarchy where possible. */
            void DuplicateSelectedObjects();

            /** @brief Creates and inserts a Prop object bound to an asset definition.
             *  @param assetId       Asset identifier to instantiate.
             *  @param parentId      Optional parent object ID.
             *  @param worldPosition Optional world position override.
             *  @param preferredId   Optional preferred object ID.
             *  @param outCreated    Optional output with created object snapshot.
             *  @param outError      Optional output error string on failure.
             *  @return True when object creation succeeds.
             */
            bool CreateObjectFromAsset(std::string_view assetId,
                                       std::string_view parentId = {},
                                       const Vec3 *worldPosition = nullptr,
                                       const std::string *preferredId = nullptr,
                                       SceneObject *outCreated = nullptr,
                                       std::string *outError = nullptr);

            /** @brief Computes a world position for viewport drag-drop asset placement.
             *  @param cam        Camera used for ray construction.
             *  @param screenW    Viewport width in pixels.
             *  @param screenH    Viewport height in pixels.
             *  @param assetId    Asset ID being dropped.
             *  @param outPosition Receives computed placement position.
             *  @return True when a valid position was found.
             */
            bool TryBuildViewportDropPosition(const Camera &cam, int screenW, int screenH,
                                              std::string_view assetId,
                                              Vec3 *outPosition) const;

            /** @brief Duplicates only the primary selected object. */
            void DuplicatePrimarySelection();

            /** @brief Pulls and executes queued inbound MCP commands. */
            void ProcessMcpCommands();

            /** @brief Publishes current editor snapshot state to MCP subscribers. */
            void PublishMcpSnapshot();

            /** @brief Saves the current document to its configured path.
             *  @param outError Optional output error string on failure.
             *  @return True when save succeeds.
             */
            bool SaveDocument(std::string *outError);

            /** @brief Reverts the active document to its last-saved baseline. */
            void DiscardUnsavedChanges();

            /** @brief Replaces object selection using an ordered list of object IDs. */
            void SetSelectedObjectIds(const std::vector<std::string> &ids);

            /** @brief Reloads the active scene document from its on-disk path.
             *  @param outError              Optional output error string on failure.
             *  @param preferredSelectionIds Optional object IDs to reselect after reload.
             *  @param preferredAssetId      Optional asset ID to reselect after reload.
             *  @return True when reload succeeds.
             */
            bool ReloadDocumentFromDisk(
                std::string *outError,
                const std::vector<std::string> *preferredSelectionIds = nullptr,
                const std::string *preferredAssetId = nullptr);

            /** @brief Complete editor state captured for undo/redo history. */
            struct EditorHistorySnapshot {
                SceneDocument document;                   /**< Scene document at the time of the snapshot. */
                SceneDocument savedDocument;              /**< Last-saved document baseline for dirty detection. */
                std::vector<std::string> selectedObjectIds;/**< Object IDs that were selected at the time. */
                std::string selectedAssetId;              /**< Selected asset ID at the time. */
            };

            /** @brief Captures the current editor state into a new history snapshot.
             *  @return Snapshot of the current document, selection, and saved baseline. */
            EditorHistorySnapshot CaptureHistorySnapshot() const;

            /** @brief Restores document, selection, and asset selection from a history snapshot.
             *  @param snapshot The snapshot to restore. */
            void RestoreHistorySnapshot(const EditorHistorySnapshot &snapshot);

            /** @brief Commits a history change by comparing @p before with the current state.
             *  @param before Snapshot taken before the operation started. */
            void CommitHistoryChange(const EditorHistorySnapshot &before);

            /** @brief Applies one frame of gizmo delta to selected objects and dependent descendants. */
            void ApplyGizmoDeltaToSelection(const Vec3 &dPos, const Vec3 &dScale,
                                            const Quaternion &dRot, float dRotXYZSq);

            /** @brief Starts a grouped history transaction from a pre-edit snapshot. */
            void BeginHistoryTransaction(const EditorHistorySnapshot &before);

            /** @brief Finalizes an open grouped history transaction. */
            void FinalizeHistoryTransaction();

            /** @brief Clears both undo and redo history stacks. */
            void ClearHistory();

            /** @brief Updates saved-baseline state used by dirty tracking and history comparisons. */
            void RefreshHistorySavedBaseline();

            /** @brief Returns whether an undo operation is currently available. */
            bool CanUndoHistory() const;

            /** @brief Returns whether a redo operation is currently available. */
            bool CanRedoHistory() const;

            /** @brief Applies one undo step.
             *  @return True when undo succeeded.
             */
            bool UndoHistory();

            /** @brief Applies one redo step.
             *  @return True when redo succeeded.
             */
            bool RedoHistory();

            /** @brief Applies a newly loaded document and optionally resets history state. */
            void ApplyLoadedDocument(SceneDocument doc, bool resetHistory);

            /** @brief Returns true when two history snapshots represent identical editor state. */
            static bool HistorySnapshotsEqual(const EditorHistorySnapshot &lhs,
                                              const EditorHistorySnapshot &rhs);

            /** @brief Trims history to the configured maximum snapshot count. */
            static void TrimHistory(std::vector<EditorHistorySnapshot> *history);

            /** @brief Result returned by DeleteAssetDefinition. */
            struct AssetDeleteResult {
                bool ok = false;                      /**< True when the asset was successfully removed. */
                int clearedReferences = 0;            /**< Number of scene-object asset references that were cleared. */
                bool deletedManagedFiles = false;     /**< True when managed asset files were deleted from disk. */
                std::string deletedAssetDirectory;    /**< Path of the deleted asset directory, if any. */
                std::string error;                    /**< Human-readable error description on failure. */
            };

            /** @brief Removes an asset definition and all managed files associated with it.
             *  @param assetId The asset identifier to delete.
             *  @return Result struct describing what was removed and any error. */
            AssetDeleteResult DeleteAssetDefinition(const std::string &assetId);

            // Multi-scene helpers
            /** @brief Creates and appends a new additional scene tab. */
            void AddNewScene();

#ifdef _WIN32
            /** @brief Opens a file dialog and loads an additional scene on Windows. */
            void OpenAdditionalSceneFile();
#else
            /** @brief Opens a file dialog and loads an additional scene on non-Windows platforms. */
            void OpenAdditionalSceneFile() const;
#endif

            /** @brief Closes an additional scene tab by index. */
            void CloseAdditionalScene(int index);

            /** @brief Saves an additional scene tab to disk.
             *  @param index    Additional-scene index.
             *  @param outError Optional output error string on failure.
             *  @return True when save succeeds.
             */
            bool SaveAdditionalScene(int index, std::string *outError);

            /** @brief Draws the header controls for a scene section in the hierarchy panel. */
            void DrawSceneHeader(SceneDocument &doc, bool isPrimary, int additionalIndex);

            /** @brief Draws context-menu actions for a scene header. */
            void DrawSceneHeaderContextMenu(SceneDocument &doc, bool isPrimary,
                                            int additionalIndex);

            /** @brief Handles scene-header drag-and-drop interactions. */
            void DrawSceneHeaderDragDrop(SceneDocument &doc);

            /** @brief Draws hierarchical object tree rows for the given scene. */
            void DrawObjectsTree(SceneDocument &doc, bool isPrimary);

            /** @brief Draws playback controls (play/stop and related state). */
            void DrawPlaybackControls();

            /** @brief Draws high-level scene management controls. */
            void DrawSceneControls();

            // DrawObjectsTree recursive node renderer (extracted from drawNode lambda)
            /** @brief Draws one hierarchy tree node and its descendants. */
            void DrawTreeNode(int idx, SceneDocument &doc, bool isPrimary,
                              int &shownObjectCount,
                              std::vector<std::vector<int> > &children);

            /** @brief Handles click-based selection behavior for a hierarchy tree node. */
            void HandleTreeNodeClickSelection(int idx);

            /** @brief Handles drag-and-drop hierarchy reparenting interactions for a tree node. */
            void HandleTreeNodeDragDrop(int idx, SceneDocument &doc, SceneObject &obj);

            /** @brief Draws search-filtered hierarchy rows while object search mode is active. */
            void DrawObjectsTreeSearchMode(SceneDocument &doc, bool isPrimary,
                                           const std::string &query);

            /** @brief Draws the hierarchy root drop target for unparenting operations. */
            void DrawObjectsTreeRootDropZone();

            /** @brief Draws read-only runtime entities not represented by authored objects. */
            void DrawObjectsTreeRuntimeEntities(const SceneDocument &doc) const;

            bool m_hasPendingPathDrop = false;     /**< True when a GLFW path-drop payload is queued for processing. */
            float m_pendingPathDropX = 0.0f;       /**< Screen-space X coordinate of the queued path drop. */
            float m_pendingPathDropY = 0.0f;       /**< Screen-space Y coordinate of the queued path drop. */
            std::vector<std::string> m_pendingPathDropPaths; /**< Absolute dropped paths queued from GLFW callbacks. */

            // Last-frame screen rect for view axis gizmo (skip scene picking when cursor
            // is here).
            ScreenRectDropZone m_viewGizmoPickRect;

            std::string m_objectSearchQuery;       /**< Current hierarchy/object-list search query text. */

            // Assets panel state
            std::string m_assetDraftId;            /**< Draft asset ID entered in the new-asset form. */
            std::string m_assetDraftGuid;          /**< Draft stable GUID for new asset creation. */
            std::string m_assetDraftDisplayName;   /**< Draft display name for new asset creation. */
            std::string m_assetDraftMesh;          /**< Draft mesh tag/path for new asset creation. */
            std::string m_assetDraftRenderScale = "1.0000,1.0000,1.0000"; /**< Draft render scale text for new asset creation. */
            std::string m_assetDraftAlbedoMap;     /**< Draft albedo map path for new asset creation. */
            std::string m_assetImportError;        /**< Last asset import error shown in assets UI. */
            bool m_openNewAssetHeader = false;     /**< True when the new-asset accordion should be expanded. */
            ScreenRectDropZone m_albedoDraftDrop;  /**< Drop zone for assigning draft asset albedo texture. */
            ScreenRectDropZone m_albedoSelDrop;    /**< Drop zone for assigning selected asset albedo texture. */
            std::string m_selectedAssetId;         /**< Currently selected asset ID in the assets panel. */
            bool m_assetSearchOpen = false;        /**< True while asset-search popup UI is open. */
            std::string m_assetSearchQuery;        /**< Current assets-panel search query text. */

            Vec3 m_batchTranslateDraft = Vec3::Zero(); /**< Draft translation delta for multi-selection batch edit. */
            Vec3 m_batchRotateDraft = Vec3::Zero();    /**< Draft Euler rotation delta for multi-selection batch edit. */
            Vec3 m_batchScaleDraft = Vec3::One();      /**< Draft scale multiplier for multi-selection batch edit. */
            int m_batchAssetChoice = 0;                /**< Selected option index for batch asset assignment UI. */
            bool m_prevHelpToggle = false;             /**< Previous-frame help-toggle shortcut state. */
            EditorHelpPopup m_helpPopup;               /**< Help/shortcuts popup component state. */
            EditorSettingsModal m_settingsModal;       /**< Settings modal component state. */
            EditorUserSettingsDocument m_userSettingsDocument{};
                                                       /**< Persisted user-wide editor preferences (theme preset). */
            [[no_unique_address]] EditorToolbar m_toolbar;                   /**< Toolbar component state and actions. */
            [[no_unique_address]] EditorAssetsPanel m_assetsPanel;           /**< Assets panel component state and renderer. */
            EditorUIWidgets m_uiWidgets;               /**< Shared editor widget helpers. */
            int m_mcpSelectedActivityIndex = 0;        /**< Selected row index in MCP activity UI. */
            bool m_mcpUiClearToggle = false;           /**< Previous-frame MCP clear-history toggle state. */
            bool m_quickOpenOpen = false;              /**< True while quick-open popup is open. */
            bool m_prevQuickOpenToggle = false;        /**< Previous-frame quick-open shortcut state. */
            std::string m_quickOpenQuery;              /**< Current quick-open search query. */
            bool m_commandPaletteOpen = false;         /**< True while command palette popup is open. */
            bool m_prevCommandPaletteToggle = false;   /**< Previous-frame command-palette shortcut state. */
            std::string m_commandPaletteQuery;         /**< Current command palette query string. */
            bool m_prevUndo = false;                   /**< Previous-frame undo shortcut state. */
            bool m_prevRedo = false;                   /**< Previous-frame redo shortcut state. */

            std::filesystem::path m_projectBrowserRoot;      /**< Configured root directory for the project browser tree. */
            bool m_projectBrowserRootValid = false;          /**< True when project browser root exists and is accessible. */
            std::filesystem::path m_projectBrowserCwd;       /**< Current directory shown in project browser. */
            bool m_projectBrowserCwdValid = false;           /**< True when project browser CWD exists and is accessible. */
            std::filesystem::path m_savedProjectBrowserCwd;  /**< Last persisted project-browser working directory. */
            std::unordered_set<std::string, StringHash, std::equal_to<>>
            m_projectExtraBlocklist;                        /**< Extra filename/dirname entries hidden from project browser. */
            bool m_projectPanelCollapseAllRequested = false;/**< One-frame request to collapse all project tree nodes. */
            bool m_projectPanelCreateModalRequested = false;/**< One-frame request to open create-entry modal. */
            bool m_projectPanelCreateFolder = true;         /**< Create modal target type: folder when true, file when false. */
            std::string m_projectPanelCreateName;           /**< Draft name entered in project create modal. */
            std::string m_projectPanelError;                /**< Latest project panel filesystem error message. */
            EditorWorkspaceDocument m_workspaceDocument;    /**< Persisted workspace settings and layout state. */
            bool m_workspaceStateDirty = false;             /**< True when workspace state has unsaved changes. */
            std::function<void()> m_fileMenuRenderCallback; /**< Optional host callback for extra File menu items. */
            std::function<void()> m_overlayRenderCallback;  /**< Optional host callback for end-of-frame overlays. */
            std::string m_imguiIniPath;                     /**< Resolved path to persisted ImGui .ini layout file. */
            bool m_hasPersistedDockLayout = false;          /**< True after a dock layout has been loaded or saved this run. */
            bool m_resetDockLayoutRequested = false;        /**< One-frame request to restore default dock layout. */
            EditorViewportRect m_viewportPanelRect;         /**< Cached viewport panel bounds for hit-testing and drop routing. */
            std::vector<EditorHistorySnapshot> m_undoHistory;/**< Undo stack (oldest to newest). */
            std::vector<EditorHistorySnapshot> m_redoHistory;/**< Redo stack (oldest to newest). */
            bool m_historyTransactionOpen = false;          /**< True while batching changes into one history entry. */
            EditorHistorySnapshot m_historyTransactionBefore;/**< Snapshot captured at history transaction start. */
            bool m_gizmoHistoryPending = false;             /**< True when gizmo drag has uncommitted history changes. */

            // Wireframe overlay
            bool m_wireframeMode = false;                   /**< True when viewport wireframe overlay rendering is enabled. */
            Shader m_wireframeShader;                       /**< Shader used to render wireframe overlay geometry. */

            // Hierarchy range-select anchor
            int m_lastClickedHierarchyIdx = -1;             /**< Last clicked hierarchy index used as shift-range anchor. */

            // Bottom dock component
            EditorBottomDock m_bottomDock;                  /**< Bottom dock component hosting logs/assets/diagnostics. */
            Mcp::McpController m_mcpController;             /**< MCP command transport and state controller. */

            // Panel split state — 0 means "use default on first frame".
            float m_leftDockWidth = 0.0f;        /**< Current width of the left hierarchy/project dock in pixels; 0 = use default. */
            float m_hierarchyHeightRatio = 0.0f; /**< Fraction of the left dock given to the hierarchy section; 0 = use default. */
            float m_bottomDockHeight = 0.0f;     /**< Current height of the bottom dock in pixels; 0 = use default. */
            // -1 = none, 0 = seam A (left dock right edge, EW),
            //            1 = seam B (hierarchy/project NS),
            //            2 = seam C (bottom dock top, NS)
            int m_activeSplitter = -1; /**< Index of the splitter currently being dragged (-1 = none). */

            // Project browser cache and blocklist (used by DrawProjectTreeRecursive)
            /** @brief Cached directory listing for a single path in the project browser. */
            struct ProjectDirCache {
                std::vector<std::pair<std::filesystem::path, bool>> entries; /**< Sorted entries: path and isDirectory flag. */
                uint32_t cachedAtFrame = 0; /**< Frame number when this cache entry was last populated. */
            };

            std::unordered_map<std::string, ProjectDirCache, StringHash, std::equal_to<>>
                m_projectDirCache;                          /**< Frame-local cache of project directory listings. */

            /** @brief Creates a new Prop object initialised from the specified asset definition. */
            static SceneObject MakeObjectFromAsset(const SceneDocument &doc,
                                                   const std::string &assetId,
                                                   const EditorSchema &schema);

            /** @brief Creates a deep duplicate of a scene object with a newly generated ID. */
            static SceneObject DuplicateObject(const SceneDocument &doc,
                                               const SceneObject &src);

            /** @brief Generates a unique non-camera object ID for insertion into @p doc. */
            static std::string GenerateId(const SceneDocument &doc);

            /** @brief Generates a unique camera object ID for insertion into @p doc. */
            static std::string GenerateCameraId(const SceneDocument &doc);

            /** @brief Applies schema default values to object properties that are currently unset. */
            void ApplySchemaDefaults(SceneObject &obj) const;

            /** @brief Applies component schema defaults to component properties that are currently unset. */
            void ApplyComponentSchemaDefaults(ComponentDesc &component) const;

            // ExecuteMcpCommand helpers — shared utilities
            /** @brief Returns the index of the scene object with the given @p id, or -1 when not found. */
            int McpFindObjectIndex(std::string_view id) const;

            /** @brief Returns a JSON summary of @p object suitable for MCP command responses. */
            nlohmann::json McpSummarizeObject(const SceneObject &object) const;

            /** @brief Returns a JSON summary of an asset definition suitable for MCP command responses.
             *  @param assetId Asset identifier string.
             *  @param asset   Asset definition to summarise. */
            nlohmann::json McpSummarizeAsset(const std::string &assetId,
                                             const AssetDef &asset) const;

            // ExecuteMcpCommand helpers — per-tool handlers
            /** @brief MCP handler: select one or more scene objects by ID. */
            Mcp::McpCommandResult McpHandleSelect(const nlohmann::json &arguments);

            /** @brief MCP handler: clear the current object selection. */
            Mcp::McpCommandResult McpHandleClearSelection(const nlohmann::json &);

            /** @brief MCP handler: undo the last history change. */
            Mcp::McpCommandResult McpHandleUndo(const nlohmann::json &);

            /** @brief MCP handler: redo the last undone history change. */
            Mcp::McpCommandResult McpHandleRedo(const nlohmann::json &);

            /** @brief MCP handler: create a new scene object of a specified type. */
            Mcp::McpCommandResult McpHandleCreateObject(const nlohmann::json &arguments);

            /** @brief MCP handler: create a new scene object from a named asset. */
            Mcp::McpCommandResult
            McpHandleCreateObjectFromAsset(const nlohmann::json &arguments);

            /** @brief MCP handler: create a prefab from the current selection. */
            Mcp::McpCommandResult McpHandleCreatePrefab(const nlohmann::json &arguments);

            /** @brief MCP handler: update properties on one or more existing scene objects.
             *  @param arguments JSON arguments specifying IDs and property deltas.
             *  @param toolName  Originating tool name, forwarded for error messages. */
            Mcp::McpCommandResult McpHandleUpdateObject(const nlohmann::json &arguments,
                                                        std::string_view toolName);

            /** @brief MCP handler: rename a scene object. */
            Mcp::McpCommandResult McpHandleRenameObject(const nlohmann::json &arguments);

            /** @brief MCP handler: reparent a scene object to a new parent. */
            Mcp::McpCommandResult
            McpHandleReparentObject(const nlohmann::json &arguments);

            /** @brief MCP handler: duplicate a scene object. */
            Mcp::McpCommandResult McpHandleDuplicate(const nlohmann::json &arguments);

            /** @brief MCP handler: delete one or more scene objects. */
            Mcp::McpCommandResult McpHandleDelete(const nlohmann::json &arguments);

            /** @brief MCP handler: select an asset in the assets panel. */
            Mcp::McpCommandResult McpHandleSelectAsset(const nlohmann::json &arguments);

            /** @brief MCP handler: update properties of an existing asset definition. */
            Mcp::McpCommandResult McpHandleUpdateAsset(const nlohmann::json &arguments);

            /** @brief MCP handler: delete an asset definition and its managed files. */
            Mcp::McpCommandResult McpHandleDeleteAsset(const nlohmann::json &arguments);

            /** @brief MCP handler: create a new empty scene. */
            Mcp::McpCommandResult McpHandleNewScene(const nlohmann::json &arguments);

            /** @brief MCP handler: save the current scene to disk. */
            Mcp::McpCommandResult McpHandleSaveScene(const nlohmann::json &);

            /** @brief MCP handler: reload the scene from disk. */
            Mcp::McpCommandResult McpHandleReloadScene(const nlohmann::json &);
        };
    } // namespace Editor
} // namespace Horo
