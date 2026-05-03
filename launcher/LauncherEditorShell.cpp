#include "launcher/LauncherEditorShell.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <format>
#include <functional>
#include <stdexcept>
#include <string_view>
#include <system_error>

#include <imgui.h>

#include "core/Logger.h"
#include "core/ProjectPath.h"
#include "editor/SceneSerializer.h"
#include "launcher/LauncherProjectTemplate.h"
#include "launcher/NativeFolderDialog.h"
#include "math/MathUtils.h"
#include "math/Quaternion.h"
#include "renderer/ObjLoader.h"
#include "renderer/Renderer.h"
#include "scene/Entity.h"
#include "scene/components/MeshComponent.h"
#include "scene/components/TransformComponent.h"
#include "ui/common/HoroTheme.h"
#include "ui/common/HoroWidgets.h"
#include "ui/launcher/LauncherWidgets.h"

#ifndef HORO_ENGINE_VERSION
#define HORO_ENGINE_VERSION "0.0.0"
#endif

namespace Horo::Launcher {
namespace fs = std::filesystem;

namespace {
std::string BufferToString(const std::array<char, 512> &buffer) {
  return std::string(buffer.data());
}

std::string BufferToString(const std::array<char, 256> &buffer) {
  return std::string(buffer.data());
}

template <size_t N>
void CopyToBuffer(std::array<char, N> *buffer, std::string_view value) {
  if (!buffer)
    return;
  buffer->fill('\0');
  if (value.empty())
    return;
  const size_t count = std::min(value.size(), N - 1);
  std::copy_n(value.data(), count, buffer->data());
  (*buffer)[count] = '\0';
}

constexpr std::array<const char *, 3> kRendererBackendLabels = {
    "OpenGL",
    "Vulkan",
    "Null",
};

constexpr std::array<const char *, 3> kRendererBackendIds = {
    "opengl",
    "vulkan",
    "null",
};

std::string RendererBackendIdFromIndex(int index) {
  if (index < 0 || index >= static_cast<int>(kRendererBackendIds.size()))
    return kRendererBackendIds.front();
  return kRendererBackendIds[static_cast<size_t>(index)];
}

Vec3 ForwardFromYawPitch(float yawDeg, float pitchDeg) {
  const float yawRad = ToRadians(yawDeg);
  const float pitchRad = ToRadians(std::clamp(pitchDeg, -89.0f, 89.0f));
  return {-std::sin(yawRad) * std::cos(pitchRad), std::sin(pitchRad),
          -std::cos(yawRad) * std::cos(pitchRad)};
}

fs::path DefaultBrowseDirectory(const fs::path &rawPath) {
  if (rawPath.empty())
    return fs::current_path();

  std::error_code ec;
  if (fs::is_directory(rawPath, ec))
    return rawPath;

  const fs::path parent = rawPath.parent_path();
  ec.clear();
  if (!parent.empty() && fs::is_directory(parent, ec))
    return parent;

  return fs::current_path();
}

bool IsInstalledEnginePrefix(const fs::path &candidate) {
  if (candidate.empty())
    return false;

  std::error_code ec;
  return fs::is_regular_file(candidate / "lib" / "cmake" / "HoroEngine" /
                                 "HoroEngineConfig.cmake",
                             ec) &&
         !ec;
}

bool IsBuildTreeEnginePrefix(const fs::path &candidate) {
  if (candidate.empty())
    return false;

  std::error_code ec;
  const bool hasConfig =
      fs::is_regular_file(candidate / "HoroEngineConfig.cmake", ec) && !ec;
  ec.clear();
  const bool hasTargets =
      fs::is_regular_file(candidate / "HoroEngineTargets.cmake", ec) && !ec;
  return hasConfig && hasTargets;
}

fs::path NormalizePathForLookup(const fs::path &rawPath) {
  if (rawPath.empty())
    return {};

  std::error_code ec;
  fs::path normalized = fs::weakly_canonical(rawPath, ec);
  if (ec)
    normalized = fs::absolute(rawPath, ec);
  if (ec)
    normalized = rawPath;
  return normalized.lexically_normal();
}

void ApplyTransformUpdateFromObject(Scene *scene,
                                    const Editor::SceneObject &object) {
  if (!scene)
    return;
  if (const auto runtimeEntityIt = object.props.find("_eid");
      runtimeEntityIt != object.props.end()) {
    try {
      const auto entity =
          static_cast<Entity>(std::stoul(runtimeEntityIt->second));
      if (scene->GetRegistry().IsAlive(entity) &&
          scene->GetRegistry().Has<TransformComponent>(entity)) {
        auto &transform = scene->GetRegistry().Get<TransformComponent>(entity);
        transform.current.position = object.position;
        transform.previous.position = object.position;
        transform.current.scale = object.scale;
        transform.previous.scale = object.scale;
        transform.current.rotation = Quaternion::FromEuler(
            ToRadians(object.pitch), ToRadians(object.yaw),
            ToRadians(object.roll));
        transform.previous.rotation = transform.current.rotation;
      }
    } catch (const std::invalid_argument &e) {
      // UI state may still reference transient runtime ids while scene reload
      // is in progress.
      LogDebug("[Launcher] Ignoring invalid runtime entity id '{}': {}",
               runtimeEntityIt->second, e.what());
    } catch (const std::out_of_range &e) {
      // Ignore malformed runtime ids coming from stale serialized props.
      LogDebug("[Launcher] Ignoring out-of-range runtime entity id '{}': {}",
               runtimeEntityIt->second, e.what());
    }
  }
}

// Validates a normalized project root and sets outError on failure.
// Extracted from OpenProject() to reduce cognitive complexity (cpp:S3776).
bool CheckProjectRootValid(const fs::path &projectRoot, std::string *outError) {
  if (projectRoot.empty()) {
    if (outError)
      *outError = "Project path is empty.";
    return false;
  }
  if (!IsLauncherProjectRoot(projectRoot)) {
    if (outError)
      *outError = "Project manifest not found at " +
                  ResolveProjectManifestPath(projectRoot).string();
    return false;
  }
  return true;
}
} // namespace

// Loads the project manifest and default scene document for the given project
// root. Extracted to reduce cognitive complexity of OpenProject() (cpp:S3776).
// resolveAsset wraps the caller's ResolveAssetPath member to avoid having to
// expose it as a free function.
template <typename F>
static bool LoadLauncherDocuments(const fs::path &projectRoot,
                                  LauncherProjectDocument &projectDocument,
                                  Editor::SceneDocument &sceneDocument,
                                  F &&resolveAsset, std::string *outError) {
  projectDocument = LoadProjectManifestDocument(projectRoot);
  if (projectDocument.parseError) {
    if (outError)
      *outError = projectDocument.error;
    return false;
  }
  try {
    const fs::path scenePath =
        resolveAsset((projectRoot / projectDocument.manifest.defaultScene)
                         .lexically_normal()
                         .generic_string());
    sceneDocument = Editor::SceneSerializer::LoadFromFile(scenePath.string());
  } catch (const Editor::SceneSerializerException &e) {
    if (outError)
      *outError = e.what();
    return false;
  }
  return true;
}

void LauncherEditorShell::Attach(Editor::EditorLayer *editor, Scene *scene,
                                 SceneReferenceRuntime *runtime,
                                 Camera *camera) {
  m_editor = editor;
  m_scene = scene;
  m_runtime = runtime;
  m_camera = camera;
}

void LauncherEditorShell::Initialize() {
  m_homeDocument = LoadEditorHomeDocument();
  if (m_homeDocument.parseError)
    LogWarn("[Launcher] Editor home settings load fallback: {}",
            m_homeDocument.error);
  PruneMissingRecentProjects(&m_homeDocument);
  std::string saveError;
  SaveEditorHomeDocument(&m_homeDocument, &saveError);

  CopyToBuffer(&m_newProjectNameInput, "MyHoroGame");
  CopyToBuffer(&m_newProjectPathInput,
               (fs::current_path() / "MyHoroGame").string());

  ConfigureRuntimeCallbacks();
}

void LauncherEditorShell::Shutdown() {
  m_processRunner.Stop();
  std::string saveError;
  SaveEditorHomeDocument(&m_homeDocument, &saveError);
}

void LauncherEditorShell::ConfigureRuntimeCallbacks() {
  if (!m_editor || !m_runtime)
    return;

  m_editor->SetFileMenuRenderCallback([this]() {
    if (!HasActiveProject())
      return;
    if (ImGui::MenuItem("Close Project"))
      CloseProject();
  });
  m_editor->SetOverlayRenderCallback([this]() { RenderOverlay(); });
  m_runtime->SetPropEntityCreatedCallback(
      [this](const RuntimeSceneProp &prop, Entity entity, Scene &sceneRef) {
        MeshComponent component;
        component.meshTag = prop.meshTag;
        component.mesh = LoadMeshForTag(prop.meshTag);
        component.material = std::make_shared<Material>();
        component.material->shader = EnsureSceneShader();
        component.material->albedoMap = LoadTexture(prop.albedoMap);
        sceneRef.GetRegistry().Add<MeshComponent>(entity, std::move(component));
      });
  m_editor->SetTransformCallback([this](const Editor::SceneObject &object) {
    ApplyTransformUpdateFromObject(m_scene, object);

    std::string lightError;
    if (!m_runtime->UpdateLiveLight(object, &lightError) &&
        !lightError.empty() && object.type == Editor::SceneObjectType::Light) {
      LogWarn("[Launcher] Live light update failed: {}", lightError);
    }
  });
}

bool LauncherEditorShell::OpenProject(const fs::path &projectPath,
                                      std::string *outError) {
  if (outError)
    outError->clear();

  const fs::path projectRoot = NormalizeProjectRootInput(projectPath);
  if (!CheckProjectRootValid(projectRoot, outError))
    return false;

  LauncherProjectDocument projectDocument;
  Editor::SceneDocument sceneDocument;
  if (!LoadLauncherDocuments(
          projectRoot, projectDocument, sceneDocument,
          [this](const std::string &p) { return ResolveAssetPath(p); },
          outError))
    return false;

  UnloadCurrentProjectState();
  m_projectRoot = projectRoot;
  m_projectDocument = projectDocument;
  ProjectPath::SetProjectRoot(projectRoot);

  if (m_editor)
    SetupEditorForProject(projectRoot, sceneDocument);

  if (m_runtime) {
    const SceneRuntimeOperationResult load =
        m_runtime->LoadDocument(sceneDocument);
    if (!load.ok) {
      if (outError)
        *outError = load.error;
      CloseProject();
      return false;
    }
  }

  if (m_editor && m_scene)
    m_editor->SyncRuntimeEntityIds(m_scene->GetRegistry());
  RefreshCameraFromSceneCamera();
  RememberRecentProject(&m_homeDocument, projectRoot);
  std::string saveError;
  SaveEditorHomeDocument(&m_homeDocument, &saveError);
  m_launcherError.clear();
  LogInfo("[Launcher] Opened project: {}", projectRoot.string());
  return true;
}

void LauncherEditorShell::CloseProject() {
  m_processRunner.Stop();

  if (m_runtime && m_runtime->GetCoordinator().IsActive()) {
    const SceneRuntimeOperationResult unload = m_runtime->Unload();
    if (!unload.ok)
      LogWarn("[Launcher] Runtime unload failed while closing project: {}",
              unload.error);
  }

  if (m_editor)
    m_editor->SaveWorkspaceStateNow();

  if (m_editor) {
    if (m_editor->IsActive())
      m_editor->Toggle();
    m_editor->SetCursorVisible(true);
  }

  if (m_scene)
    m_scene->Clear();

  ProjectPath::SetProjectRoot({});
  if (m_editor) {
    m_editor->ReloadWorkspaceStateFromDisk();
    m_editor->SetProjectBrowserRoot({});
  }
  m_projectDocument = {};
  m_projectRoot.clear();
}

void LauncherEditorShell::UnloadCurrentProjectState() {
  m_processRunner.Stop();
  if (m_runtime && m_runtime->GetCoordinator().IsActive()) {
    const SceneRuntimeOperationResult unload = m_runtime->Unload();
    if (!unload.ok)
      LogWarn("[Launcher] Runtime unload during project switch failed: {}",
              unload.error);
  }
  if (m_scene)
    m_scene->Clear();
  if (m_editor)
    m_editor->SaveWorkspaceStateNow();
}

void LauncherEditorShell::SetupEditorForProject(
    const fs::path &projectRoot, const Editor::SceneDocument &sceneDocument) {
  m_editor->ReloadWorkspaceStateFromDisk();
  m_editor->SetProjectBrowserRoot(projectRoot);
  m_editor->LoadDocument(sceneDocument);
  if (!m_editor->IsActive())
    m_editor->Toggle();
}

void LauncherEditorShell::Update() {
  m_processRunner.Poll();
  HandlePendingSceneReload();

  if (HasActiveProject() && m_editor && !m_editor->IsActive())
    CloseProject();
}

void LauncherEditorShell::RenderOverlay() {
  if (!HasActiveProject())
    RenderLauncher();
}

void LauncherEditorShell::HandlePendingSceneReload() {
  if (!HasActiveProject() || !m_editor || !m_runtime ||
      !m_editor->WantsSceneReload())
    return;

  if (const SceneRuntimeOperationResult reload =
          m_runtime->ReloadDocument(m_editor->GetPendingDocument());
      !reload.ok) {
    LogError("[Launcher] Runtime reload failed: {}", reload.error);
  } else {
    if (m_scene)
      m_editor->SyncRuntimeEntityIds(m_scene->GetRegistry());
    RefreshCameraFromSceneCamera();
  }
  m_editor->AcknowledgeReload();
}

void LauncherEditorShell::RefreshCameraFromSceneCamera() {
  if (!m_camera || !m_runtime || !m_runtime->GetSceneCamera().has_value())
    return;

  const RuntimeSceneCamera &sceneCamera = *m_runtime->GetSceneCamera();
  m_camera->position = sceneCamera.position;
  m_camera->target = sceneCamera.position +
                     ForwardFromYawPitch(sceneCamera.yaw, sceneCamera.pitch);
  m_camera->fovY = sceneCamera.fovY;
  m_camera->zNear = sceneCamera.nearClip;
  m_camera->zFar = sceneCamera.farClip;
}

bool LauncherEditorShell::OpenProjectFromPicker(std::string *outError) {
  const fs::path pickedPath = PickFolderPath(
      "Select Horo project folder",
      DefaultBrowseDirectory(m_homeDocument.state.lastProjectPath));
  if (pickedPath.empty()) {
    if (outError)
      outError->clear();
    return false;
  }

  return OpenProject(pickedPath, outError);
}

void LauncherEditorShell::OnPathsDropped(int pathCount, const char **utf8Paths,
                                         float dropX, float dropY) {
  const ImportProjectDropTarget &target = m_importProjectDropTarget;
  if (const bool insideImportTarget = target.valid && dropX >= target.minX &&
                                     dropX <= target.maxX &&
                                     dropY >= target.minY && dropY <= target.maxY;
      HasActiveProject() || pathCount <= 0 || !utf8Paths || !insideImportTarget)
    return;

  for (int i = 0; i < pathCount; ++i) {
    if (!utf8Paths[i] || !*utf8Paths[i])
      continue;

    std::string openError;
    if (OpenProject(fs::path(reinterpret_cast<const char8_t *>(utf8Paths[i])),
                    &openError)) {
      m_launcherError.clear();
      return;
    }
    if (!openError.empty())
      m_launcherError = openError;
  }
}

void LauncherEditorShell::RenderLauncher() {
  const ImGuiViewport *viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(viewport->Pos);
  ImGui::SetNextWindowSize(viewport->Size);
  const ImGuiWindowFlags flags =
      ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
      ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
  ImGui::Begin("Horo Launcher", nullptr, flags);
  ImGui::PopStyleVar(2);

  Horo::Launcher::UI::DrawBackdrop(ImGui::GetWindowDrawList(), ImGui::GetWindowPos(),
               ImGui::GetWindowSize());

  const float outerPadding = 24.0f;
  const float sidebarWidth =
      std::max(180.0f, std::min(252.0f, viewport->Size.x * 0.19f));
  const float contentGap = 18.0f;
  const float mainWidth =
      std::max(420.0f, viewport->Size.x - outerPadding * 2.0f - sidebarWidth -
                           contentGap);
  const float fullHeight =
      std::max(120.0f, viewport->Size.y - outerPadding * 2.0f);

  ImGui::SetCursorPos(ImVec2(outerPadding, outerPadding));
  RenderLauncherSidebar(sidebarWidth, fullHeight);

  ImGui::SameLine(0.0f, contentGap);
  RenderLauncherMainContent(mainWidth, fullHeight);

  ImGui::End();
}

void LauncherEditorShell::RenderLauncherSidebar(float sidebarWidth,
                                                float fullHeight) {
  const Horo::UI::HoroTheme &theme = Horo::UI::GetHoroTheme();
  ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, theme.windowRounding);
  ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(22.0f, 22.0f));
  ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
  ImGui::BeginChild("LauncherSidebar", ImVec2(sidebarWidth, fullHeight), false);

  Horo::Launcher::UI::RenderLauncherBrand(EnsureLauncherLogoTexture().get());
  ImGui::Dummy(ImVec2(0.0f, 16.0f));

  Horo::Launcher::UI::SidebarNavItem("Home", true);
  Horo::Launcher::UI::SidebarNavItem("Projects", false);
  Horo::Launcher::UI::SidebarNavItem("Templates", false);
  Horo::Launcher::UI::SidebarNavItem("Learn", false);

  const float footerHeight = 116.0f;
  if (const float remainingHeight = ImGui::GetContentRegionAvail().y;
      remainingHeight > footerHeight) {
    ImGui::Dummy(ImVec2(0.0f, remainingHeight - footerHeight));
  } else {
    ImGui::Dummy(ImVec2(0.0f, 18.0f));
  }
  Horo::Launcher::UI::SidebarFooterSeparator();
  Horo::Launcher::UI::SidebarFooterImageItem("Join Community", EnsureDiscordIconTexture().get());
  Horo::Launcher::UI::SidebarFooterItem("Documentation", Horo::Launcher::UI::SidebarFooterIcon::Documentation);
  ImGui::Dummy(ImVec2(0.0f, 6.0f));
  ImGui::TextDisabled("Horo Engine v%s", HORO_ENGINE_VERSION);
  ImGui::EndChild();
  ImGui::PopStyleColor();
  ImGui::PopStyleVar(3);
}

