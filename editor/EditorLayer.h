#pragma once
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <unordered_set>
#include <vector>

#include <imgui.h>
#include <nlohmann/json.hpp>

#include "core/LogBuffer.h"
#include "editor/AssetImportService.h"
#include "editor/EditorSchema.h"
#include "editor/EditorUiLogic.h"
#include "editor/EditorWorkspaceSettings.h"
#include "editor/SceneDocument.h"
#include "editor/TransformGizmo.h"
#include "mcp/McpController.h"
#include "renderer/Camera.h"
#include "renderer/Shader.h"

struct GLFWwindow;

namespace Monolith {

class Registry;

namespace Editor {

// In-game editor overlay.
//
// Typical usage (from a CharacterApp subclass):
//
//   // main():
//   app.ParseArgs(argc, argv);   // --editor / --play / Debug default
//   app.Run();
//
//   // OnInit():
//   editor.Init(window);
//   editor.SetProjectBrowserRoot(gameProjectRoot);  // optional file tree root
//   editor.SetLiveRegistry(&scene.registry);
//   editor.SetTransformCallback(...);
//   // ...load scene...
//   if (ShouldStartWithEditor()) {
//     editor.LoadDocument(doc);
//     editor.SyncRuntimeEntityIds(registry);
//     editor.Toggle();
//   }
//
//   // OnUpdate():
//   editor.OnUpdate(dt, cam, w, h);
//   // When editor active: if (!editor.IsPlayMode()) suppress game; else run game + chrome
//
//   // OnRender():
//   editor.Render(cam, w, h);   // call after game 3-D scene, before EndFrame
//
//   // OnShutdown():
//   editor.Shutdown();
//
//   // Scene reload:
//   if (editor.WantsSceneReload()) { reload from editor.GetPendingDocument(); }
class EditorLayer {
 public:
  void Init(GLFWwindow* window);
  void Shutdown();

  // Toggle editor mode and update cursor accordingly.
  void Toggle();

  // Process input / picking for this frame.
  // In fly mode the camera position/target are updated directly.
  // Returns true when ImGui is consuming mouse or keyboard input
  // (caller should suppress game input in that case).
  bool OnUpdate(float dt, Camera& cam, int screenW, int screenH);

  // Render ImGui panels, selection highlight, and transform gizmo.
  // Must be called after the game 3D render and before RenderContext::EndFrame.
  void Render(const Camera& cam, int screenW, int screenH);

  // Replace the current document with a live-scene snapshot (called on editor open).
  void LoadDocument(SceneDocument doc);

  // GLFW file drop (UTF-8 paths). dropX/dropY = cursor in same coordinates as ImGui (glfwGetCursorPos).
  void OnPathsDropped(int pathCount, const char** utf8Paths, float dropX, float dropY);

  // After a full scene rebuild, map each Prop row to its mesh entity so transform
  // callbacks can update TransformComponent (runtime-only _eid props).
  void SyncRuntimeEntityIds(Registry& registry);

  // Live ECS for accurate selection boxes / picking while the editor is open.
  void SetLiveRegistry(Registry* registry) { m_liveRegistry = registry; }

  // Called every time position/scale/yaw is dragged in the properties panel.
  // Use to update the live scene without a full Apply/reload.
  // Signature: void(const SceneObject& changedObj)
  void SetTransformCallback(std::function<void(const SceneObject&)> cb) {
    m_transformCb = std::move(cb);
  }

  void SetScriptBehaviorOptionsProvider(std::function<std::vector<std::string>()> cb) {
    m_scriptBehaviorOptionsCb = std::move(cb);
  }

  // Absolute or empty (disables Project tab tree).
  void SetProjectBrowserRoot(std::filesystem::path root);
  void SetProjectBrowserExtraBlocklist(std::unordered_set<std::string> names);

  bool IsActive() const { return m_active; }
  // Play-in-editor: game sim runs in viewport; chrome (hierarchy, dock) stays.
  bool IsPlayMode() const { return m_playMode; }
  bool WantsSceneReload() const { return m_wantsReload; }

