/**
 * @file EditorLayerUiPanels.cpp
 * @brief EditorLayer UI panel rendering: dockspace, toolbar, project browser,
 *        assets panel, command palette, quick-open, and splitter logic.
 */
#include "ui/editor/EditorLayer.h"
#include "ui/editor/EditorLayerInternal.h"

// clang-format off
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
// clang-format on
#include <imgui.h>

#include <array>
#include <cstdint>
#include <fstream>
#include <format>
#include <ranges>

#include "core/Logger.h"
#include "renderer/DebugDraw.h"
#include "renderer/Renderer.h"
#include "ui/editor/EditorImportedAssetPathUtils.h"
#include "ui/editor/EditorImGuiBackend.h"
#include "ui/editor/EditorSearch.h"
#include "ui/editor/ProjectEntryFilter.h"
#include "ui/editor/components/EditorComponentContext.h"
#include "ui/IconsFontAwesome6.h"
#include "ui/HoroTheme.h"
#include "ui/UiComponents.h"

namespace Horo::Editor
{
  namespace
  {
    constexpr uint32_t kProjectListingCacheFrames = 48;
  }

  /** @copydoc EditorLayer::Render */
  void EditorLayer::Render(const Camera& cam, int screenW, int screenH)
  {
    ProcessDeferredFilePicks();
    if (!m_active)
    {
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

    if (m_active)
    {
      DrawToolbar();
      DrawDockspace();
      DrawViewportPanel(cam, screenW, screenH);
      DrawObjectList();
      DrawProjectPanel();
      DrawPropertiesPanel();

      {
        const ImGuiIO& io = ImGui::GetIO();
        if (m_bottomDockHeight <= 0.0f)
          m_bottomDockHeight = ComputeEditorBottomDockHeight(io.DisplaySize.y);
        if (m_leftDockWidth <= 0.0f)
          m_leftDockWidth = ComputeEditorLeftDockWidth(io.DisplaySize.x);
        m_bottomDock.Draw(&m_mcpController, m_window, m_leftDockWidth, m_bottomDockHeight);
      }
      DrawEditorSplitters(ImGui::GetIO());
      m_uiWidgets.DrawStatusBar();
      m_helpPopup.Draw();
      DrawCommandPalettePopup();
      DrawQuickOpenPopup();
      m_settingsModal.Draw();
      DrawDeleteConfirmModals();
      m_uiWidgets.DrawExitConfirmModal();
      if (!m_playMode)
      {
        DrawSelectionHighlight();
        if (m_gizmo.IsActive())
          m_gizmo.Draw(cam, screenW, screenH);
      }
    }
    if (m_overlayRenderCallback)
      m_overlayRenderCallback();
    m_uiWidgets.DrawHotReloadOverlay();
    m_uiWidgets.DrawClipboardToast();
    SaveWorkspaceStateIfNeeded(false);

    if (m_active && !m_historyTransactionOpen &&
      m_undoHistory.size() == undoHistorySizeBeforeRender &&
      m_redoHistory.size() == redoHistorySizeBeforeRender)
    {
      CommitHistoryChange(frameHistoryBefore);
    }

    if (m_active && !m_playMode)
      DrawWireframeOverlay(cam);

    DebugDraw::Flush(cam, 2.0f);

    ImGui::Render();
    if (m_imguiBackendInitialized)
      RenderEditorImGuiDrawData(Renderer::GetBackendId(), ImGui::GetDrawData());
  }

  /** @copydoc EditorLayer::DrawDockspace */
  void EditorLayer::DrawDockspace()
  {
    if (!m_resetDockLayoutRequested)
      return;

    ImGui::LoadIniSettingsFromMemory("", 0);
    if (!m_imguiIniPath.empty())
    {
      std::error_code ec;
      std::filesystem::remove(ResolveEditorLayoutPath(), ec);
      ImGui::SaveIniSettingsToDisk(m_imguiIniPath.c_str());
    }
    m_hasPersistedDockLayout = false;
    m_resetDockLayoutRequested = false;
  }

  /** @copydoc EditorLayer::DrawToolbar */
  void EditorLayer::DrawToolbar()
  {
    EditorToolbarCallbacks callbacks;
    EditorToolbarState state;

    // Callbacks for scene actions and object operations
    callbacks.requestSceneAction = [this](const std::string& action)
    {
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
    callbacks.createPrefabFromSelection = [this]()
    {
      if (std::string prefabError; !CreatePrefabFromSelection(&prefabError))
        LogError("[Editor] Create prefab failed: {}", prefabError);
      return true;
    };
    callbacks.duplicateSelectedObjects = [this]() { DuplicateSelectedObjects(); };
    callbacks.requestDeleteSelectedObjects = [this]() { RequestDeleteSelectedObjects(); };
    callbacks.buildSelectionRefCode = [this](int idx)
    {
      if (idx >= 0 && idx < static_cast<int>(m_document.objects.size()))
        return BuildSelectionRefCode(m_document.objects[static_cast<size_t>(idx)], idx);
      return std::string{};
    };

    // View menu callbacks
    callbacks.openHelpPopup = [this]() { m_helpPopup.SetOpen(true); };
    callbacks.openQuickOpen = [this]() { m_quickOpenOpen = true; };
    callbacks.openCommandPalette = [this]()
    {
      m_commandPaletteOpen = true;
      m_commandPaletteQuery.clear();
    };
    callbacks.setFlyMode = [this](bool flyMode)
    {
      if (m_flyMode != flyMode)
      {
        m_flyMode = flyMode;
        m_flyCamInitialized = false;
        m_prevCursorInit = false;
        glfwSetInputMode(m_window, GLFW_CURSOR,
                         m_flyMode ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
      }
    };
    callbacks.setResetDockLayout = [this](bool reset)
    {
      m_resetDockLayoutRequested = reset;
    };

    // Settings callback
    callbacks.openSettings = [this]()
    {
      m_settingsModal.Open(m_mcpController.GetSettings(),
                           m_userSettingsDocument.settings);
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

    // Snapshot mode before Draw so we can detect toolbar button clicks below.
    const GizmoMode prevGizmoMode = m_currentGizmoMode;
    m_toolbar.Draw(callbacks, state);
    // The toolbar writes directly into m_currentGizmoMode via void* pointer.
    // If it changed, drive the real gizmo through RequestGizmoMode so that
    // clicking a toolbar icon activates the gizmo for the current selection.
    if (m_currentGizmoMode != prevGizmoMode)
      RequestGizmoMode(m_currentGizmoMode);
  }

  /** @copydoc EditorLayer::GetProjectDirListing */
  const std::vector<std::pair<std::filesystem::path, bool>>*
  EditorLayer::GetProjectDirListing(const std::filesystem::path& absPath)
  {
    namespace fs = std::filesystem;
    if (std::error_code existsEc;
      !fs::is_directory(absPath, existsEc) || existsEc)
      return nullptr;

    const std::string key = absPath.generic_string();
    const auto frame = static_cast<uint32_t>(ImGui::GetFrameCount());
    if (auto it = m_projectDirCache.find(key); it != m_projectDirCache.end())
    {
      const uint32_t age = frame - it->second.cachedAtFrame;
      if (age < kProjectListingCacheFrames)
        return &it->second.entries;
    }

    std::vector<std::pair<fs::path, bool>> sorted;
    std::error_code ec;
    for (fs::directory_iterator
           dit(absPath, fs::directory_options::skip_permission_denied, ec),
           end;
         !ec && dit != end; dit.increment(ec))
    {
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
    std::ranges::sort(sorted, [](const auto& a, const auto& b)
    {
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

  /** @copydoc EditorLayer::DrawProjectTreeRecursive */
  void EditorLayer::DrawProjectTreeRecursive(
    const std::filesystem::path& absPath,
    const std::filesystem::path& /*displayRoot*/)
  {
    const auto* listing = GetProjectDirListing(absPath);
    if (!listing)
      return;

    const Ui::EditorTheme& theme = Ui::GetEditorTheme();
    const Ui::ScopedEditorTreeRowStyle treeRowStyle(theme);

    static const ImVec4 kFileColor(0.65f, 0.75f, 0.95f, 1.0f);

    for (const auto& [p, isDir] : *listing)
    {
      const std::string name = p.filename().string();

      if (isDir)
      {
        Ui::EditorTreeItemSpec spec;
        spec.label = name.c_str();
        spec.prefixIcon = ICON_FA_FOLDER;
        spec.kind = Ui::EditorTreeItemKind::Node;
        spec.treeFlags = ImGuiTreeNodeFlags_SpanAvailWidth;
        spec.normalTextColor = &theme.palette.text;
        if (m_projectPanelCollapseAllRequested)
          ImGui::SetNextItemOpen(false, ImGuiCond_Always);
        const auto res = Ui::DrawEditorTreeItem(theme, spec);
        if (res.open)
        {
          DrawProjectTreeRecursive(p, absPath);
          ImGui::TreePop();
        }
      }
      else
      {
        Ui::EditorTreeItemSpec spec;
        spec.label = name.c_str();
        spec.prefixIcon = ICON_FA_FILE;
        spec.kind = Ui::EditorTreeItemKind::Leaf;
        spec.normalTextColor = &kFileColor;
        spec.hoveredTextColor = &theme.palette.text;
        Ui::DrawEditorTreeItem(theme, spec);
      }
    }
  }

  /** @copydoc EditorLayer::DrawProjectPanel */
  void EditorLayer::DrawProjectPanel()
  {
    // Note: kEditorToolbarH, kEditorStatusH, kHierarchySectionRatio, and
    // kMainPanelWindowFlags come from EditorLayerInternal.h (shared constants).
    // The Project window does not show a title bar, so add NoTitleBar here.
    constexpr ImGuiWindowFlags kProjectWindowFlags =
      kMainPanelWindowFlags | ImGuiWindowFlags_NoTitleBar;

    const ImGuiIO& io = ImGui::GetIO();
    const float availableH = io.DisplaySize.y - kEditorStatusH - kEditorToolbarH;

    // Lazy-initialise from the compute function on first frame.
    if (m_leftDockWidth <= 0.0f)
      m_leftDockWidth = ComputeEditorLeftDockWidth(io.DisplaySize.x);
    if (m_hierarchyHeightRatio <= 0.0f)
      m_hierarchyHeightRatio = kHierarchySectionRatio;

    const float leftDockW = m_leftDockWidth;
    const float hierarchyHeight = std::max(180.0f, availableH * m_hierarchyHeightRatio);
    const float projectTop = kEditorToolbarH + hierarchyHeight;

    ImGui::SetNextWindowPos(ImVec2(0.0f, projectTop), ImGuiCond_Always);
    ImGui::SetNextWindowSize(
      ImVec2(leftDockW, std::max(180.0f, availableH - hierarchyHeight)),
      ImGuiCond_Always);
    ImGui::Begin("Project", nullptr, kProjectWindowFlags);

    const Ui::EditorTheme& theme = Ui::GetEditorTheme();

    const std::array projectTabs = {
      Ui::EditorPanelTabItem{Ui::EditorPanelTab::Project, true},
    };
    const std::array projectActions = {
      Ui::EditorPanelActionItem{ICON_FA_PLUS},
      Ui::EditorPanelActionItem{ICON_FA_ELLIPSIS_VERTICAL},
    };
    const Ui::EditorPanelTopBarResult topBar = Ui::RenderEditorPanelTopBar(
      theme, "project_topbar",
      projectTabs, projectActions);
    if (topBar.clickedActionIndex == 0)
      ImGui::OpenPopup("##project_add_menu");
    if (topBar.clickedActionIndex == 1)
      ImGui::OpenPopup("##project_panel_menu");

    DrawProjectAddPopup();
    DrawProjectMorePopup();

    if (m_projectPanelCreateModalRequested)
    {
      ImGui::OpenPopup("Create Project Entry");
      m_projectPanelCreateModalRequested = false;
    }

    DrawProjectCreateModal();

    if (!m_projectPanelError.empty() && !ImGui::IsPopupOpen("Create Project Entry"))
    {
      Ui::ErrorText(theme, m_projectPanelError.c_str());
      ImGui::Separator();
    }

    if (!m_projectBrowserRootValid ||
      !std::filesystem::is_directory(m_projectBrowserRoot))
    {
      ImGui::TextDisabled("Set project root to browse files.");
    }
    else
    {
      DrawProjectTree(theme);
    }

    m_projectPanelCollapseAllRequested = false;

    ImGui::End();
  }

  /** @copydoc EditorLayer::DrawProjectAddPopup */
  void EditorLayer::DrawProjectAddPopup()
  {
    if (!ImGui::BeginPopup("##project_add_menu"))
      return;

    if (ImGui::MenuItem("New Folder"))
    {
      m_projectPanelCreateFolder = true;
      m_projectPanelCreateName.clear();
      m_projectPanelError.clear();
      m_projectPanelCreateModalRequested = true;
    }
    if (ImGui::MenuItem("New File"))
    {
      m_projectPanelCreateFolder = false;
      m_projectPanelCreateName.clear();
      m_projectPanelError.clear();
      m_projectPanelCreateModalRequested = true;
    }
    ImGui::EndPopup();
  }

  /** @copydoc EditorLayer::DrawProjectMorePopup */
  void EditorLayer::DrawProjectMorePopup()
  {
    if (!ImGui::BeginPopup("##project_panel_menu"))
      return;

    if (ImGui::MenuItem("Refresh"))
    {
      InvalidateProjectBrowserCache();
      m_projectPanelError.clear();
    }
    if (ImGui::MenuItem("Collapse All"))
      m_projectPanelCollapseAllRequested = true;
    ImGui::EndPopup();
  }

  /** @copydoc EditorLayer::DrawProjectCreateModal */
  void EditorLayer::DrawProjectCreateModal()
  {
    if (!Ui::BeginEditorModal({"Create Project Entry", 400.0f, true}, false))
      return;

    const Ui::EditorTheme& theme = Ui::GetEditorTheme();
    const char* itemKind = m_projectPanelCreateFolder ? "folder" : "file";
    ImGui::Text("Create %s in project root", itemKind);

    std::array<char, 256> inputBuffer{};
    m_projectPanelCreateName.copy(inputBuffer.data(), inputBuffer.size() - 1);
    if (ImGui::IsWindowAppearing())
      ImGui::SetKeyboardFocusHere();
    const bool enterPressed = ImGui::InputTextWithHint(
      "##project_create_name", m_projectPanelCreateFolder
                                 ? "e.g. scripts"
                                 : "e.g. scripts/main.cpp",
      inputBuffer.data(), inputBuffer.size(), ImGuiInputTextFlags_EnterReturnsTrue);
    m_projectPanelCreateName = inputBuffer.data();

    if (!m_projectPanelError.empty())
      Ui::ErrorText(theme, m_projectPanelError.c_str());

    const auto footer = Ui::RenderEditorModalFooter(theme, "Create");
    if (footer.cancelled)
      m_projectPanelError.clear();

    if (enterPressed || footer.confirmed)
      HandleProjectCreateSubmit();

    Ui::EndEditorModal();
  }

  /** @copydoc EditorLayer::HandleProjectCreateSubmit */
  void EditorLayer::HandleProjectCreateSubmit()
  {
    namespace fs = std::filesystem;
    auto fail = [this](std::string msg) { m_projectPanelError = std::move(msg); };

    if (!m_projectBrowserRootValid || !fs::is_directory(m_projectBrowserRoot))
    {
      fail("Project root is unavailable.");
      return;
    }
    if (m_projectPanelCreateName.find_first_not_of(" \t\r\n") == std::string::npos)
    {
      fail("Name cannot be empty.");
      return;
    }

    const fs::path relPath = fs::path(m_projectPanelCreateName).lexically_normal();
    const bool hasParentTraversal = std::ranges::any_of(
      relPath, [](const auto& part) { return part == ".."; });
    if (relPath.empty() || relPath == "." || relPath.is_absolute() || hasParentTraversal)
    {
      fail("Use a relative path inside the project root.");
      return;
    }

    const fs::path candidate = m_projectBrowserRoot / relPath;
    if (!IsPathWithinDirectory(candidate, m_projectBrowserRoot))
    {
      fail("Path must remain inside the project root.");
      return;
    }

    std::error_code ec;
    if (m_projectPanelCreateFolder)
    {
      if (fs::exists(candidate, ec))
        fail("A file or folder with that name already exists.");
      else if (!fs::create_directories(candidate, ec) || ec)
        fail("Failed to create folder.");
    }
    else
    {
      if (const fs::path parent = candidate.parent_path(); !parent.empty())
      {
        fs::create_directories(parent, ec);
        if (ec)
        {
          fail("Failed to create parent folder(s).");
          ec.clear();
        }
      }
      if (m_projectPanelError.empty())
      {
        if (fs::exists(candidate, ec))
        {
          fail("A file or folder with that name already exists.");
        }
        else
        {
          std::ofstream out(candidate, std::ios::out | std::ios::trunc);
          if (!out.good())
            fail("Failed to create file.");
        }
      }
    }

    if (m_projectPanelError.empty())
    {
      InvalidateProjectBrowserCache();
      m_projectPanelCreateName.clear();
      ImGui::CloseCurrentPopup();
    }
  }

  /** @copydoc EditorLayer::DrawProjectTree */
  void EditorLayer::DrawProjectTree(const Ui::EditorTheme& theme)
  {
    const auto& palette = theme.palette;
    ImGui::BeginChild("##project_tree", ImVec2(0, 0), false);

    {
      const ImGuiTreeNodeFlags topFlags =
        ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_OpenOnArrow;
      const Ui::ScopedEditorTreeRowStyle treeRowStyle(theme);

      if (m_projectPanelCollapseAllRequested)
        ImGui::SetNextItemOpen(false, ImGuiCond_Always);
      else
        ImGui::SetNextItemOpen(true, ImGuiCond_Once);

      Ui::EditorTreeItemSpec favSpec;
      favSpec.id = "##project_favorites_node";
      favSpec.label = "Favorites";
      favSpec.prefixIcon = ICON_FA_FOLDER_OPEN;
      favSpec.kind = Ui::EditorTreeItemKind::Node;
      favSpec.treeFlags = topFlags;
      favSpec.normalTextColor = &theme.palette.text;
      if (const auto favRes = Ui::DrawEditorTreeItem(theme, favSpec); favRes.open)
      {
        ImGui::PushStyleColor(ImGuiCol_Text, palette.textMuted);
        constexpr const char* kNoFavoritesText = "No favorites yet.";
        const ImVec2 textSize = ImGui::CalcTextSize(kNoFavoritesText);
        const float minX = ImGui::GetWindowContentRegionMin().x;
        const float maxX = ImGui::GetWindowContentRegionMax().x;
        const float centeredX = minX + (maxX - minX - textSize.x) * 0.5f;
        constexpr float kPlaceholderBlockHeight = 24.0f;
        const float blockStartY = ImGui::GetCursorPosY();
        const float centeredY =
          blockStartY + (kPlaceholderBlockHeight - textSize.y) * 0.5f;
        ImGui::SetCursorPosY(centeredY);
        ImGui::SetCursorPosX(centeredX);
        ImGui::TextUnformatted(kNoFavoritesText);
        ImGui::SetCursorPosY(blockStartY + kPlaceholderBlockHeight);
        ImGui::PopStyleColor();
        ImGui::TreePop();
      }

      std::string rootName = m_projectBrowserRoot.filename().string();
      if (rootName.empty())
        rootName = m_projectBrowserRoot.generic_string();
      if (m_projectPanelCollapseAllRequested)
        ImGui::SetNextItemOpen(false, ImGuiCond_Always);
      else
        ImGui::SetNextItemOpen(true, ImGuiCond_Once);

      Ui::EditorTreeItemSpec rootSpec;
      rootSpec.id = "##project_root_node";
      rootSpec.label = rootName.c_str();
      rootSpec.prefixIcon = ICON_FA_FOLDER;
      rootSpec.kind = Ui::EditorTreeItemKind::Node;
      rootSpec.treeFlags = topFlags;
      rootSpec.normalTextColor = &theme.palette.text;
      if (const auto rootRes = Ui::DrawEditorTreeItem(theme, rootSpec); rootRes.open)
      {
        DrawProjectTreeRecursive(m_projectBrowserRoot, m_projectBrowserRoot);
        ImGui::TreePop();
      }
    }

    ImGui::EndChild();
  }

  /** @copydoc EditorLayer::DrawAssetsPanel */
  void EditorLayer::DrawAssetsPanel()
  {
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
    callbacks.requestDeleteAsset = [this](std::string_view assetId)
    {
      RequestDeleteAsset(assetId);
    };
    callbacks.markDirtyAndReload = [this]()
    {
      MarkDirtyAndReload();
    };
    callbacks.makeObjectFromAsset = [this](const std::string& assetId)
    {
      return MakeObjectFromAsset(m_document, assetId, m_schema);
    };
    callbacks.setDeferredFilePick = [this](int deferredType)
    {
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

  /** @copydoc EditorLayer::DrawAssetsPanelInline */
  void EditorLayer::DrawAssetsPanelInline()
  {
    EditorAssetsPanelCallbacks callbacks;
    callbacks.requestDeleteAsset = [this](std::string_view assetId)
    {
      RequestDeleteAsset(assetId);
    };
    callbacks.markDirtyAndReload = [this]()
    {
      MarkDirtyAndReload();
    };
    callbacks.makeObjectFromAsset = [this](const std::string& assetId)
    {
      return MakeObjectFromAsset(m_document, assetId, m_schema);
    };
    callbacks.setDeferredFilePick = [this](int deferredType)
    {
      m_deferredFilePick = static_cast<DeferredFilePick>(deferredType);
    };

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

    m_assetsPanel.DrawContent(callbacks, state);
  }

  /** @copydoc EditorLayer::DrawCommandPalettePopup */
  void EditorLayer::DrawCommandPalettePopup()
  {
    const Ui::EditorPickerConfig paletteCfg{
      "Command Palette", "Search commands", 520.0f, "Type a command..."
    };

    std::array<char, 256> paletteBuf{};
    m_commandPaletteQuery.copy(paletteBuf.data(), paletteBuf.size() - 1);

    if (!Ui::BeginEditorPickerModal(paletteCfg, m_commandPaletteOpen,
                                    paletteBuf.data(), paletteBuf.size()))
      return;

    m_commandPaletteQuery = paletteBuf.data();

    bool executed = false;
    int shownCount = 0;
    for (const CommandPaletteRow& row : GetEditorCommands())
    {
      if (!MatchesCommandPaletteQuery(row, m_commandPaletteQuery))
        continue;

      if (Ui::EditorPickerModalRow(
        std::format("{}##cmd_{}", row.command, row.id).c_str(), false))
      {
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

    if (executed)
    {
      m_commandPaletteOpen = false;
      ImGui::CloseCurrentPopup();
    }

    Ui::EndEditorPickerModal(m_commandPaletteOpen, &m_commandPaletteQuery);
  }

  /** @copydoc EditorLayer::DrawQuickOpenPopup */
  void EditorLayer::DrawQuickOpenPopup()
  {
    const Ui::EditorPickerConfig quickOpenCfg{
      "Quick Open", "Open object or asset", 520.0f,
      "Type id, type, asset, or mesh..."
    };

    std::array<char, 256> quickOpenBuf{};
    m_quickOpenQuery.copy(quickOpenBuf.data(), quickOpenBuf.size() - 1);

    if (!Ui::BeginEditorPickerModal(quickOpenCfg, m_quickOpenOpen,
                                    quickOpenBuf.data(), quickOpenBuf.size()))
      return;

    m_quickOpenQuery = quickOpenBuf.data();

    bool picked = false;
    int shownCount = 0;

    ImGui::TextDisabled("Objects");
    for (int i = 0; i < static_cast<int>(m_document.objects.size()); ++i)
    {
      const auto& obj = m_document.objects[i];
      const char* typeName = ObjectTypeLabel(obj.type);

      if (!ObjectMatchesQuickOpenQuery(obj, m_quickOpenQuery))
        continue;

      if (Ui::EditorPickerModalRow(
        std::format("Object: {}##quick_open_obj_{}", obj.id, i).c_str(),
        IsSelected(i)))
      {
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
    for (const auto& [assetId, asset] : m_document.assets)
    {
      if (!AssetMatchesQuickOpenQuery(assetId, asset, m_quickOpenQuery))
        continue;

      if (Ui::EditorPickerModalRow(
        std::format("Asset: {}##quick_open_asset_{}", assetId, assetId)
        .c_str(),
        m_selectedAssetId == assetId))
      {
        m_selectedAssetId = assetId;
        picked = true;
      }
      ImGui::SameLine();
      ImGui::TextDisabled("mesh: %s", asset.mesh.c_str());
      ++shownCount;
    }

    if (shownCount == 0)
      ImGui::TextDisabled("No match for '%s'", m_quickOpenQuery.c_str());

    if (picked)
    {
      m_quickOpenOpen = false;
      ImGui::CloseCurrentPopup();
    }

    Ui::EndEditorPickerModal(m_quickOpenOpen, &m_quickOpenQuery);
  }

  /** @copydoc EditorLayer::DrawDeleteConfirmModals */
  void EditorLayer::DrawDeleteConfirmModals()
  {
    m_uiWidgets.DrawConfirmDeleteObjectsModal();
    m_uiWidgets.DrawConfirmDeleteAssetModal();
  }

  /** @copydoc EditorLayer::DrawEditorSplitters */
  void EditorLayer::DrawEditorSplitters(const ImGuiIO& io)
  {
    // Splitter logic using raw mouse position — no overlay window needed.
    // This avoids all ImGui window z-order issues: panel windows drawn before
    // this call would always win focus over an overlay window, making
    // InvisibleButton hit-testing unreliable. Instead we test io.MousePos
    // directly and manage drag state with m_activeSplitter.
    //
    //   Seam A — right edge of the left dock (EW)
    //   Seam B — bottom of Hierarchy / top of Project (NS, left dock only)
    //   Seam C — top of the bottom dock (NS, right of left dock to edge)
    constexpr float kHalfThick = 5.0f; // half hit-area in px
    constexpr float kMinLeftDock = 180.0f;
    constexpr float kMaxLeftDockRatio = 0.35f;
    constexpr float kMinHierarchyRatio = 0.20f;
    constexpr float kMaxHierarchyRatio = 0.80f;
    constexpr float kMinBottomDock = 120.0f;
    constexpr float kMaxBottomDockRatio = 0.60f;

    const float displayW = io.DisplaySize.x;
    const float displayH = io.DisplaySize.y;
    const float availableH = displayH - kEditorStatusH - kEditorToolbarH;

    // Ensure stored values are initialised before computing seam positions.
    if (m_leftDockWidth <= 0.0f) m_leftDockWidth = ComputeEditorLeftDockWidth(displayW);
    if (m_hierarchyHeightRatio <= 0.0f) m_hierarchyHeightRatio = kHierarchySectionRatio;
    if (m_bottomDockHeight <= 0.0f) m_bottomDockHeight = ComputeEditorBottomDockHeight(displayH);

    const float hierarchyH = std::max(180.0f, availableH * m_hierarchyHeightRatio);
    const float seamAx = m_leftDockWidth;
    const float seamBy = kEditorToolbarH + hierarchyH;
    const float seamCy = displayH - kEditorStatusH - m_bottomDockHeight;

    const ImVec2 mouse = io.MousePos;

    // Seam A: EW seam spanning full panel height (toolbar → bottom dock top)
    const bool hoverA = (mouse.x >= seamAx - kHalfThick && mouse.x <= seamAx + kHalfThick &&
      mouse.y >= kEditorToolbarH && mouse.y <= seamCy);
    // Seam B: NS seam spanning left dock width only
    const bool hoverB = (mouse.x >= 0.0f && mouse.x <= seamAx &&
      mouse.y >= seamBy - kHalfThick && mouse.y <= seamBy + kHalfThick);
    // Seam C: NS seam spanning from left dock to right edge
    const bool hoverC = (mouse.x >= seamAx && mouse.x <= displayW &&
      mouse.y >= seamCy - kHalfThick && mouse.y <= seamCy + kHalfThick);

    if (io.MouseClicked[0])
    {
      if (hoverA) m_activeSplitter = 0;
      else if (hoverB) m_activeSplitter = 1;
      else if (hoverC) m_activeSplitter = 2;
      // Don't clear here — a click elsewhere is handled by the release below.
    }
    if (!io.MouseDown[0])
      m_activeSplitter = -1;

    if (m_activeSplitter == 0)
    {
      m_leftDockWidth = std::clamp(
        m_leftDockWidth + io.MouseDelta.x,
        kMinLeftDock, displayW * kMaxLeftDockRatio);
    }
    else if (m_activeSplitter == 1)
    {
      const float rawH = hierarchyH + io.MouseDelta.y;
      m_hierarchyHeightRatio = std::clamp(
        rawH / availableH, kMinHierarchyRatio, kMaxHierarchyRatio);
    }
    else if (m_activeSplitter == 2)
    {
      m_bottomDockHeight = std::clamp(
        m_bottomDockHeight - io.MouseDelta.y,
        kMinBottomDock, availableH * kMaxBottomDockRatio);
    }

    if (hoverA || m_activeSplitter == 0)
      ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    else if (hoverB || hoverC || m_activeSplitter == 1 || m_activeSplitter == 2)
      ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
  }
} // namespace Horo::Editor