void LauncherEditorShell::RenderLauncherMainContent(float mainWidth,
                                                    float fullHeight) {
  const Horo::UI::HoroTheme &theme = Horo::UI::GetHoroTheme();
  ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, theme.windowRounding);
  ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
  ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
  ImGui::BeginChild("LauncherMainContent", ImVec2(mainWidth, fullHeight),
                    false);
  ImGui::PopStyleColor();
  ImGui::PopStyleVar(3);
  const float mainContentWidth = ImGui::GetContentRegionAvail().x;

  RenderLauncherHero(mainContentWidth);

  if (!m_launcherError.empty()) {
    ImGui::Dummy(ImVec2(0.0f, 8.0f));
    ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.45f, 1.0f), "%s",
                       m_launcherError.c_str());
  }

  ImGui::Dummy(ImVec2(0.0f, 18.0f));
  const float lowerHeight = ImGui::GetContentRegionAvail().y;

  RenderNewProjectPanel(mainContentWidth);

  ImGui::Dummy(ImVec2(0.0f, 12.0f));
  const float newProjectSpace =
      m_newProjectAdvancedSettingsOpen ? 405.0f : 360.0f;
  const float recentHeight =
      std::max(160.0f, std::min(320.0f, lowerHeight - newProjectSpace));
  RenderRecentProjectsList(mainContentWidth, recentHeight);

  ImGui::EndChild();
}

