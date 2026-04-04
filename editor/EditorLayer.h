#pragma once
#include <functional>
#include <string>
#include <vector>

#include "editor/EditorSchema.h"
#include "editor/SceneDocument.h"
#include "renderer/Camera.h"

struct GLFWwindow;

namespace Monolith {

class Registry;

namespace Editor {

// In-game editor overlay.
//
// Usage (from CharacterApp):
//   OnInit  → editor.Init(window)
//   OnUpdate→ if F10 pressed: editor.Toggle()
//              editor.OnUpdate(dt, cam, w, h)  — returns true if ImGui consumed input
//   OnRender→ editor.Render(cam)               — call after game scene, before EndFrame
//   OnShutdown → editor.Shutdown()
//   Reload  → if editor.WantsSceneReload(): reload from editor.GetPendingDocument()
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

  // Render ImGui panels and the selection highlight.
  // Must be called after the game 3D render and before RenderContext::EndFrame.
  void Render(const Camera& cam);

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

  bool IsActive() const { return m_active; }
  bool WantsSceneReload() const { return m_wantsReload; }

  // Development overlay shown regardless of editor active state.
  void SetHotReloadOverlay(bool active, float progress01, float spinnerAngleRad, const std::string& label);

  // The document queued for reload.  Valid only while WantsSceneReload() == true.
  const SceneDocument& GetPendingDocument() const { return m_pendingDoc; }
  void AcknowledgeReload() { m_wantsReload = false; }

 private:
  enum class ViewSnap { None, Top, Bottom, Left, Right, Front, Back };

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
  bool m_wantsReload = false;
  bool m_prevMouseL = false;
  bool m_prevDel = false;
  bool m_prevCopyRef = false;
  bool m_prevEsc = false;
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

  SceneDocument m_document;
  SceneDocument m_lastSavedDocument;
  SceneDocument m_pendingDoc;
  EditorSchema m_schema;
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
  void DrawViewGimbal(const Camera& cam);
  void DrawHotReloadOverlay();
  void DrawClipboardToast();
  void DrawObjectList();
  void DrawAssetsPanel();
  void DrawPropertiesPanel();
  void DrawHelpPopup();
  void DrawQuickOpenPopup();
  void DrawStatusBar();
  void DrawDeleteConfirmModals();
  void DrawExitConfirmModal();
  void HandlePicking(const Camera& cam, int screenW, int screenH);
  void DrawSelectionHighlight();
  void ApplyPendingViewSnap(Camera& cam);
  std::string BuildSelectionRefCode(const SceneObject& obj, int idx) const;
  void RequestDeleteSelectedObjects();
  void RequestDeleteAsset(const std::string& assetId);
  void OpenRenameObjectModal(int index);
  void AddObject(SceneObjectType type, const std::string& parentId = {});
  void AddObjectFromSelectedAsset(const std::string& parentId = {});
  void DuplicatePrimarySelection();
  bool SaveDocument(std::string* outError);
  void DiscardUnsavedChanges();

  bool m_hotReloadOverlayActive = false;
  float m_hotReloadOverlayProgress = 0.0f;
  float m_hotReloadOverlaySpinner = 0.0f;
  std::string m_hotReloadOverlayLabel;
  float m_clipboardToastTime = 0.0f;
  std::string m_clipboardToastLabel;

  std::string m_assetDraftId;
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
  bool m_helpOpen = false;
  bool m_prevHelpToggle = false;
  std::string m_helpSearchQuery;
  bool m_quickOpenOpen = false;
  bool m_prevQuickOpenToggle = false;
  std::string m_quickOpenQuery;
  bool m_confirmDeleteObjectsOpen = false;
  bool m_confirmDeleteAssetOpen = false;
  bool m_confirmExitOpen = false;
  std::vector<int> m_pendingDeleteObjectIndices;
  std::string m_pendingDeleteAssetId;
  std::string m_exitConfirmError;
  bool m_renameObjectOpen = false;
  int m_renameObjectIndex = -1;
  std::string m_renameObjectDraft;
  std::string m_renameObjectError;

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
