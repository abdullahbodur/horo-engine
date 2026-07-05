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
#include <fstream>
#include <format>
#include <limits>

#include "ui/IconsFontAwesome6.h"

#include "core/Logger.h"
#include "renderer/DebugDraw.h"
#include "renderer/Renderer.h"
#include "ui/editor/EditorImportedAssetPathUtils.h"
#include "ui/editor/EditorImGuiBackend.h"
#include "ui/editor/EditorSearch.h"
#include "ui/editor/ProjectEntryFilter.h"
#include "ui/editor/components/EditorComponentContext.h"
#include "ui/HoroTheme.h"
#include "ui/UiComponents.h"

namespace Horo::Editor
{
  namespace
  {
    constexpr uint32_t kProjectListingCacheFrames = 48;

    std::string CreateProjectFolder(const std::filesystem::path& candidate)
    {
      namespace fs = std::filesystem;
      std::error_code ec;
      if (fs::exists(candidate, ec))
        return "A file or folder with that name already exists.";
      if (!fs::create_directories(candidate, ec) || ec)
        return "Failed to create folder.";
      return {};
    }

    std::string CreateProjectFile(const std::filesystem::path& candidate)
    {
      namespace fs = std::filesystem;
      std::error_code ec;
      if (const fs::path parent = candidate.parent_path(); !parent.empty())
      {
        fs::create_directories(parent, ec);
        if (ec)
          return "Failed to create parent folder(s).";
      }
      if (fs::exists(candidate, ec))
        return "A file or folder with that name already exists.";
      if (std::ofstream out(candidate, std::ios::out | std::ios::trunc);
          !out.good())
        return "Failed to create file.";
      return {};
    }
  }