void LauncherEditorShell::RenderLauncherHero(float contentWidth) {
  const Horo::UI::HoroTheme &theme = Horo::UI::GetHoroTheme();
  m_importProjectDropTarget.valid = false;
  const float heroHeight = 152.0f;
  ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, theme.panelRounding);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18.0f, 18.0f));
  ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
  ImGui::BeginChild("LauncherHero", ImVec2(contentWidth, heroHeight), false,
                    ImGuiWindowFlags_NoScrollbar |
                        ImGuiWindowFlags_NoScrollWithMouse);
  ImGui::PopStyleColor();
  ImGui::PopStyleVar(2);

  ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.97f, 0.98f, 1.0f, 1.0f));
  ImGui::SetWindowFontScale(1.45f);
  ImGui::TextUnformatted("Welcome to Horo Engine");
  ImGui::SetWindowFontScale(1.0f);
  ImGui::PopStyleColor();
  ImGui::TextColored(theme.textMuted,
                     "Build worlds. Tell stories. Create your games.");
  ImGui::Dummy(ImVec2(0.0f, 24.0f));

  const float actionGap = 6.0f;
  const float actionWidth =
      std::max(260.0f, (ImGui::GetContentRegionAvail().x - actionGap) * 0.5f);
  const ImVec2 actionSize(actionWidth, 72.0f);
  if (Horo::Launcher::UI::LauncherActionCard("##open-existing-project-action",
                               "Open Existing Project",
                               "Open a project from your computer",
                               Horo::Launcher::UI::LauncherActionIcon::Folder, actionSize)) {
    std::string openError;
    if (!OpenProjectFromPicker(&openError) && !openError.empty())
      m_launcherError = openError;
  }
  ImGui::SameLine(0.0f, actionGap);
  if (Horo::Launcher::UI::LauncherActionCard("##import-project-action", "Import Project",
                               "Import a project from disk",
                               Horo::Launcher::UI::LauncherActionIcon::Import, actionSize)) {
    std::string openError;
    if (!OpenProjectFromPicker(&openError) && !openError.empty())
      m_launcherError = openError;
  }
  const ImVec2 importMin = ImGui::GetItemRectMin();
  const ImVec2 importMax = ImGui::GetItemRectMax();
  m_importProjectDropTarget = {
      .valid = true,
      .minX = importMin.x,
      .minY = importMin.y,
      .maxX = importMax.x,
      .maxY = importMax.y,
  };

  ImGui::EndChild();
}