  // Development overlay shown regardless of editor active state.
  void SetHotReloadOverlay(bool active, float progress01, float spinnerAngleRad, const std::string& label);

  // The document queued for reload.  Valid only while WantsSceneReload() == true.
  const SceneDocument& GetPendingDocument() const { return m_pendingDoc; }
  void AcknowledgeReload() { m_wantsReload = false; }
  const SceneDocument& GetDocument() const { return m_document; }
  const std::string& GetSelectedAssetId() const { return m_selectedAssetId; }
  std::vector<std::string> GetSelectedObjectIds() const;
  Mcp::McpCommandResult ExecuteMcpCommand(const std::string& toolName,
                                         const nlohmann::json& arguments);

 private:
  enum class ViewSnap { None, Top, Bottom, Left, Right, Front, Back };
  enum class PendingSceneAction {
    None,
    NewScene,
    OpenSceneFile,
    LoadSceneFromDisk,
    ReloadSceneFromDisk,
    CloseEditor,
  };

  // Run native file dialogs on the next Render() tick so GLFW/ImGui is not mid-frame.
  enum class DeferredFilePick {
    None,
    ImportObjBulk,
    NewAssetAlbedo,
    SelectedAssetAlbedo,
  };
  DeferredFilePick m_deferredFilePick = DeferredFilePick::None;
  void ProcessDeferredFilePicks();
  void ProcessPendingPathDrops();

  GLFWwindow* m_window = nullptr;
  bool m_active = false;
  bool m_playMode = false;
  bool m_wantsReload = false;
  bool m_prevMouseL = false;
  bool m_prevDel = false;
  bool m_prevCopyRef = false;
  bool m_prevEsc = false;
  bool m_prevGizmoW = false;
  bool m_prevGizmoE = false;
  bool m_prevGizmoR = false;
  bool m_closeRequested = false;

  // Fly camera
  bool m_flyMode = false;
  bool m_flyCamInitialized = false;  // yaw/pitch synced from live cam on first frame
  float m_flyYaw = 0.0f;
  float m_flyPitch = 0.0f;
  double m_prevCursorX = 0.0;
  double m_prevCursorY = 0.0;
  bool m_prevCursorInit = false;
  bool m_prevTab = false;
  ViewSnap m_pendingViewSnap = ViewSnap::None;

  void ToggleFlyMode(Camera& cam);
  void UpdateFlyCamera(float dt, Camera& cam);

  TransformGizmo m_gizmo;

  SceneDocument m_document;
  SceneDocument m_lastSavedDocument;
  SceneDocument m_pendingDoc;

  // Additional scenes shown in the hierarchy panel (secondary; not actively edited).
  // The primary editable scene is always m_document.
  std::vector<SceneDocument> m_additionalScenes;
  EditorSchema m_schema;
  AssetImportService m_assetImportService;
  std::vector<int> m_selectedIndices;  // all selected; last = primary for properties
  std::function<void(const SceneObject&)> m_transformCb;
  std::function<std::vector<std::string>()> m_scriptBehaviorOptionsCb;
  Registry* m_liveRegistry = nullptr;

  // Helpers
  bool IsSelected(int i) const;
  int PrimaryIdx() const;    // last selected index, or -1 if empty
  void ToggleSelect(int i);  // add or remove i; clears others if Shift not held
  void TriggerReload();      // snapshot document → pending and set wantsReload