  /** @copydoc EditorLayer::Render */
  void EditorLayer::Render(const Camera& cam, int screenW, int screenH)
  {
    ProcessDeferredFilePicks();
    if (!m_active)
    {
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

    if (m_active && m_viewportNavActive)
    {
      ImGuiIO& io = ImGui::GetIO();
      const float hiddenMouse = -std::numeric_limits<float>::max();
      io.MousePos = ImVec2(hiddenMouse, hiddenMouse);
      for (bool& mouseDown : io.MouseDown)
        mouseDown = false;
    }

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
      m_importAssetModal.Draw();
      m_buildPipelineModal.Draw();
      m_buildPipelineModal.PollBuilds();
      ProcessImportAssetModalRequest();
      PollAsyncImport();
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
    DrawSnackbar();
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

    if (m_beforeImGuiRenderCallback)
      m_beforeImGuiRenderCallback();

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
    callbacks.requestSceneAction = [this](std::string_view action)
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
    callbacks.setResetDockLayout = [this](bool reset)
    {
      m_resetDockLayoutRequested = reset;
    };

    // Settings / modal callbacks
    callbacks.openSettings = [this]()
    {
      m_settingsModal.Open(m_mcpController.GetSettings(),
                           m_userSettingsDocument.settings,
                           m_toolchainStore);
    };
    callbacks.openBuildPipeline = [this]()
    {
      OnMenuBuildPipeline();
    };

    // File menu custom callback
    callbacks.fileMenuRenderCallback = m_fileMenuRenderCallback;

    // State pointers
    state.playMode = &m_playMode;
    state.playModeEscPresses = &m_playModeEscPresses;
    state.viewportNavActive = &m_viewportNavActive;
    state.viewportNavCameraInitialized = &m_viewportNavCameraInitialized;
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

  /** @copydoc EditorLayer::RequestProjectTreeAddMenuForDirectory */
  void EditorLayer::RequestProjectTreeAddMenuForDirectory(
    const std::filesystem::path& directoryPath)
  {
    std::string relPath = directoryPath.lexically_relative(m_projectBrowserRoot).generic_string();
    if (!relPath.empty() && relPath != ".")
      relPath += "/";
    else
      relPath.clear();

    m_projectPanelCreateBasePath = relPath;
    m_projectPanelAddMenuRequested = true;
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

      if (!isDir)
      {
        Ui::EditorTreeItemSpec spec;
        spec.label = name.c_str();
        spec.prefixIcon = ICON_FA_FILE;
        spec.kind = Ui::EditorTreeItemKind::Leaf;
        spec.normalTextColor = &kFileColor;
        spec.hoveredTextColor = &theme.palette.text;
        Ui::DrawEditorTreeItem(theme, spec);
        continue;
      }

      Ui::EditorTreeItemSpec spec;
      spec.label = name.c_str();
      spec.prefixIcon = ICON_FA_FOLDER;
      spec.kind = Ui::EditorTreeItemKind::Node;
      spec.treeFlags = ImGuiTreeNodeFlags_SpanAvailWidth;
      spec.normalTextColor = &theme.palette.text;
      if (m_projectPanelCollapseAllRequested)
        ImGui::SetNextItemOpen(false, ImGuiCond_Always);
      const auto res = Ui::DrawEditorTreeItem(theme, spec);

      ImGui::PushID(p.string().c_str());
      if (ImGui::IsItemHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Right))
        RequestProjectTreeAddMenuForDirectory(p);
      ImGui::PopID();

      if (res.open)
      {
        DrawProjectTreeRecursive(p, absPath);
        ImGui::TreePop();
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
      Ui::EditorPanelTabItem{Ui::EditorPanelTab::Project,   m_projectPanelTab == Ui::EditorPanelTab::Project},
      Ui::EditorPanelTabItem{Ui::EditorPanelTab::Favorites, m_projectPanelTab == Ui::EditorPanelTab::Favorites},
    };
    const std::array projectActions = {
      Ui::EditorPanelActionItem{ICON_FA_PLUS},
      Ui::EditorPanelActionItem{ICON_FA_ELLIPSIS_VERTICAL},
    };
    const Ui::EditorPanelTopBarResult topBar = Ui::RenderEditorPanelTopBar(
      theme, "project_topbar",
      projectTabs, projectActions);
    if (topBar.clickedActionIndex == 0)
    {
      m_projectPanelCreateBasePath = "";
      m_projectPanelAddMenuRequested = true;
    }
    if (topBar.clickedActionIndex == 1)
      ImGui::OpenPopup("##project_panel_menu");
    if (m_projectPanelAddMenuRequested)
    {
      ImGui::OpenPopup("##project_add_menu");
      m_projectPanelAddMenuRequested = false;
    }
    if (topBar.clickedTabIndex >= 0)
    {
      if (topBar.clickedTabIndex == 0)
        m_projectPanelTab = Ui::EditorPanelTab::Project;
      else if (topBar.clickedTabIndex == 1)
        m_projectPanelTab = Ui::EditorPanelTab::Favorites;
    }

    DrawProjectAddPopup();
    DrawProjectMorePopup();

    if (m_projectPanelCreateModalRequested)
    {
      ImGui::OpenPopup("Create Project Entry");
      m_projectPanelCreateModalRequested = false;
    }

    if (m_projectPanelCreateScriptModalRequested)
    {
      ImGui::OpenPopup("Create Template File");
      m_projectPanelCreateScriptModalRequested = false;
    }

    DrawProjectCreateModal();
    DrawProjectCreateScriptModal();

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
    else if (m_projectPanelTab == Ui::EditorPanelTab::Favorites)
    {
      DrawProjectFavoritesTree(theme);
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

    DrawProjectAddMenuItems(m_projectPanelCreateBasePath);

    ImGui::EndPopup();
  }

  void EditorLayer::DrawProjectAddMenuItems(const std::string& prefillPath)
  {
    const Ui::EditorTheme& theme = Ui::GetEditorTheme();
    ImGui::PushStyleColor(ImGuiCol_Text, theme.palette.text);

    if (ImGui::MenuItem((std::string(ICON_FA_FOLDER) + "  New Folder").c_str()))
    {
      m_projectPanelCreateFolder = true;
      m_projectPanelCreateBasePath = prefillPath;
      m_projectPanelCreateName.clear();
      m_projectPanelError.clear();
      m_projectPanelCreateModalRequested = true;
    }

    if (ImGui::BeginMenu((std::string(ICON_FA_FILE) + "  New File").c_str()))
    {
      // Search Box
      std::array<char, 256> searchBuf{};
      m_projectPanelTemplateSearchText.copy(searchBuf.data(), searchBuf.size() - 1);
      
      ImGui::PushItemWidth(200.0f);
      if (ImGui::InputTextWithHint("##template_search", (std::string(ICON_FA_MAGNIFYING_GLASS) + " Search templates...").c_str(), searchBuf.data(), searchBuf.size())) {
          m_projectPanelTemplateSearchText = searchBuf.data();
      }
      ImGui::PopItemWidth();
      ImGui::Separator();

      // Helper to draw template items
      auto drawTemplate = [&](const char* icon, const char* name, ScriptTemplateType type) {
          std::string lowerName = Horo::ToLowerAscii(name);
          if (std::string lowerQuery = Horo::ToLowerAscii(m_projectPanelTemplateSearchText); !lowerQuery.empty() && lowerName.find(lowerQuery) == std::string::npos)
              return;

          ImGui::Dummy(ImVec2(0.0f, 2.0f));
          std::string label = std::string(icon) + "  " + name;
          if (ImGui::MenuItem(label.c_str())) {
              m_projectPanelScriptTemplateType = type;
              m_projectPanelCreateBasePath = prefillPath;
              m_projectPanelCreateName.clear();
              m_projectPanelError.clear();
              m_projectPanelCreateScriptModalRequested = true;
          }
      };

      drawTemplate(ICON_FA_FILE, "Behavior Script", ScriptTemplateType::BehaviorScript);
      drawTemplate("C", "Component Script", ScriptTemplateType::ComponentScript);
      drawTemplate(ICON_FA_GEAR, "System Script", ScriptTemplateType::SystemScript);
      drawTemplate(ICON_FA_FILE, "UI Script", ScriptTemplateType::UIScript);
      ImGui::Separator();
      drawTemplate("S", "Shader", ScriptTemplateType::Shader);
      drawTemplate(ICON_FA_CIRCLE, "Material", ScriptTemplateType::Material);
      ImGui::Separator();
      drawTemplate(ICON_FA_CUBE, "Scene", ScriptTemplateType::Scene);
      drawTemplate(ICON_FA_CUBE, "Prefab", ScriptTemplateType::Prefab);
      drawTemplate(ICON_FA_FILE, "Animation Controller", ScriptTemplateType::AnimationController);

      ImGui::EndMenu();
    }
    
    ImGui::PopStyleColor();
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
    if (!Ui::BeginEditorModal({"Create Project Entry", 400.0f, true}, false)) {
      m_projectPanelCreateModalOpen = false;
      return;
    }
    m_projectPanelCreateModalOpen = true;

    const Ui::EditorTheme& theme = Ui::GetEditorTheme();
    const char* itemKind = m_projectPanelCreateFolder ? "folder" : "file";
    
    std::string locationText = m_projectPanelCreateBasePath.empty() ? "project root" : m_projectPanelCreateBasePath;
    ImGui::Text("Create %s in %s", itemKind, locationText.c_str());

    std::array<char, 256> inputBuffer{};
    m_projectPanelCreateName.copy(inputBuffer.data(), inputBuffer.size() - 1);
    if (ImGui::IsWindowAppearing())
      ImGui::SetKeyboardFocusHere();
    const bool enterPressed = ImGui::InputTextWithHint(
      "##project_create_name", m_projectPanelCreateFolder
                                 ? "e.g. scripts"
                                 : "e.g. main.cpp",
      inputBuffer.data(), inputBuffer.size(), ImGuiInputTextFlags_EnterReturnsTrue);
    m_projectPanelCreateName = inputBuffer.data();

    if (!m_projectPanelError.empty())
      Ui::ErrorText(theme, m_projectPanelError.c_str());

    const auto footer = Ui::RenderEditorModalFooter(theme, "Create");
    if (footer.cancelled) {
      m_projectPanelError.clear();
      m_projectPanelCreateModalOpen = false;
    }

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
    if (const bool hasParentTraversal =
          std::ranges::any_of(relPath, [](const auto& part) { return part == ".."; });
      relPath.empty() || relPath == "." || relPath.is_absolute() || hasParentTraversal)
    {
      fail("Use a relative path inside the project root.");
      return;
    }

    const fs::path basePath = m_projectPanelCreateBasePath.empty() ? fs::path(".") : fs::path(m_projectPanelCreateBasePath);
    const fs::path candidate = m_projectBrowserRoot / basePath / relPath;
    if (!IsPathWithinDirectory(candidate, m_projectBrowserRoot))
    {
      fail("Path must remain inside the project root.");
      return;
    }

    if (const std::string createError =
      m_projectPanelCreateFolder ? CreateProjectFolder(candidate) : CreateProjectFile(candidate);
        !createError.empty())
      fail(createError);

    if (m_projectPanelError.empty())
    {
      InvalidateProjectBrowserCache();
      m_projectPanelCreateName.clear();
      m_projectPanelCreateModalOpen = false;
      ImGui::CloseCurrentPopup();
    }
  }

  /** @copydoc EditorLayer::DrawProjectCreateScriptModal */
  void EditorLayer::DrawProjectCreateScriptModal()
  {
    if (!Ui::BeginEditorModal({"Create Template File", 400.0f, true}, false)) {
      m_projectPanelCreateScriptModalOpen = false;
      return;
    }
    m_projectPanelCreateScriptModalOpen = true;

    const Ui::EditorTheme& theme = Ui::GetEditorTheme();
    const char* title;
    using enum ScriptTemplateType;
    switch (m_projectPanelScriptTemplateType) {
        case BehaviorScript: title = "Behavior Script"; break;
        case ComponentScript: title = "Component Script"; break;
        case SystemScript: title = "System Script"; break;
        case UIScript: title = "UI Script"; break;
        case Shader: title = "Shader"; break;
        case Material: title = "Material"; break;
        case Scene: title = "Scene"; break;
        case Prefab: title = "Prefab"; break;
        case AnimationController: title = "Animation Controller"; break;
        default: title = "Template"; break;
    }

    std::string locationText = m_projectPanelCreateBasePath.empty() ? "project root" : m_projectPanelCreateBasePath;
    ImGui::Text("Create %s in %s", title, locationText.c_str());

    std::array<char, 256> inputBuffer{};
    m_projectPanelCreateName.copy(inputBuffer.data(), inputBuffer.size() - 1);
    if (ImGui::IsWindowAppearing())
      ImGui::SetKeyboardFocusHere();
    
    const char* hint = "e.g. PlayerBehavior";
    if (m_projectPanelScriptTemplateType == ScriptTemplateType::Shader) hint = "e.g. MyShader";
    
    const bool enterPressed = ImGui::InputTextWithHint(
      "##project_create_script_name", hint,
      inputBuffer.data(), inputBuffer.size(), ImGuiInputTextFlags_EnterReturnsTrue);
    m_projectPanelCreateName = inputBuffer.data();

    if (!m_projectPanelError.empty())
      Ui::ErrorText(theme, m_projectPanelError.c_str());

    const auto footer = Ui::RenderEditorModalFooter(theme, "Create");
    if (footer.cancelled) {
      m_projectPanelError.clear();
      m_projectPanelCreateScriptModalOpen = false;
    }

    if (enterPressed || footer.confirmed)
      HandleProjectCreateScriptSubmit();

    Ui::EndEditorModal();
  }

  /** @copydoc EditorLayer::HandleProjectCreateScriptSubmit */
  void EditorLayer::HandleProjectCreateScriptSubmit()
  {
    namespace fs = std::filesystem;
    auto fail = [this](std::string msg) { m_projectPanelError = std::move(msg); };

    if (!m_projectBrowserRootValid || !fs::is_directory(m_projectBrowserRoot))
    {
      fail("Project root is unavailable.");
      return;
    }
    
    std::string baseName = m_projectPanelCreateName;
    if (baseName.find_first_not_of(" \t\r\n") == std::string::npos)
    {
      fail("Name cannot be empty.");
      return;
    }
    
    // Strip extension if provided
    if (size_t extPos = baseName.find_last_of('.'); extPos != std::string::npos) {
        baseName = baseName.substr(0, extPos);
    }

    const fs::path basePath = m_projectPanelCreateBasePath.empty() ? fs::path(".") : fs::path(m_projectPanelCreateBasePath);
    const fs::path relPath = basePath / baseName;
    const fs::path candidateBase = m_projectBrowserRoot / relPath;
    
    if (!IsPathWithinDirectory(candidateBase.parent_path(), m_projectBrowserRoot))
    {
      fail("Path must remain inside the project root.");
      return;
    }

    std::error_code ec;
    fs::create_directories(candidateBase.parent_path(), ec);

    std::string headerContent;
    std::string sourceContent;
    std::string singleFileContent;
    std::string singleFileExt;

    // Generate content based on template type
    if (m_projectPanelScriptTemplateType == ScriptTemplateType::BehaviorScript) {
        headerContent = std::format(
            "#pragma once\n\n"
            "#include \"scene/components/BehaviorComponent.h\"\n"
            "#include \"scene/Registry.h\"\n\n"
            "class {} : public Horo::Behavior {{\n"
            "public:\n"
            "    void OnUpdate(Horo::Entity self, Horo::Registry& reg, float dt) override;\n"
            "}};\n",
            baseName);
            
        sourceContent = std::format(
            "#include \"{}.h\"\n\n"
            "void {}::OnUpdate(Horo::Entity self, Horo::Registry& reg, float dt) {{\n"
            "    // Add your behavior logic here\n"
            "}}\n",
            baseName, baseName);
    } else if (m_projectPanelScriptTemplateType == ScriptTemplateType::ComponentScript) {
        headerContent = std::format(
            "#pragma once\n\n"
            "struct {} {{\n"
            "    // Add your component data fields here\n"
            "}};\n",
            baseName);
    } else if (m_projectPanelScriptTemplateType == ScriptTemplateType::SystemScript) {
        headerContent = std::format(
            "#pragma once\n\n"
            "#include \"scene/Registry.h\"\n\n"
            "class {} {{\n"
            "public:\n"
            "    void Update(Horo::Registry& reg, float dt);\n"
            "}};\n",
            baseName);
        sourceContent = std::format(
            "#include \"{}.h\"\n\n"
            "void {}::Update(Horo::Registry& reg, float dt) {{\n"
            "    // Add your system logic here\n"
            "}}\n",
            baseName, baseName);
    } else {
        // Fallback for others - just create an empty file
        singleFileContent = "// Template generated file\n";
        singleFileExt = ".txt";
    }

    auto writeFile = [](const fs::path& path, const std::string& content) -> std::string {
        std::ofstream out(path);
        if (!out.is_open()) return "Failed to write file: " + path.filename().string();
        out << content;
        return "";
    };

    if (!headerContent.empty()) {
        if (std::string err = writeFile(fs::path(candidateBase.string() + ".h"), headerContent); !err.empty()) {
            fail(err);
            return;
        }
    }
    
    if (!sourceContent.empty()) {
        if (std::string err = writeFile(fs::path(candidateBase.string() + ".cpp"), sourceContent); !err.empty()) {
            fail(err);
            return;
        }
    }
    
    if (!singleFileContent.empty()) {
        if (std::string err = writeFile(fs::path(candidateBase.string() + singleFileExt), singleFileContent); !err.empty()) {
            fail(err);
            return;
        }
    }

    InvalidateProjectBrowserCache();
    m_projectPanelCreateName.clear();
    m_projectPanelCreateScriptModalOpen = false;
    ImGui::CloseCurrentPopup();
  }

  /** @copydoc EditorLayer::DrawProjectTree */
  void EditorLayer::DrawProjectTree(const Ui::EditorTheme&)
  {
    ImGui::BeginChild("##project_tree", ImVec2(0, 0), false);
    DrawProjectTreeRecursive(m_projectBrowserRoot, m_projectBrowserRoot);
    ImGui::EndChild();
  }

  /** @brief Draws the favorites tree with placeholder content. */
  void EditorLayer::DrawProjectFavoritesTree(const Ui::EditorTheme& theme) const
  {
    const auto& palette = theme.palette;
    ImGui::BeginChild("##project_favorites_tree", ImVec2(0, 0), false);

    ImGui::PushStyleColor(ImGuiCol_Text, palette.textMuted);
    constexpr const char* kNoFavoritesText = "No favorites yet.";
    const ImVec2 textSize = ImGui::CalcTextSize(kNoFavoritesText);
    const float contentW = ImGui::GetContentRegionAvail().x;
    const float contentH = ImGui::GetContentRegionAvail().y;
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + contentH * 0.4f);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (contentW - textSize.x) * 0.5f);
    ImGui::TextUnformatted(kNoFavoritesText);
    ImGui::PopStyleColor();

    ImGui::EndChild();
  }