void LauncherEditorShell::RenderNewProjectPanel(float contentWidth) {
  const Horo::UI::HoroTheme &theme = Horo::UI::GetHoroTheme();
  const float panelHeight = m_newProjectAdvancedSettingsOpen ? 326.0f : 282.0f;
  ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.06f, 0.10f, 0.16f, 0.80f));
  ImGui::PushStyleColor(ImGuiCol_Border, theme.border);
  ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, theme.panelRounding);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(22.0f, 18.0f));
  ImGui::BeginChild("LauncherPanel", ImVec2(contentWidth, panelHeight), true,
                    ImGuiWindowFlags_NoScrollbar |
                        ImGuiWindowFlags_NoScrollWithMouse);
  ImGui::PopStyleVar(2);
  ImGui::PopStyleColor(2);
  const float innerWidth = ImGui::GetContentRegionAvail().x;

  Horo::Launcher::UI::RenderCreateProjectHeader();
  ImGui::Dummy(ImVec2(0.0f, 14.0f));
  ImGui::GetWindowDrawList()->AddLine(
      ImGui::GetCursorScreenPos(),
      ImVec2(ImGui::GetCursorScreenPos().x + innerWidth,
             ImGui::GetCursorScreenPos().y),
      ImGui::ColorConvertFloat4ToU32(ImVec4(0.15f, 0.24f, 0.35f, 0.38f)), 1.0f);
  ImGui::Dummy(ImVec2(0.0f, 14.0f));

  const float rowStartX = ImGui::GetCursorPosX();
  const float rowStartY = ImGui::GetCursorPosY();
  const float mainRowHeight = 98.0f;

  RenderNewProjectFormRow(innerWidth, rowStartX, rowStartY);

  ImGui::SetCursorPosY(rowStartY + mainRowHeight + 32.0f);
  const float advancedRowY = ImGui::GetCursorPosY();
  RenderNewProjectActions(innerWidth, rowStartX, advancedRowY);
  ImGui::EndChild();
}

