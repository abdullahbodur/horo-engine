#pragma once
#include <filesystem>
#include <functional>
#include <string>
#include <unordered_set>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/LogBuffer.h"
#include "AssetImportService.h"
#include "EditorSchema.h"
#include "EditorWorkspaceSettings.h"
#include "SceneDocument.h"
#include "mcp/McpController.h"
#include "renderer/Camera.h"

struct GLFWwindow;

namespace Monolith {

class Registry;

namespace Editor {

// EditorLayer — backend-mode MCP bridge.
//
// Processes incoming MCP tool calls each frame and publishes scene snapshots
// to connected horo-studio clients. No ImGui rendering.
//
// Typical usage:
//   editor.Init(window);
//   editor.SetLiveRegistry(&scene.registry);
//   editor.SetTransformCallback([&](const SceneObject& obj) { ... });
//   // each frame:
//   editor.OnUpdate(dt, cam, screenW, screenH);
//   // on shutdown:
//   editor.Shutdown();
class EditorLayer {
 public:
  struct LauncherCallbacks {
    std::function<bool(const std::filesystem::path&, std::string*)> openProject;
    std::function<bool(const std::string&, const std::filesystem::path&, std::string*)> createProject;
    std::function<void()> closeProject;
    std::function<bool()> hasProject;
    std::function<std::string()> getProjectPath;
    std::function<std::string()> getProjectName;
  };
  void SetLauncherCallbacks(LauncherCallbacks callbacks) {
    m_launcherCallbacks = std::move(callbacks);
  }

  void Init(GLFWwindow* window);
  void Shutdown();

  // Process MCP commands and publish scene snapshot. Call once per frame.
  void OnUpdate(float dt, Camera& cam, int screenW, int screenH);

  // Replace the current document with a live-scene snapshot.
  void LoadDocument(SceneDocument doc);

  // After scene rebuild, map each Prop row to its mesh entity.
  void SyncRuntimeEntityIds(Registry& registry);

  // Live ECS for accurate selection boxes / picking via MCP.
  void SetLiveRegistry(Registry* registry) { m_liveRegistry = registry; }

  // Called when position/scale/yaw changes via MCP.
  void SetTransformCallback(std::function<void(const SceneObject&)> cb) {
    m_transformCallback = std::move(cb);
  }

  void SetProjectBrowserRoot(std::filesystem::path root);

  // Pending scene reload requested via MCP.
  bool WantsSceneReload() const;
  void ClearPendingReload();
  SceneDocument GetPendingDocument() const;

  // Access to current document (for project bridge).
  const SceneDocument& GetCurrentDocument() const { return m_document; }
  SceneDocument& GetCurrentDocument() { return m_document; }
  // Alias for tests and callers that use the old name.
  const SceneDocument& GetDocument() const { return m_document; }
  SceneDocument& GetDocument() { return m_document; }

  // Selection accessors (public for tests and external MCP integrations).
  std::vector<std::string> GetSelectedObjectIds() const;
  const std::string& GetSelectedAssetId() const { return m_selectedAssetId; }

  // For MCP snapshot publication.
  void PublishMcpSnapshot();

  // Direct MCP command execution (used by tests and external integrations).
  Mcp::McpCommandResult ExecuteMcpCommand(const std::string& toolName,
                                          const nlohmann::json& arguments);

  Mcp::McpController& GetMcpController() { return m_mcpController; }
  const Mcp::McpController& GetMcpController() const { return m_mcpController; }

  // ---- Backward-compatibility stubs (lean backend-mode equivalents) ----

  // Always true: backend EditorLayer is always active.
  bool IsActive() const { return true; }
  // Always false: no play-in-editor mode in backend bridge.
  bool IsPlayMode() const { return false; }
  // Alias for ClearPendingReload() — kept for callers not yet migrated.
  void AcknowledgeReload() { ClearPendingReload(); }

 private:
  void ProcessMcpCommands();