  /** @copydoc EditorLayer::MakeAssetsPanelCallbacks */
  EditorAssetsPanelCallbacks EditorLayer::MakeAssetsPanelCallbacks()
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
    callbacks.makeObjectFromAsset = [this](std::string_view assetId)
    {
      return MakeObjectFromAsset(m_document, assetId, m_schema);
    };
    callbacks.setDeferredFilePick = [this](int deferredType)
    {
      m_deferredFilePick = static_cast<DeferredFilePick>(deferredType);
    };
    callbacks.openImportAssetModal = [this]()
    {
      m_importAssetModal.Open({}, &m_assetImportService.Registry(),
                              m_projectBrowserRootValid ? m_projectBrowserRoot : std::filesystem::current_path());
    };
    return callbacks;
  }

  /** @copydoc EditorLayer::MakeAssetsPanelState */
  EditorAssetsPanelState EditorLayer::MakeAssetsPanelState()
  {
    EditorAssetsPanelState state;
    state.selectedAssetId = &m_selectedAssetId;
    state.selectedIndices = &m_selectedIndices;
    state.albedoSelDrop = &m_albedoSelDrop;
    state.assetSearchOpen = &m_assetSearchOpen;
    state.assetSearchQuery = &m_assetSearchQuery;
    state.document = &m_document;
    state.assetImportService = &m_assetImportService;
    state.liveRegistry = m_liveRegistry;
    return state;
  }

  /** @copydoc EditorLayer::DrawAssetsPanel */
  void EditorLayer::DrawAssetsPanel()
  {
    EditorComponentContext ctx;
    ctx.document = &m_document;
    ctx.lastSavedDocument = &m_lastSavedDocument;
    ctx.schema = &m_schema;
    ctx.selectedIndices = &m_selectedIndices;
    ctx.selectedAssetId = &m_selectedAssetId;
    ctx.assetImportService = &m_assetImportService;
    ctx.liveRegistry = m_liveRegistry;

    m_assetsPanel.Draw(ctx, MakeAssetsPanelCallbacks(), MakeAssetsPanelState());
  }

  /** @copydoc EditorLayer::DrawAssetsPanelInline */
  void EditorLayer::DrawAssetsPanelInline()
  {
    m_assetsPanel.DrawContent(MakeAssetsPanelCallbacks(), MakeAssetsPanelState());
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