void LauncherEditorShell::RenderNewProjectFormRow(float innerWidth,
                                                  float rowStartX,
                                                  float rowStartY) {
  const float sectionGap = 18.0f;
  const float dividerGap = 14.0f;
  const float mainRowHeight = 98.0f;
  const float templateWidth = std::clamp(innerWidth * 0.37f, 300.0f, 430.0f);
  const float formWidth =
      std::max(360.0f, innerWidth - templateWidth - sectionGap);
  const float formGap = 24.0f;
  const float formFieldWidth =
      std::max(300.0f, formWidth - (dividerGap * 2.0f) - 1.0f);
  const float nameWidth = std::max(220.0f, formFieldWidth * 0.43f);
  const float locationWidth =
      std::max(220.0f, formFieldWidth - nameWidth - formGap);
  const float nameX = rowStartX;
  const float nameDividerX = nameX + nameWidth + dividerGap;
  const float locationX = nameDividerX + 1.0f + dividerGap;
  const float templateDividerX = locationX + locationWidth + sectionGap;
  const float templateX = templateDividerX + 1.0f + sectionGap;

  ImGui::SetCursorPos(ImVec2(nameX, rowStartY));
  ImGui::BeginGroup();
  Horo::UI::LabeledInput("Project Name", "##new-project-name",
                     m_newProjectNameInput.data(), m_newProjectNameInput.size(),
                     nameWidth);
  ImGui::EndGroup();

  ImGui::SetCursorPos(ImVec2(nameDividerX, rowStartY));
  Horo::Launcher::UI::VerticalDivider(mainRowHeight);
  ImGui::SetCursorPos(ImVec2(locationX, rowStartY));
  ImGui::BeginGroup();
  const float browseWidth = 94.0f;
  Horo::UI::LabeledInput("Location", "##new-project-path",
                     m_newProjectPathInput.data(), m_newProjectPathInput.size(),
                     std::max(120.0f, locationWidth - browseWidth - 8.0f));
  ImGui::SameLine(0.0f, 8.0f);
  if (Horo::UI::SecondaryButton("Browse", ImVec2(browseWidth, 0.0f))) {
    const fs::path pickedLocation = PickFolderPath(
        "Select project location",
        DefaultBrowseDirectory(BufferToString(m_newProjectPathInput)));
    if (!pickedLocation.empty()) {
      const std::string projectName = BufferToString(m_newProjectNameInput);
      fs::path resolvedProjectPath = pickedLocation;
      if (!projectName.empty() && resolvedProjectPath.filename() != projectName)
        resolvedProjectPath /= projectName;
      CopyToBuffer(&m_newProjectPathInput,
                   resolvedProjectPath.lexically_normal().string());
      m_launcherError.clear();
    }
  }
  ImGui::EndGroup();

  ImGui::SetCursorPos(ImVec2(templateDividerX, rowStartY));
  Horo::Launcher::UI::VerticalDivider(mainRowHeight);
  ImGui::SetCursorPos(ImVec2(templateX, rowStartY));
  ImGui::BeginGroup();
  ImGui::TextDisabled("%s", "Template");
  ImGui::Dummy(ImVec2(0.0f, 6.0f));
  static int selectedTemplateIndex = 0;
  const float tileGap = 8.0f;
  const float tileWidth = (templateWidth - tileGap * 3.0f) * 0.25f;
  const ImVec2 tileSize(std::max(64.0f, tileWidth), 70.0f);
  if (Horo::Launcher::UI::TemplateTile("Empty", Horo::Launcher::UI::TemplateTileIcon::Cube,
                         selectedTemplateIndex == 0, true, tileSize))
    selectedTemplateIndex = 0;
  ImGui::SameLine(0.0f, tileGap);
  if (Horo::Launcher::UI::TemplateTile("2D", Horo::Launcher::UI::TemplateTileIcon::Image,
                         selectedTemplateIndex == 1, false, tileSize))
    selectedTemplateIndex = 1;
  ImGui::SameLine(0.0f, tileGap);
  if (Horo::Launcher::UI::TemplateTile("3D", Horo::Launcher::UI::TemplateTileIcon::Cube,
                         selectedTemplateIndex == 2, false, tileSize))
    selectedTemplateIndex = 2;
  ImGui::SameLine(0.0f, tileGap);
  if (Horo::Launcher::UI::TemplateTile("Sandbox", Horo::Launcher::UI::TemplateTileIcon::Sandbox,
                         selectedTemplateIndex == 3, false, tileSize))
    selectedTemplateIndex = 3;
  ImGui::EndGroup();
}