  // ---- Internal helpers used by ProcessMcpCommands / ExecuteMcpCommand ----
  bool IsSelected(int i) const;
  int PrimaryIdx() const;
  void ToggleSelect(int i);
  void TriggerReload();

  void SetSelectedObjectIds(const std::vector<std::string>& ids);

  void ApplyLoadedDocument(SceneDocument doc, bool resetHistory);

  bool SaveDocument(std::string* outError);
  bool ReloadDocumentFromDisk(std::string* outError,
                              const std::vector<std::string>* preferredSelectionIds = nullptr,
                              const std::string* preferredAssetId = nullptr);

  bool CreateObjectFromAsset(const std::string& assetId,
                             const std::string& parentId = {},
                             const Vec3* worldPosition = nullptr,
                             const std::string* preferredId = nullptr,
                             SceneObject* outCreated = nullptr,
                             std::string* outError = nullptr);
  bool CreatePrefabFromSelection(std::string* outError = nullptr,
                                 std::string* outPrefabPath = nullptr);

  void AddNewScene();
  void ApplySchemaDefaults(SceneObject& obj) const;
  void ApplyComponentSchemaDefaults(ComponentDesc& component) const;

  static SceneObject MakeObjectFromAsset(const SceneDocument& doc,
                                         const std::string& assetId,
                                         const EditorSchema& schema);
  static SceneObject DuplicateObject(const SceneDocument& doc, const SceneObject& src);
  static std::string GenerateId(const SceneDocument& doc);
  static std::string GenerateCameraId(const SceneDocument& doc);

  // ---- History ----
  struct EditorHistorySnapshot {
    SceneDocument document;
    SceneDocument savedDocument;
    std::vector<std::string> selectedObjectIds;
    std::string selectedAssetId;
  };
  EditorHistorySnapshot CaptureHistorySnapshot() const;
  void RestoreHistorySnapshot(const EditorHistorySnapshot& snapshot);
  void CommitHistoryChange(const EditorHistorySnapshot& before);
  void BeginHistoryTransaction(const EditorHistorySnapshot& before);
  void FinalizeHistoryTransaction();
  void ClearHistory();
  void RefreshHistorySavedBaseline();
  bool CanUndoHistory() const;
  bool CanRedoHistory() const;
  bool UndoHistory();
  bool RedoHistory();
  static bool HistorySnapshotsEqual(const EditorHistorySnapshot& lhs,
                                    const EditorHistorySnapshot& rhs);
  static void TrimHistory(std::vector<EditorHistorySnapshot>* history);

  struct AssetDeleteResult {
    bool ok = false;
    int clearedReferences = 0;
    bool deletedManagedFiles = false;
    std::string deletedAssetDirectory;
    std::string error;
  };
  AssetDeleteResult DeleteAssetDefinition(const std::string& assetId);

  // ---- State ----
  LauncherCallbacks m_launcherCallbacks;
  SceneDocument m_document;
  SceneDocument m_lastSavedDocument;
  SceneDocument m_pendingDocument;
  EditorSchema m_schema;
  std::vector<int> m_selectedIndices;
  std::string m_selectedAssetId;
  Registry* m_liveRegistry = nullptr;
  std::function<void(const SceneObject&)> m_transformCallback;
  std::filesystem::path m_projectBrowserRoot;
  bool m_projectBrowserRootValid = false;
  std::filesystem::path m_projectBrowserCwd;
  bool m_projectBrowserCwdValid = false;
  std::filesystem::path m_savedProjectBrowserCwd;
  Mcp::McpController m_mcpController;
  GLFWwindow* m_window = nullptr;

  bool m_wantsSceneReload = false;
  bool m_active = true;
  bool m_playMode = false;

  // ---- History state ----
  std::vector<EditorHistorySnapshot> m_undoHistory;
  std::vector<EditorHistorySnapshot> m_redoHistory;
  bool m_historyTransactionOpen = false;
  EditorHistorySnapshot m_historyTransactionBefore;
};

}  // namespace Editor
}  // namespace Monolith