  void DrawToolbar();
  void DrawDockspace();
  void DrawViewportPanel(const Camera& cam, int screenW, int screenH);
  void DrawViewGimbal(const Camera& cam);
  void DrawHotReloadOverlay();
  void DrawClipboardToast();
  void DrawObjectList();
  void DrawAssetsPanel();
  void DrawPropertiesPanel();
  void DrawHelpPopup();
  void DrawCommandPalettePopup();
  void DrawQuickOpenPopup();
  void DrawStatusBar();
  void DrawBottomDock();
  void DrawMcpTab();
  void DrawProjectTreeRecursive(const std::filesystem::path& absPath,
                                const std::filesystem::path& displayRoot);
  void InvalidateProjectBrowserCache();
  const std::vector<std::pair<std::filesystem::path, bool>>* GetProjectDirListing(
      const std::filesystem::path& absPath);
  void DrawDeleteConfirmModals();
  void DrawExitConfirmModal();
  void DrawSettingsModal();
  void HandlePicking(const Camera& cam, int screenW, int screenH);
  void DrawSelectionHighlight();
  void DrawWireframeOverlay(const Camera& cam);
  void ApplyPendingViewSnap(Camera& cam);
  void LoadWorkspaceState();
  void SaveWorkspaceStateIfNeeded(bool force);
  void MarkWorkspaceStateDirty();
  void RefreshViewportPanelRect();
  std::string BuildSelectionRefCode(const SceneObject& obj, int idx) const;
  void RequestDeleteSelectedObjects();
  void RequestDeleteAsset(const std::string& assetId);
  void RequestSceneAction(PendingSceneAction action);
  bool ExecutePendingSceneAction(std::string* outError);
  void ExecuteCommandPaletteAction(const std::string& commandId);
  void OpenRenameObjectModal(int index);
  void AddObject(SceneObjectType type, const std::string& parentId = {});
  void AddObjectFromSelectedAsset(const std::string& parentId = {});
  void DuplicateSelectedObjects();
  bool CreateObjectFromAsset(const std::string& assetId,
                             const std::string& parentId = {},
                             const Vec3* worldPosition = nullptr,
                             const std::string* preferredId = nullptr,
                             SceneObject* outCreated = nullptr,
                             std::string* outError = nullptr);
  bool TryBuildViewportDropPosition(const Camera& cam,
                                    int screenW,
                                    int screenH,
                                    const std::string& assetId,
                                    Vec3* outPosition) const;
  void DuplicatePrimarySelection();
  void ProcessMcpCommands();
  void PublishMcpSnapshot();
  bool SaveDocument(std::string* outError);
  void DiscardUnsavedChanges();
  void SetSelectedObjectIds(const std::vector<std::string>& ids);
  bool ReloadDocumentFromDisk(std::string* outError,
                              const std::vector<std::string>* preferredSelectionIds = nullptr,
                              const std::string* preferredAssetId = nullptr);
  struct AssetDeleteResult {
    bool ok = false;
    int clearedReferences = 0;
    bool deletedManagedFiles = false;
    std::string deletedAssetDirectory;
    std::string error;
  };
  AssetDeleteResult DeleteAssetDefinition(const std::string& assetId);

  // Multi-scene helpers
  void AddNewScene();
  void OpenAdditionalSceneFile();
  void CloseAdditionalScene(int index);
  bool SaveAdditionalScene(int index, std::string* outError);
  void DrawSceneHeader(SceneDocument& doc, bool isPrimary, int additionalIndex);
  void DrawObjectsTree(SceneDocument& doc, bool isPrimary);

  bool m_hotReloadOverlayActive = false;
  float m_hotReloadOverlayProgress = 0.0f;
  float m_hotReloadOverlaySpinner = 0.0f;
  std::string m_hotReloadOverlayLabel;
  float m_clipboardToastTime = 0.0f;
  std::string m_clipboardToastLabel;

  std::string m_assetDraftId;
  std::string m_assetDraftGuid;
  std::string m_assetDraftDisplayName;
  std::string m_assetDraftMesh;
  std::string m_assetDraftRenderScale = "1.0000,1.0000,1.0000";
  std::string m_assetDraftAlbedoMap;
  std::string m_assetImportError;
  bool m_openNewAssetHeader = false;

  bool m_hasPendingPathDrop = false;
  float m_pendingPathDropX = 0.0f;
  float m_pendingPathDropY = 0.0f;
  std::vector<std::string> m_pendingPathDropPaths;