void LauncherEditorShell::RenderNewProjectActions(float innerWidth,
                                                  float rowStartX,
                                                  float advancedRowY) {
  Horo::Launcher::UI::AdvancedSettingsToggle(&m_newProjectAdvancedSettingsOpen);

  const float createButtonWidth = 210.0f;
  if (m_newProjectAdvancedSettingsOpen) {
    const float rendererControlX = rowStartX + 26.0f;
    ImGui::SetCursorPos(ImVec2(rendererControlX, advancedRowY + 38.0f));
    ImGui::TextDisabled("%s", "Renderer");
    ImGui::SameLine(0.0f, 10.0f);
    ImGui::SetNextItemWidth(132.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f, 8.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.04f, 0.08f, 0.13f, 0.92f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered,
                          ImVec4(0.06f, 0.11f, 0.18f, 0.96f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive,
                          ImVec4(0.08f, 0.15f, 0.25f, 1.0f));
    ImGui::Combo("##new-project-renderer", &m_newProjectRendererBackendIndex,
                 kRendererBackendLabels.data(),
                 static_cast<int>(kRendererBackendLabels.size()));
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar(2);
  }

  ImGui::SetCursorPos(
      ImVec2(rowStartX + innerWidth - createButtonWidth - 2.0f, advancedRowY));
  if (Horo::UI::PrimaryButton("Create Project   +",
                          ImVec2(createButtonWidth, 40.0f))) {
    std::string createError;
    if (!CreateProjectFromLauncher(&createError))
      m_launcherError = createError;
  }
}

void LauncherEditorShell::RenderRecentProjectsList(float contentWidth,
                                                   float panelHeight) {
  const Horo::UI::HoroTheme &theme = Horo::UI::GetHoroTheme();
  ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.06f, 0.10f, 0.16f, 0.80f));
  ImGui::PushStyleColor(ImGuiCol_Border, theme.border);
  ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, theme.panelRounding);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.0f, 14.0f));
  ImGui::BeginChild("RecentProjectsList", ImVec2(contentWidth, panelHeight),
                    true);
  ImGui::PopStyleVar(2);
  ImGui::PopStyleColor(2);
  const float listInnerWidth = ImGui::GetContentRegionAvail().x;

  ImGui::TextUnformatted("Recent Projects");
  ImGui::SameLine(std::max(0.0f, listInnerWidth - 70.0f));
  ImGui::TextColored(theme.textMuted, "View All  >");
  ImGui::Dummy(ImVec2(0.0f, 8.0f));

  if (m_homeDocument.state.recentProjects.empty()) {
    Horo::UI::CenteredEmptyState("No recent projects yet.");
  } else {
    int cardIndex = 0;
    for (const std::string &recentPath : m_homeDocument.state.recentProjects) {
      RenderRecentProjectCard(recentPath, cardIndex);
      ++cardIndex;
    }
  }
  ImGui::EndChild();
}

