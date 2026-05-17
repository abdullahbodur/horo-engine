/**
 * @file EditorLifecycle.cpp
 * @brief EditorLayer lifecycle: initialization, shutdown, toggle, and document loading.
 */
#include "ui/editor/EditorLayer.h"
#include "ui/editor/EditorLayerInternal.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
// clang-format off
#include <windows.h>
#include <commdlg.h>
// clang-format on
#endif

// clang-format off
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
// clang-format on
#include <imgui.h>

#include <algorithm>
#include <array>

#include "core/Logger.h"
#include "ui/editor/AssetIdentity.h"
#include "ui/editor/EditorAssetImport.h"
#include "ui/editor/components/EditorAssetThumbnailPreview.h"
#include "ui/editor/EditorFilePickerUtils.h"
#include "ui/editor/EditorImportedAssetPathUtils.h"
#include "ui/editor/EditorImGuiBackend.h"
#include "ui/editor/EditorPropertyRules.h"
#include "ui/editor/EditorSceneGraph.h"
#include "ui/editor/EditorSelectionRules.h"
#include "renderer/Renderer.h"
#include "renderer/Shader.h"
#include "scene/Entity.h"
#include "scene/Registry.h"
#include "scene/components/MeshComponent.h"
#include "scene/components/PlayerTagComponent.h"
#include "ui/editor/SceneSerializer.h"
#include "ui/HoroTheme.h"
#include "ui/IconsFontAwesome6.h"
#include "ui/UiFonts.h"
#include "ui/editor/components/EditorFileBrowser.h"