  // Last-frame ImGui screen rects for albedo texture drops (Assets panel).
  struct ScreenRectDropZone {
    bool valid = false;
    float minX = 0.0f;
    float minY = 0.0f;
    float maxX = 0.0f;
    float maxY = 0.0f;
    void Clear() noexcept { valid = false; }
    bool Contains(float x, float y, float paddingPx) const noexcept {
      if (!valid)
        return false;
      const float p = paddingPx;
      return x >= minX - p && x <= maxX + p && y >= minY - p && y <= maxY + p;
    }
  };
  ScreenRectDropZone m_albedoDraftDrop;
  ScreenRectDropZone m_albedoSelDrop;

  // Last-frame screen rect for view axis gizmo (skip scene picking when cursor is here).
  ScreenRectDropZone m_viewGizmoPickRect;

  std::string m_selectedAssetId;
  bool m_assetSearchOpen = false;
  std::string m_assetSearchQuery;
  std::string m_objectSearchQuery;
  Vec3 m_batchTranslateDraft = Vec3::Zero();
  Vec3 m_batchRotateDraft = Vec3::Zero();
  Vec3 m_batchScaleDraft = Vec3::One();
  int m_batchAssetChoice = 0;
  bool m_helpOpen = false;
  bool m_prevHelpToggle = false;
  std::string m_helpSearchQuery;
  bool m_settingsOpen = false;
  Mcp::McpSettings m_mcpSettingsDraft;
  std::string m_mcpSettingsError;
  int m_mcpSelectedActivityIndex = 0;
  bool m_quickOpenOpen = false;
  bool m_prevQuickOpenToggle = false;
  std::string m_quickOpenQuery;
  bool m_commandPaletteOpen = false;
  bool m_prevCommandPaletteToggle = false;
  std::string m_commandPaletteQuery;
  bool m_confirmDeleteObjectsOpen = false;
  bool m_confirmDeleteAssetOpen = false;
  bool m_confirmExitOpen = false;
  std::vector<int> m_pendingDeleteObjectIndices;
  std::string m_pendingDeleteAssetId;
  std::string m_pendingDeleteAssetError;
  std::string m_exitConfirmError;
  PendingSceneAction m_pendingSceneAction = PendingSceneAction::None;
  bool m_renameObjectOpen = false;
  int m_renameObjectIndex = -1;
  std::string m_renameObjectDraft;
  std::string m_renameObjectError;

  std::filesystem::path m_projectBrowserRoot;
  bool m_projectBrowserRootValid = false;
  std::filesystem::path m_projectBrowserCwd;
  bool m_projectBrowserCwdValid = false;
  std::filesystem::path m_savedProjectBrowserCwd;
  std::unordered_set<std::string> m_projectExtraBlocklist;
  EditorWorkspaceDocument m_workspaceDocument;
  bool m_workspaceStateDirty = false;
  std::string m_imguiIniPath;
  bool m_hasPersistedDockLayout = false;
  bool m_resetDockLayoutRequested = false;
  EditorViewportRect m_viewportPanelRect;

  // Wireframe overlay
  bool m_wireframeMode = false;
  Shader m_wireframeShader;

  // Hierarchy range-select anchor
  int m_lastClickedHierarchyIdx = -1;

  bool m_consoleShowInfo = true;
  bool m_consoleShowWarn = true;
  bool m_consoleShowError = true;

  struct ProjectDirCache {
    std::vector<std::pair<std::filesystem::path, bool>> entries;
    uint32_t cachedAtFrame = 0;
  };
  std::unordered_map<std::string, ProjectDirCache> m_projectDirCache;
  std::vector<LogLine> m_consoleLinesCache;
  std::vector<int> m_consoleVisibleScratch;
  uint64_t m_consoleLogRevision = UINT64_MAX;
  Mcp::McpController m_mcpController;

  static SceneObject MakeObjectFromAsset(const SceneDocument& doc,
                                         const std::string& assetId,
                                         const EditorSchema& schema);
  static SceneObject DuplicateObject(const SceneDocument& doc, const SceneObject& src);

  static std::string GenerateId(const SceneDocument& doc);
  static std::string GenerateCameraId(const SceneDocument& doc);
  void ApplySchemaDefaults(SceneObject& obj) const;
};

}  // namespace Editor
}  // namespace Monolith