void LauncherEditorShell::RenderRecentProjectCard(const std::string &recentPath,
                                                  int cardIndex) {
  const Horo::UI::HoroTheme &theme = Horo::UI::GetHoroTheme();
  const fs::path path(recentPath);
  const fs::path normalizedPath = path.lexically_normal();
  std::string title = normalizedPath.filename().string();
  if (title.empty())
    title = normalizedPath.parent_path().filename().string();
  if (title.empty())
    title = recentPath;

  const std::string cardId = std::format("RecentProjectCard##{}", cardIndex);
  ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.07f, 0.11f, 0.17f, 0.72f));
  ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.15f, 0.24f, 0.35f, 0.62f));
  ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, theme.cardRounding);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f, 14.0f));
  ImGui::BeginChild(cardId.c_str(), ImVec2(-1.0f, 94.0f), true,
                    ImGuiWindowFlags_NoScrollbar |
                        ImGuiWindowFlags_NoScrollWithMouse);
  ImGui::PopStyleVar(2);
  ImGui::PopStyleColor(2);

  const float contentStartX = ImGui::GetCursorPosX();
  const float contentStartY = ImGui::GetCursorPosY();
  constexpr float openButtonWidth = 74.0f;
  constexpr float openButtonHeight = 38.0f;
  constexpr float menuButtonWidth = 34.0f;
  constexpr float actionGap = 20.0f;
  const float rowInnerWidth = ImGui::GetContentRegionAvail().x;
  const float openButtonX = contentStartX + rowInnerWidth - menuButtonWidth -
                            actionGap - openButtonWidth;
  const float menuButtonX = contentStartX + rowInnerWidth - menuButtonWidth;
  const float actionY = contentStartY + 14.0f;

  ImGui::SetCursorPos(ImVec2(contentStartX, contentStartY + 2.0f));
  ImGui::BeginGroup();
  ImGui::TextUnformatted(title.c_str());
  ImGui::Dummy(ImVec2(0.0f, 1.0f));
  ImGui::TextColored(theme.textMuted, "%s", recentPath.c_str());
  ImGui::Dummy(ImVec2(0.0f, 4.0f));
  Horo::Launcher::UI::DrawRecentProjectMetaIcon(ImGui::GetWindowDrawList(),
                            ImGui::GetCursorScreenPos(),
                            ImGui::ColorConvertFloat4ToU32(theme.textMuted));
  ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 18.0f);
  ImGui::TextColored(theme.textMuted, "%s", "Last opened: Recently");
  ImGui::EndGroup();

  ImGui::SetCursorPos(ImVec2(openButtonX, actionY));
  if (const std::string openLabel =
          std::format("Open##recent-open-{}", cardIndex);
      Horo::Launcher::UI::RecentProjectButton(openLabel.c_str(),
                                ImVec2(openButtonWidth, openButtonHeight))) {
    if (std::string openError; !OpenProject(path, &openError))
      m_launcherError = openError;
  }

  ImGui::SetCursorPos(ImVec2(menuButtonX, actionY));
  const std::string menuLabel = std::format("##recent-menu-{}", cardIndex);
  Horo::Launcher::UI::RecentProjectMenuButton(menuLabel.c_str(),
                                ImVec2(menuButtonWidth, openButtonHeight));
  ImGui::EndChild();
  ImGui::Dummy(ImVec2(0.0f, 8.0f));
}

std::shared_ptr<Texture> LauncherEditorShell::EnsureLauncherLogoTexture() {
  if (m_launcherLogoTexture)
    return m_launcherLogoTexture;

  if (Renderer::GetBackendId() != RenderBackendId::OpenGL)
    return {};

  const fs::path logoPath = Horo::Launcher::UI::ResolveLauncherVisualAsset("logo_with_title.png");
  if (logoPath.empty())
    return {};

  m_launcherLogoTexture =
      std::make_shared<Texture>(Texture::FromFile(logoPath.string(), false));
  return m_launcherLogoTexture;
}

std::shared_ptr<Texture> LauncherEditorShell::EnsureDiscordIconTexture() {
  if (m_discordIconTexture)
    return m_discordIconTexture;

  if (Renderer::GetBackendId() != RenderBackendId::OpenGL)
    return {};

  const fs::path iconPath = Horo::Launcher::UI::ResolveLauncherVisualAsset("discord-icon.png");
  if (iconPath.empty())
    return {};

  m_discordIconTexture =
      std::make_shared<Texture>(Texture::FromFile(iconPath.string(), false));
  return m_discordIconTexture;
}