namespace Horo::Editor {
namespace {

/** @brief Load the editor UI font and merge FontAwesome icon glyphs. */
void LoadEditorFonts(ImGuiIO &io) {
  const Ui::FontFamilyConfig config{.relativePath = "assets/fonts/InterVariable.ttf",
                                    .size = 16.0f};
  Ui::LoadFonts(io, config);

  const Ui::FontFamilyConfig faConfig{.relativePath = "assets/fonts/FontAwesome6.ttf",
                                      .size = 14.0f};
  if (const Ui::FontResolutionResult resolved = Ui::ResolveFontPath(faConfig);
      resolved.found) {
    ImFontConfig faCfg;
    faCfg.MergeMode = true;
    faCfg.GlyphMinAdvanceX = 14.0f;
    static constexpr std::array<ImWchar, 3> iconRanges = {ICON_MIN_FA, ICON_MAX_FA, 0};
    io.Fonts->AddFontFromFileTTF(resolved.resolvedPath.string().c_str(),
                                 14.0f, &faCfg, iconRanges.data());
  }

  // Standalone large FA font for thumbnail icons (not merged — keeps toolbar/search unaffected)
  const Ui::FontFamilyConfig faLargeConfig{.relativePath = "assets/fonts/FontAwesome6.ttf",
                                           .size = 72.0f};
  if (const Ui::FontResolutionResult faLargeResolved = Ui::ResolveFontPath(faLargeConfig);
      faLargeResolved.found) {
    ImFontConfig faLargeCfg;
    faLargeCfg.GlyphMinAdvanceX = 72.0f;
    static constexpr std::array<ImWchar, 3> iconRangesLarge = {ICON_MIN_FA, ICON_MAX_FA, 0};
    if (ImFont *largeFont = io.Fonts->AddFontFromFileTTF(
            faLargeResolved.resolvedPath.string().c_str(),
            72.0f, &faLargeCfg, iconRangesLarge.data())) {
      EditorFileBrowser::SetLargeIconFont(largeFont);
    }
  }
}

} // namespace

/** @copydoc EditorLayer::Init */
void EditorLayer::Init(GLFWwindow *window) {
  m_window = window;
  m_uiWidgets.Initialize(this);

  m_bottomDock.SetAssetsTabCallback([this]() { DrawAssetsPanelInline(); });

  InitUiWidgetCallbacks();

  m_mcpController.Initialize();
  m_settingsModal.SetMcpController(&m_mcpController);
  if (m_mcpController.SettingsDocument().parseError)
    LogWarn("[MCP] Settings load fallback: {}",
            m_mcpController.SettingsDocument().error);

  // Load persisted editor user preferences (theme preset, ...) and apply.
  m_userSettingsDocument = LoadEditorUserSettingsDocument();
  if (m_userSettingsDocument.parseError ||
      !m_userSettingsDocument.error.empty()) {
    LogWarn("[Editor] User settings load fallback: {}",
            m_userSettingsDocument.error);
  }
  Ui::SetEditorThemePreset(m_userSettingsDocument.settings.themePreset);

  m_settingsModal.SetUserSettingsDocument(&m_userSettingsDocument);
  m_settingsModal.SetApplyThemePresetCallback(
      [](Ui::EditorThemePreset preset) {
        Ui::SetEditorThemePreset(preset);
        Ui::ApplyEditorTheme(ImGui::GetStyle());
      });

  InitImGuiContext(window);

  LoadEditorSchema();

  try {
    const std::filesystem::path wv = ResolvePreviewShaderPath("wire.vert");
    const std::filesystem::path wf = ResolvePreviewShaderPath("wire.frag");
    m_wireframeShader = Shader::FromFiles(wv.generic_string(), wf.generic_string());
  } catch (const ShaderException &e) {
    LogWarn("[Editor] Failed to load wireframe shader: {}", e.what());
  }

  ClearAssetThumbnailMeshCaches();
  LogInfo("[Editor] Asset thumbnail caches cleared on Init");
}

/** @copydoc EditorLayer::InitUiWidgetCallbacks */
void EditorLayer::InitUiWidgetCallbacks() {
  EditorUIWidgets::Callbacks callbacks;

  callbacks.onApplyRenameObject = [this](int index, const std::string &newId) {
    if (index < 0 || index >= static_cast<int>(m_document.objects.size())) {
      m_uiWidgets.SetRenameObjectError("Invalid object index");
      return false;
    }

    if (std::string error = ValidateRenameCandidate(m_document, index, newId);
        !error.empty()) {
      m_uiWidgets.SetRenameObjectError(error);
      return false;
    }

    SceneObject &target = m_document.objects[static_cast<size_t>(index)];
    if (const std::string oldId = target.id; oldId != newId) {
      target.id = newId;
      RewriteObjectIdReferences(&m_document, oldId, newId);
      m_document.dirty = true;
    }
    return true;
  };

  callbacks.onConfirmDeleteObjects = [this](const std::vector<int> &indices) {
    std::vector<int> sorted = indices;
    std::sort(sorted.rbegin(), sorted.rend());
    for (int idx : sorted) {
      if (idx >= 0 && idx < static_cast<int>(m_document.objects.size()))
        m_document.objects.erase(m_document.objects.begin() + idx);
    }
    m_selectedIndices.clear();
    MarkDirtyAndReload();
  };

  callbacks.onConfirmDeleteAsset = [this](const std::string &assetId) {
    const AssetDeleteResult deleteResult = DeleteAssetDefinition(assetId);
    if (!deleteResult.ok) {
      LogWarn("[Editor] Failed to delete asset '{}': {}", assetId,
              deleteResult.error.empty() ? "unknown error"
                                         : deleteResult.error);
    }
  };

  callbacks.onConfirmExit = [this]() { m_closeRequested = true; };

  callbacks.getStatusBarText = [this]() {
    const EditorStatusText status = BuildEditorStatusText(
        EditorStatusSnapshot{static_cast<int>(m_selectedIndices.size()),
                             m_document.dirty, m_flyMode, m_wantsReload});
    return std::format("Sel: {} | Dirty: {} | Fly: {} | Reload: {}",
                       status.selectionCount, status.dirtyText, status.flyText,
                       status.reloadText);
  };

  m_uiWidgets.SetCallbacks(std::move(callbacks));
}

/** @copydoc EditorLayer::InitImGuiContext */
void EditorLayer::InitImGuiContext(GLFWwindow *window) {
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO &io = ImGui::GetIO();
  LoadEditorFonts(io);
  Ui::ApplyEditorTheme(ImGui::GetStyle());
  m_imguiIniPath = ResolveEditorLayoutPath().string();
  std::error_code settingsEc;
  std::filesystem::create_directories(ResolveEditorLayoutPath().parent_path(),
                                      settingsEc);
  if (settingsEc) {
    LogWarn("[Editor] Failed to ensure editor settings directory: {}",
            settingsEc.message());
    m_imguiIniPath.clear();
    io.IniFilename = nullptr;
  } else {
    m_hasPersistedDockLayout =
        std::filesystem::exists(ResolveEditorLayoutPath());
    io.IniFilename = m_imguiIniPath.c_str();
  }
  LoadWorkspaceState();

  const RenderBackendId backendId = Renderer::GetBackendId();
  m_imguiBackendInitialized = InitEditorImGuiBackend(window, backendId);
  if (!m_imguiBackendInitialized) {
    LogWarn("[Editor] No supported ImGui backend for renderer backend '{}'",
            ToString(backendId));
  }
}

/** @copydoc EditorLayer::LoadEditorSchema */
void EditorLayer::LoadEditorSchema() {
  const std::array<std::filesystem::path, 4> schemaCandidates = {
      ProjectPath::ResolveSdk("assets/editor_schema.json"),
      ProjectPath::Root() / "assets" / "editor_schema.json",
      ProjectPath::Root() / "engine" / "assets" / "editor_schema.json",
      ProjectPath::Root() / "horo-engine" / "assets" / "editor_schema.json",
  };
  for (const auto &candidate : schemaCandidates) {
    std::error_code ec;
    if (std::filesystem::is_regular_file(candidate, ec) && !ec) {
      m_schema.LoadFromFile(candidate.string());
      break;
    }
  }
}

/** @copydoc EditorLayer::Shutdown */
void EditorLayer::Shutdown() {
  SaveWorkspaceStateIfNeeded(true);
  if (!m_imguiIniPath.empty())
    ImGui::SaveIniSettingsToDisk(m_imguiIniPath.c_str());
  m_mcpController.Shutdown();
  if (m_imguiBackendInitialized)
    ShutdownEditorImGuiBackend(Renderer::GetBackendId());
  m_imguiBackendInitialized = false;
  ImGui::DestroyContext();
}

/** @copydoc EditorLayer::Toggle */
void EditorLayer::Toggle() {
  m_active = !m_active;
  if (!m_active)
    m_playMode = false;
  if (m_flyMode) {
    m_flyMode = false;
    m_flyCamInitialized = false;
  }
  m_closeRequested = false;
  if (m_window)
    glfwSetInputMode(m_window, GLFW_CURSOR,
                     m_active ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_DISABLED);
  m_prevMouseL = false;
  m_selectedIndices.clear();
  m_mcpController.SetEditorActive(m_active);
}

/** @copydoc EditorLayer::SetCursorVisible */
void EditorLayer::SetCursorVisible(bool visible) {
  if (!m_window)
    return;
  glfwSetInputMode(m_window, GLFW_CURSOR,
                   visible ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_DISABLED);
}

/** @copydoc EditorLayer::SetProjectBrowserRoot */
void EditorLayer::SetProjectBrowserRoot(std::filesystem::path root) {
  m_bottomDock.InvalidateProjectBrowserCache();
  std::error_code ec;

  if (root.empty()) {
    m_projectBrowserRoot.clear();
    m_projectBrowserRootValid = false;
    m_bottomDock.SetProjectBrowserRoot({});
    return;
  }

  std::filesystem::path canon = std::filesystem::weakly_canonical(root, ec);
  if (ec)
    canon = std::move(root);
  if (!std::filesystem::is_directory(canon, ec) || ec) {
    m_projectBrowserRoot = canon;
    m_projectBrowserRootValid = false;
    m_bottomDock.SetProjectBrowserRoot(canon);
    return;
  }

  m_projectBrowserRoot = canon;
  m_projectBrowserRootValid = true;
  m_bottomDock.SetProjectBrowserRoot(canon);

  if (!m_bottomDock.GetSavedProjectBrowserCwd().empty()) {
    std::filesystem::path preferred = m_bottomDock.GetSavedProjectBrowserCwd();
    if (preferred.is_relative())
      preferred = canon / preferred;
    preferred = std::filesystem::weakly_canonical(preferred, ec);
    if (!ec && std::filesystem::is_directory(preferred) &&
        IsPathWithinDirectory(preferred, canon)) {
      m_bottomDock.SetProjectBrowserCwd(preferred);
      return;
    }
  }
  m_bottomDock.SetProjectBrowserCwd(canon);
}

/** @copydoc EditorLayer::SetProjectBrowserExtraBlocklist */
void EditorLayer::SetProjectBrowserExtraBlocklist(
    const std::unordered_set<std::string, StringHash, std::equal_to<>>& names) {
  m_projectExtraBlocklist = names;
  m_bottomDock.SetProjectExtraBlocklist(names);
  m_bottomDock.InvalidateProjectBrowserCache();
}

/** @copydoc EditorLayer::InvalidateProjectBrowserCache */
void EditorLayer::InvalidateProjectBrowserCache() {
  m_projectDirCache.clear();
  m_bottomDock.InvalidateProjectBrowserCache();
}

/** @copydoc EditorLayer::LoadWorkspaceState */
void EditorLayer::LoadWorkspaceState() {
  m_workspaceDocument = LoadEditorWorkspaceDocument();
  if (m_workspaceDocument.parseError) {
    LogWarn("[Editor] Workspace settings load fallback: {}",
            m_workspaceDocument.error);
  }

  m_bottomDock.SetConsoleShowInfo(m_workspaceDocument.state.consoleShowInfo);
  m_bottomDock.SetConsoleShowWarn(m_workspaceDocument.state.consoleShowWarn);
  m_bottomDock.SetConsoleShowError(m_workspaceDocument.state.consoleShowError);
  if (!m_workspaceDocument.state.projectBrowserCwd.empty())
    m_bottomDock.SetSavedProjectBrowserCwd(
        std::filesystem::path(m_workspaceDocument.state.projectBrowserCwd));
  m_workspaceStateDirty = false;
}

/** @copydoc EditorLayer::SaveWorkspaceStateIfNeeded */
void EditorLayer::SaveWorkspaceStateIfNeeded(bool force) {
  if (!force && !m_workspaceStateDirty)
    return;

  m_workspaceDocument.state.consoleShowInfo = m_bottomDock.IsConsoleShowInfo();
  m_workspaceDocument.state.consoleShowWarn = m_bottomDock.IsConsoleShowWarn();
  m_workspaceDocument.state.consoleShowError = m_bottomDock.IsConsoleShowError();

  const auto &projectCwd = m_bottomDock.GetProjectBrowserCwd();
  const auto &savedProjectCwd = m_bottomDock.GetSavedProjectBrowserCwd();

  if (!projectCwd.empty()) {
    m_workspaceDocument.state.projectBrowserCwd = projectCwd.generic_string();
  } else if (!savedProjectCwd.empty()) {
    m_workspaceDocument.state.projectBrowserCwd = savedProjectCwd.generic_string();
  } else {
    m_workspaceDocument.state.projectBrowserCwd.clear();
  }

  if (std::string saveError;
      !SaveEditorWorkspaceDocument(&m_workspaceDocument, &saveError)) {
    LogWarn("[Editor] Failed to save workspace settings: {}", saveError);
    return;
  }
  m_workspaceStateDirty = false;
}

/** @copydoc EditorLayer::MarkWorkspaceStateDirty */
void EditorLayer::MarkWorkspaceStateDirty() { m_workspaceStateDirty = true; }

/** @copydoc EditorLayer::ApplyLoadedDocument */
void EditorLayer::ApplyLoadedDocument(SceneDocument doc, bool resetHistory) {
  using enum SceneObjectType;
  if (doc.filePath.empty())
    doc.filePath = "assets/scenes/scene.json";

  EnsureAssetIdentity(&doc);

  for (auto &obj : doc.objects) {
    if (obj.type != Prop)
      continue;
    const auto behIt = obj.props.find("behavior");
    if (behIt == obj.props.end() || behIt->second.empty() ||
        behIt->second == "none")
      continue;

    if (!std::ranges::any_of(obj.components, [](const auto &comp) {
          return comp.type == "script";
        })) {
      ComponentDesc script;
      script.type = "script";
      script.props["behaviorTag"] = behIt->second;
      obj.components.push_back(std::move(script));
    }
    obj.props.erase("behavior");
  }

  SyncAssetScaleMetadata(&doc);
  LogDanglingObjectReferences(doc, doc.filePath);

  m_document = std::move(doc);
  m_lastSavedDocument = m_document;
  m_selectedIndices.clear();
  m_selectedAssetId.clear();
  if (resetHistory)
    ClearHistory();
}

/** @copydoc EditorLayer::LoadDocument */
void EditorLayer::LoadDocument(SceneDocument doc) {
  ApplyLoadedDocument(std::move(doc), true);
}

/** @copydoc EditorLayer::SyncRuntimeEntityIds */
void EditorLayer::SyncRuntimeEntityIds(const Registry &registry) {
  using enum SceneObjectType;
  std::vector<int> propIndices;
  propIndices.reserve(m_document.objects.size());
  for (int i = 0; i < static_cast<int>(m_document.objects.size()); ++i) {
    if (m_document.objects[static_cast<size_t>(i)].type == Prop)
      propIndices.push_back(i);
  }

  std::vector<Entity> meshEntities;
  for (Entity e : registry.GetEntities<MeshComponent>()) {
    if (registry.Has<PlayerTagComponent>(e))
      continue;
    meshEntities.push_back(e);
  }
  std::ranges::sort(meshEntities);

  const size_t propN = propIndices.size();
  const size_t meshN = meshEntities.size();
  const size_t n = std::min(propN, meshN);
  if (propN != meshN) {
    LogWarn("EditorLayer::SyncRuntimeEntityIds: {} prop(s) vs {} mesh "
            "entity(ies); mapping first {}",
            propN, meshN, n);
  }

  for (size_t j = 0; j < n; ++j) {
    m_document.objects[static_cast<size_t>(propIndices[j])].props["_eid"] =
        std::to_string(meshEntities[j]);
  }
  for (size_t j = n; j < propN; ++j)
    m_document.objects[static_cast<size_t>(propIndices[j])].props.erase("_eid");
}

/** @copydoc EditorLayer::SetHotReloadOverlay */
void EditorLayer::SetHotReloadOverlay(bool active, float progress01,
                                      float spinnerAngleRad,
                                      std::string_view label) {
  if (active) {
    m_uiWidgets.OnHotReloadStart(0.0f, label);
    m_uiWidgets.OnHotReloadProgress(progress01, spinnerAngleRad);
  } else {
    m_uiWidgets.OnHotReloadEnd();
  }
}

/** @copydoc EditorLayer::ProcessDeferredFilePicks */
void EditorLayer::ProcessDeferredFilePicks() { // NOSONAR
  const DeferredFilePick pick = m_deferredFilePick;
  if (pick == DeferredFilePick::None)
    return;
  m_deferredFilePick = DeferredFilePick::None;

#if !defined(_WIN32) && !defined(__APPLE__)
  (void)pick;
  return;
#endif

  switch (pick) {
  case DeferredFilePick::None:
    break;
  case DeferredFilePick::SelectedAssetAlbedo: {
    const std::string id = m_selectedAssetId;
    if (id.empty() || !m_document.assets.contains(id))
      break;
    if (std::string err; m_assetImportService.ImportTextureForAsset(
            PickTextureFilePath(), id, &m_document.assets[id], &err)) {
      m_document.dirty = true;
    } else if (!err.empty())
      LogWarn("Texture browse: {}", err);
    break;
  }
  }
}

/** @copydoc EditorLayer::AddNewScene */
void EditorLayer::AddNewScene() {
  SceneDocument newDoc;
  newDoc.sceneId = "scene";
  newDoc.sceneName = "Scene";
  newDoc.dirty = true;
  m_document = std::move(newDoc);
  m_selectedIndices.clear();
  m_selectedAssetId.clear();
}

#ifdef _WIN32
// SONAR-OFF
/** @copydoc EditorLayer::OpenAdditionalSceneFile */
void EditorLayer::OpenAdditionalSceneFile() {
#else
/** @copydoc EditorLayer::OpenAdditionalSceneFile */
void EditorLayer::OpenAdditionalSceneFile() const {
#endif
#ifdef _WIN32
  char filePath[MAX_PATH] = {}; // NOSONAR: cpp:S3003 Windows OPENFILENAMEA
  // requires a char[] output buffer
  OPENFILENAMEA ofn = {};
  ofn.lStructSize = sizeof(ofn);
  ofn.hwndOwner = nullptr;
  ofn.lpstrFilter = "Scene Files\0*.horo;*.json\0All Files\0*.*\0";
  ofn.lpstrFile = filePath;
  ofn.nMaxFile = MAX_PATH;
  ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
  if (GetOpenFileNameA(&ofn)) {
    try {
      SceneDocument doc = SceneSerializer::LoadFromFile(filePath);
      ApplyLoadedDocument(std::move(doc), false);
    } catch (const SceneSerializerException &e) {
      LogError("EditorLayer: failed to open scene '{}': {}", filePath,
               e.what());
    }
  }
#endif
}
#ifdef _WIN32
// SONAR-ON
#endif

/** @copydoc EditorLayer::CloseAdditionalScene */
void EditorLayer::CloseAdditionalScene(int index) {
  if (index < 0 || index >= static_cast<int>(m_additionalScenes.size()))
    return;
  m_additionalScenes.erase(m_additionalScenes.begin() + index);
}

/** @copydoc EditorLayer::SaveAdditionalScene */
bool EditorLayer::SaveAdditionalScene(int index, std::string *outError) {
  if (index < 0 || index >= static_cast<int>(m_additionalScenes.size()))
    return false;
  SceneDocument &doc = m_additionalScenes[static_cast<size_t>(index)];
  if (doc.filePath.empty()) {
    if (outError)
      *outError = "No file path — use Save As.";
    return false;
  }
  try {
    SceneSerializer::SaveToFile(doc, doc.filePath);
    doc.dirty = false;
    return true;
  } catch (const SceneSerializerException &e) {
    if (outError)
      *outError = e.what();
    return false;
  }
}

/** @copydoc EditorLayer::ApplySchemaDefaults */
void EditorLayer::ApplySchemaDefaults(SceneObject &obj) const {
  ApplySchemaFieldDefaults(obj, m_schema);
}

/** @copydoc EditorLayer::ApplyComponentSchemaDefaults */
void EditorLayer::ApplyComponentSchemaDefaults(ComponentDesc &component) const {
  ApplyComponentFieldDefaults(component, m_schema);
}

} // namespace Horo::Editor
