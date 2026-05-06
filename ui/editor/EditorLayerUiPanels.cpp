#include "ui/editor/EditorLayer.h"

// clang-format off
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
// clang-format on
#include <imgui.h>

#include <cstdint>
#include <format>
#include <ranges>

#include "core/Logger.h"
#include "renderer/DebugDraw.h"
#include "renderer/Renderer.h"
#include "ui/editor/EditorImGuiBackend.h"
#include "ui/editor/EditorSearch.h"
#include "ui/editor/ProjectEntryFilter.h"
#include "ui/editor/components/EditorComponentContext.h"

namespace Horo::Editor {
namespace {
constexpr uint32_t kProjectListingCacheFrames = 48;
}

void EditorLayer::Render(const Camera &cam, int screenW, int screenH) {
  ProcessDeferredFilePicks();
  if (!m_active) {
    m_albedoDraftDrop.Clear();
    m_albedoSelDrop.Clear();
    m_viewGizmoPickRect.Clear();
    m_viewportPanelRect = {};
  }
  m_viewGizmoPickRect.Clear();
  ProcessPendingPathDrops();

  if (m_imguiBackendInitialized)
    BeginEditorImGuiFrame(Renderer::GetBackendId());
  ImGui::NewFrame();

  const EditorHistorySnapshot frameHistoryBefore =
      m_active ? CaptureHistorySnapshot() : EditorHistorySnapshot{};
  const size_t undoHistorySizeBeforeRender = m_undoHistory.size();
  const size_t redoHistorySizeBeforeRender = m_redoHistory.size();

  if (m_active) {
    DrawToolbar();
    DrawDockspace();
    DrawViewportPanel(cam, screenW, screenH);
    DrawObjectList();
    DrawAssetsPanel();
    DrawPropertiesPanel();
    m_bottomDock.Draw(&m_mcpController, m_window);
    m_uiWidgets.DrawStatusBar();
    m_helpPopup.Draw();
    DrawCommandPalettePopup();
    DrawQuickOpenPopup();
    m_settingsModal.Draw();
    DrawDeleteConfirmModals();
    m_uiWidgets.DrawExitConfirmModal();
    if (!m_playMode) {
      DrawSelectionHighlight(); // queues to DebugDraw
      if (m_gizmo.IsActive())
        m_gizmo.Draw(cam, screenW, screenH); // queues to DebugDraw
    }
  }
  if (m_overlayRenderCallback)
    m_overlayRenderCallback();
  m_uiWidgets.DrawHotReloadOverlay();
  m_uiWidgets.DrawClipboardToast();
  SaveWorkspaceStateIfNeeded(false);

  if (m_active && !m_historyTransactionOpen &&
      m_undoHistory.size() == undoHistorySizeBeforeRender &&
      m_redoHistory.size() == redoHistorySizeBeforeRender) {
    CommitHistoryChange(frameHistoryBefore);
  }

  // Wireframe pass: clears solid scene and draws edges; must happen before
  // DebugDraw::Flush so selection highlight/gizmo render on top.
  if (m_active && !m_playMode)
    DrawWireframeOverlay(cam);

  // Flush any queued debug primitives (selection box, gizmo, etc.) before ImGui
  DebugDraw::Flush(cam);

  ImGui::Render();
  if (m_imguiBackendInitialized)
    RenderEditorImGuiDrawData(Renderer::GetBackendId(), ImGui::GetDrawData());
}

void EditorLayer::DrawDockspace() {
  if (!m_resetDockLayoutRequested)
    return;

  ImGui::LoadIniSettingsFromMemory("", 0);
  if (!m_imguiIniPath.empty()) {
    std::error_code ec;
    std::filesystem::remove(ResolveEditorLayoutPath(), ec);
    ImGui::SaveIniSettingsToDisk(m_imguiIniPath.c_str());
  }
  m_hasPersistedDockLayout = false;
  m_resetDockLayoutRequested = false;
}

void EditorLayer::DrawToolbar() {
    EditorToolbarCallbacks callbacks;
    EditorToolbarState state;

    // Callbacks for scene actions and object operations
    callbacks.requestSceneAction = [this](std::string action) {
        if (action == "NewScene")
            RequestSceneAction(PendingSceneAction::NewScene);
        else if (action == "OpenSceneFile")
            RequestSceneAction(PendingSceneAction::OpenSceneFile);
    };

    callbacks.addObject = [this](SceneObjectType type) { AddObject(type); };
    callbacks.addObjectFromSelectedAsset = [this]() { AddObjectFromSelectedAsset(); };

    // Edit callbacks
    callbacks.canUndoHistory = [this]() { return CanUndoHistory(); };
    callbacks.canRedoHistory = [this]() { return CanRedoHistory(); };
    callbacks.undoHistory = [this]() { UndoHistory(); };
    callbacks.redoHistory = [this]() { RedoHistory(); };
    callbacks.openRenameObjectModal = [this](int idx) { OpenRenameObjectModal(idx); };
    callbacks.createPrefabFromSelection = [this]() {
        std::string prefabError;
        if (!CreatePrefabFromSelection(&prefabError))
            LogError("[Editor] Create prefab failed: {}", prefabError);
        return true;
    };
    callbacks.duplicateSelectedObjects = [this]() { DuplicateSelectedObjects(); };
    callbacks.requestDeleteSelectedObjects = [this]() { RequestDeleteSelectedObjects(); };
    callbacks.buildSelectionRefCode = [this](int idx) {
        if (idx >= 0 && idx < static_cast<int>(m_document.objects.size()))
            return BuildSelectionRefCode(m_document.objects[static_cast<size_t>(idx)], idx);
        return std::string{};
    };

    // View menu callbacks
    callbacks.openHelpPopup = [this]() { m_helpPopup.SetOpen(true); };
    callbacks.openQuickOpen = [this]() { m_quickOpenOpen = true; };
    callbacks.openCommandPalette = [this]() {
        m_commandPaletteOpen = true;
        m_commandPaletteQuery.clear();
    };
    callbacks.setFlyMode = [this](bool flyMode) {
        if (m_flyMode != flyMode) {
            m_flyMode = flyMode;
            m_flyCamInitialized = false;
            m_prevCursorInit = false;
            glfwSetInputMode(m_window, GLFW_CURSOR,
                             m_flyMode ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
        }
    };
    callbacks.setResetDockLayout = [this](bool reset) {
        m_resetDockLayoutRequested = reset;
    };

    // Settings callback
    callbacks.openSettings = [this]() {
        m_settingsModal.SetOpen(true);
        *m_settingsModal.GetDraft() = m_mcpController.GetSettings();
        m_settingsModal.GetError()->clear();
    };

    // File menu custom callback
    callbacks.fileMenuRenderCallback = m_fileMenuRenderCallback;

    // State pointers
    state.playMode = &m_playMode;
    state.playModeEscPresses = &m_playModeEscPresses;
    state.flyMode = &m_flyMode;
    state.flyCamInitialized = &m_flyCamInitialized;
    state.prevCursorInit = &m_prevCursorInit;
    state.quickOpenOpen = &m_quickOpenOpen;
    state.commandPaletteOpen = &m_commandPaletteOpen;
    state.commandPaletteQuery = &m_commandPaletteQuery;
    state.currentGizmoMode = &m_currentGizmoMode;
    state.wireframeMode = &m_wireframeMode;
    state.resetDockLayoutRequested = &m_resetDockLayoutRequested;
    state.window = m_window;
    state.selectedIndices = &m_selectedIndices;
    state.selectedAssetId = &m_selectedAssetId;

    m_toolbar.Draw(callbacks, state);
}

const std::vector<std::pair<std::filesystem::path, bool>> *
EditorLayer::GetProjectDirListing(const std::filesystem::path &absPath) {
  namespace fs = std::filesystem;
  if (std::error_code existsEc;
      !fs::is_directory(absPath, existsEc) || existsEc)
    return nullptr;

  const std::string key = absPath.generic_string();
  const auto frame = static_cast<uint32_t>(ImGui::GetFrameCount());
  if (auto it = m_projectDirCache.find(key); it != m_projectDirCache.end()) {
    const uint32_t age = frame - it->second.cachedAtFrame;
    if (age < kProjectListingCacheFrames)
      return &it->second.entries;
  }

  std::vector<std::pair<fs::path, bool>> sorted;
  std::error_code ec;
  for (fs::directory_iterator
           dit(absPath, fs::directory_options::skip_permission_denied, ec),
       end;
       !ec && dit != end; dit.increment(ec)) {
    const fs::directory_entry ent = *dit;
    const std::string name = ent.path().filename().string();
    if (Editor::IsHiddenDotEntry(name))
      continue;
    std::error_code typEc;
    const bool isDir = ent.is_directory(typEc);
    if (isDir && !typEc &&
        Editor::IsBlockedProjectDirName(name, &m_projectExtraBlocklist))
      continue;
    sorted.emplace_back(ent.path(), isDir && !typEc);
  }
  std::ranges::sort(sorted, [](const auto &a, const auto &b) {
    if (a.second != b.second)
      return a.second && !b.second;
    return a.first.filename() < b.first.filename();
  });

  ProjectDirCache slot;
  slot.cachedAtFrame = frame;
  slot.entries = std::move(sorted);
  m_projectDirCache[key] = std::move(slot);
  return &m_projectDirCache.at(key).entries;
}

void EditorLayer::DrawProjectTreeRecursive(
    const std::filesystem::path &absPath,
    const std::filesystem::path & /*displayRoot*/) {
  namespace fs = std::filesystem;
  const auto *listing = GetProjectDirListing(absPath);
  if (!listing)
    return;

  for (const auto &[p, isDir] : *listing) {
    const std::string name = p.filename().string();
    if (isDir) {
      if (ImGui::TreeNodeEx(name.c_str(), 0)) {
        DrawProjectTreeRecursive(p, absPath);
        ImGui::TreePop();
      }
    } else {
      ImGui::BulletText("%s", name.c_str());
    }
  }
}

void EditorLayer::DrawAssetsPanel() {
    // Build component context
    EditorComponentContext ctx;
    ctx.document = &m_document;
    ctx.lastSavedDocument = &m_lastSavedDocument;
    ctx.schema = &m_schema;
    ctx.selectedIndices = &m_selectedIndices;
    ctx.selectedAssetId = &m_selectedAssetId;
    ctx.assetImportService = &m_assetImportService;
    ctx.liveRegistry = m_liveRegistry;

    // Build callbacks
    EditorAssetsPanelCallbacks callbacks;
    callbacks.requestDeleteAsset = [this](std::string_view assetId) {
        RequestDeleteAsset(assetId);
    };
    callbacks.markDirtyAndReload = [this]() {
        MarkDirtyAndReload();
    };
    callbacks.makeObjectFromAsset = [this](const std::string& assetId) {
        return MakeObjectFromAsset(m_document, assetId, m_schema);
    };
    callbacks.setDeferredFilePick = [this](int deferredType) {
        m_deferredFilePick = static_cast<DeferredFilePick>(deferredType);
    };

    // Build state
    EditorAssetsPanelState state;
    state.selectedAssetId = &m_selectedAssetId;
    state.selectedIndices = &m_selectedIndices;
    state.assetDraftId = &m_assetDraftId;
    state.assetDraftGuid = &m_assetDraftGuid;
    state.assetDraftDisplayName = &m_assetDraftDisplayName;
    state.assetDraftMesh = &m_assetDraftMesh;
    state.assetDraftRenderScale = &m_assetDraftRenderScale;
    state.assetDraftAlbedoMap = &m_assetDraftAlbedoMap;
    state.assetImportError = &m_assetImportError;
    state.openNewAssetHeader = &m_openNewAssetHeader;
    state.albedoDraftDrop = &m_albedoDraftDrop;
    state.albedoSelDrop = &m_albedoSelDrop;
    state.assetSearchOpen = &m_assetSearchOpen;
    state.assetSearchQuery = &m_assetSearchQuery;
    state.document = &m_document;
    state.assetImportService = &m_assetImportService;
    state.liveRegistry = m_liveRegistry;

    m_assetsPanel.Draw(ctx, callbacks, state);
}

void EditorLayer::DrawCommandPalettePopup() {
  if (m_commandPaletteOpen) {
    ImGui::SetNextWindowSize(ImVec2(520.0f, 0.0f), ImGuiCond_Appearing);
    if (!ImGui::IsPopupOpen("Command Palette"))
      ImGui::OpenPopup("Command Palette");
  }

  if (!ImGui::BeginPopupModal("Command Palette", nullptr,
                              ImGuiWindowFlags_AlwaysAutoResize))
    return;

  ImGui::TextDisabled("Search commands");
  ImGui::SetNextItemWidth(480.0f);
  std::string queryBuf(256, '\0');
  m_commandPaletteQuery.copy(queryBuf.data(), queryBuf.size() - 1);
  if (ImGui::InputTextWithHint("##command_palette_input", "Type a command...",
                               queryBuf.data(), queryBuf.size())) {
    m_commandPaletteQuery = queryBuf.data();
  }

  ImGui::Separator();
  bool executed = false;
  int shownCount = 0;
  for (const CommandPaletteRow &row : GetEditorCommands()) {
    if (!MatchesCommandPaletteQuery(row, m_commandPaletteQuery))
      continue;

    if (const auto label = std::format("{}##cmd_{}", row.command, row.id);
        ImGui::Selectable(label.c_str(), false)) {
      ExecuteCommandPaletteAction(row.id);
      executed = true;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%s", row.keys);
    ++shownCount;
  }

  if (shownCount == 0)
    ImGui::TextDisabled("No command matches '%s'",
                        m_commandPaletteQuery.c_str());

  if (executed || ImGui::Button("Close") ||
      ImGui::IsKeyPressed(ImGuiKey_Escape)) {
    m_commandPaletteOpen = false;
    ImGui::CloseCurrentPopup();
  }

  ImGui::EndPopup();
}

void EditorLayer::DrawQuickOpenPopup() {
  if (m_quickOpenOpen) {
    ImGui::SetNextWindowSize(ImVec2(560.0f, 0.0f), ImGuiCond_Appearing);
    if (!ImGui::IsPopupOpen("Quick Open"))
      ImGui::OpenPopup("Quick Open");
  }

  if (!ImGui::BeginPopupModal("Quick Open", nullptr,
                              ImGuiWindowFlags_AlwaysAutoResize))
    return;

  ImGui::TextDisabled("Open object or asset");
  ImGui::SetNextItemWidth(520.0f);
  std::string queryBuf(256, '\0');
  m_quickOpenQuery.copy(queryBuf.data(), queryBuf.size() - 1);
  if (ImGui::InputTextWithHint("##quick_open_input",
                               "Type id, type, asset, or mesh...",
                               queryBuf.data(), queryBuf.size()))
    m_quickOpenQuery = queryBuf.data();

  ImGui::Separator();

  bool picked = false;
  int shownCount = 0;

  ImGui::TextDisabled("Objects");
  for (int i = 0; i < static_cast<int>(m_document.objects.size()); ++i) {
    const auto &obj = m_document.objects[i];
    const char *typeName = ObjectTypeLabel(obj.type);

    if (!ObjectMatchesQuickOpenQuery(obj, m_quickOpenQuery))
      continue;

    if (const auto label =
            std::format("Object: {}##quick_open_obj_{}", obj.id, i);
        ImGui::Selectable(label.c_str(), IsSelected(i))) {
      m_selectedIndices = {i};
      picked = true;
    }
    ImGui::SameLine();
    if (obj.assetId.empty())
      ImGui::TextDisabled("type: %s", typeName);
    else
      ImGui::TextDisabled("type: %s  |  asset: %s", typeName,
                          obj.assetId.c_str());
    ++shownCount;
  }

  ImGui::Spacing();
  ImGui::TextDisabled("Assets");
  for (const auto &[assetId, asset] : m_document.assets) {
    if (!AssetMatchesQuickOpenQuery(assetId, asset, m_quickOpenQuery))
      continue;

    if (const auto label =
            std::format("Asset: {}##quick_open_asset_{}", assetId, assetId);
        ImGui::Selectable(label.c_str(), m_selectedAssetId == assetId)) {
      m_selectedAssetId = assetId;
      picked = true;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("mesh: %s", asset.mesh.c_str());
    ++shownCount;
  }

  if (shownCount == 0)
    ImGui::TextDisabled("No match for '%s'", m_quickOpenQuery.c_str());

  if (picked || ImGui::Button("Close") ||
      ImGui::IsKeyPressed(ImGuiKey_Escape)) {
    m_quickOpenOpen = false;
    ImGui::CloseCurrentPopup();
  }

  ImGui::EndPopup();
}

void EditorLayer::DrawDeleteConfirmModals() {
  m_uiWidgets.DrawConfirmDeleteObjectsModal();
  m_uiWidgets.DrawConfirmDeleteAssetModal();
}

} // namespace Horo::Editor