bool LauncherEditorShell::CreateProjectFromLauncher(std::string *outError) {
  LauncherProjectDocument createdProject;
  const LauncherProjectTemplateRequest request{
      .projectRoot = BufferToString(m_newProjectPathInput),
      .projectName = BufferToString(m_newProjectNameInput),
      .sdkRoot = ResolveCommandSdkRoot(),
      .rendererBackend =
          RendererBackendIdFromIndex(m_newProjectRendererBackendIndex),
  };
  if (!CreateLauncherProjectTemplate(request, &createdProject, outError))
    return false;

  return OpenProject(request.projectRoot, outError);
}

fs::path LauncherEditorShell::ResolveCommandSdkRoot() const {
  std::vector<fs::path> candidates;
  const fs::path assetSdkRoot = NormalizePathForLookup(ProjectPath::SdkRoot());
  if (!assetSdkRoot.empty()) {
    candidates.push_back(assetSdkRoot);
    if (assetSdkRoot.filename() == "sdk")
      candidates.push_back(assetSdkRoot.parent_path());
  }

  std::error_code ec;
  if (const fs::path exeDir = NormalizePathForLookup(fs::current_path(ec));
      !ec && !exeDir.empty()) {
    candidates.push_back(exeDir);
    candidates.push_back(exeDir.parent_path());
    candidates.push_back(exeDir.parent_path().parent_path());
  }

  for (const fs::path &candidate : candidates) {
    if (IsInstalledEnginePrefix(candidate) ||
        IsBuildTreeEnginePrefix(candidate))
      return candidate;
  }

  if (!assetSdkRoot.empty() && assetSdkRoot.filename() == "sdk")
    return assetSdkRoot.parent_path();
  return assetSdkRoot;
}

fs::path
LauncherEditorShell::NormalizeProjectRootInput(const fs::path &rawPath) const {
  if (rawPath.empty())
    return {};

  std::error_code ec;
  fs::path normalized = rawPath;
  if (fs::is_regular_file(normalized, ec) &&
      normalized.filename() == "project.json")
    normalized = normalized.parent_path().parent_path();

  normalized = fs::weakly_canonical(normalized, ec);
  if (ec)
    normalized = fs::absolute(rawPath, ec);
  if (ec)
    normalized = rawPath;
  return normalized.lexically_normal();
}

fs::path
LauncherEditorShell::ResolveAssetPath(const std::string &rawPath) const {
  if (rawPath.empty())
    return {};

  if (fs::path path(rawPath); path.is_absolute())
    return path;
  return ProjectPath::Resolve(rawPath);
}

fs::path LauncherEditorShell::ResolveShaderPath(const char *fileName) const {
  const std::array<fs::path, 4> candidates = {
      ProjectPath::ResolveSdk(std::string("renderer/shaders/") + fileName),
      ProjectPath::ResolveSdk(std::string("bin/shaders/") + fileName),
      ProjectPath::ResolveSdk(std::string("sdk/renderer/shaders/") + fileName),
      ProjectPath::Root() / "renderer" / "shaders" / fileName,
  };

  for (const fs::path &candidate : candidates) {
    std::error_code ec;
    if (fs::is_regular_file(candidate, ec) && !ec)
      return candidate;
  }
  return candidates.front();
}

std::shared_ptr<Shader> LauncherEditorShell::EnsureSceneShader() {
  if (!m_sceneShader) {
    m_sceneShader = std::make_shared<Shader>(
        Shader::FromFiles(ResolveShaderPath("basic.vert").string(),
                          ResolveShaderPath("basic.frag").string()));
  }
  return m_sceneShader;
}

std::shared_ptr<Mesh>
LauncherEditorShell::LoadMeshForTag(const std::string &meshTag) {
  if (meshTag.empty())
    return {};

  if (const auto it = m_meshCache.find(meshTag); it != m_meshCache.end())
    return it->second;

  auto mesh = std::make_shared<Mesh>();
  try {
    if (meshTag == "box")
      *mesh = Mesh::CreateBox();
    else if (meshTag == "sphere")
      *mesh = Mesh::CreateSphere();
    else if (meshTag == "cylinder")
      *mesh = Mesh::CreateCylinder();
    else if (meshTag == "pyramid")
      *mesh = Mesh::CreatePyramid();
    else if (meshTag == "plane")
      *mesh = Mesh::CreatePlane();
    else
      *mesh = ObjLoader::Load(ResolveAssetPath(meshTag).string());
  } catch (const ObjLoader::ObjLoaderException &e) {
    LogWarn("[Launcher] Failed to load mesh '{}': {}", meshTag, e.what());
    return {};
  }

  m_meshCache[meshTag] = mesh;
  return mesh;
}

std::shared_ptr<Texture>
LauncherEditorShell::LoadTexture(const std::string &rawPath) {
  if (rawPath.empty())
    return {};

  const fs::path path = ResolveAssetPath(rawPath);
  const std::string key = path.generic_string();
  if (const auto it = m_textureCache.find(key); it != m_textureCache.end())
    return it->second;

  auto texture = std::make_shared<Texture>(Texture::FromFile(path.string()));
  if (!texture || !texture->IsValid())
    return {};
  m_textureCache[key] = texture;
  return texture;
}
} // namespace Horo::Launcher
