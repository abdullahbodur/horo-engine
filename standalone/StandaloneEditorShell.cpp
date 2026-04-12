#include "standalone/StandaloneEditorShell.h"

#include <algorithm>
#include <cmath>
#include <system_error>

#include <imgui.h>

#include "core/Logger.h"
#include "core/ProjectPath.h"
#include "editor/SceneSerializer.h"
#include "math/MathUtils.h"
#include "math/Quaternion.h"
#include "renderer/ObjLoader.h"
#include "scene/Entity.h"
#include "scene/components/MeshComponent.h"
#include "scene/components/TransformComponent.h"
#include "standalone/NativeFolderDialog.h"
#include "standalone/StandaloneProjectTemplate.h"

namespace Monolith::Standalone {

namespace fs = std::filesystem;

namespace {

std::string BufferToString(const std::array<char, 512>& buffer) {
  return std::string(buffer.data());
}

std::string BufferToString(const std::array<char, 256>& buffer) {
  return std::string(buffer.data());
}

template <size_t N>
void CopyToBuffer(std::array<char, N>* buffer, const std::string& value) {
  if (!buffer)
    return;
  buffer->fill('\0');
  if (value.empty())
    return;
  const size_t count = std::min(value.size(), N - 1);
  std::copy_n(value.data(), count, buffer->data());
  (*buffer)[count] = '\0';
}

Vec3 ForwardFromYawPitch(float yawDeg, float pitchDeg) {
  const float yawRad = ToRadians(yawDeg);
  const float pitchRad = ToRadians(std::clamp(pitchDeg, -89.0f, 89.0f));
  return {-std::sin(yawRad) * std::cos(pitchRad),
          std::sin(pitchRad),
          -std::cos(yawRad) * std::cos(pitchRad)};
}

fs::path DefaultBrowseDirectory(const fs::path& rawPath) {
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

}  // namespace

void StandaloneEditorShell::Attach(Editor::EditorLayer* editor,
                                   Scene* scene,
                                   SceneReferenceRuntime* runtime,
                                   Camera* camera) {
  m_editor = editor;
  m_scene = scene;
  m_runtime = runtime;
  m_camera = camera;
}

void StandaloneEditorShell::Initialize() {
  m_homeDocument = LoadEditorHomeDocument();
  if (m_homeDocument.parseError)
    LOG_WARN("[Standalone] Editor home settings load fallback: %s", m_homeDocument.error.c_str());
  PruneMissingRecentProjects(&m_homeDocument);
  std::string saveError;
  SaveEditorHomeDocument(&m_homeDocument, &saveError);

  CopyToBuffer(&m_newProjectNameInput, "MyHoroGame");
  CopyToBuffer(&m_newProjectPathInput, (fs::current_path() / "MyHoroGame").string());

  ConfigureRuntimeCallbacks();
}

void StandaloneEditorShell::Shutdown() {
  m_processRunner.Stop();
  std::string saveError;
  SaveEditorHomeDocument(&m_homeDocument, &saveError);
}

void StandaloneEditorShell::ConfigureRuntimeCallbacks() {
  if (!m_editor || !m_runtime)
    return;

  m_editor->SetOverlayRenderCallback([this]() { RenderOverlay(); });
  m_runtime->SetPropEntityCreatedCallback(
      [this](const RuntimeSceneProp& prop, Entity entity, Scene& sceneRef) {
        MeshComponent component;
        component.meshTag = prop.meshTag;
        component.mesh = LoadMeshForTag(prop.meshTag);
        component.material = std::make_shared<Material>();
        component.material->shader = EnsureSceneShader();
        component.material->albedoMap = LoadTexture(prop.albedoMap);
        sceneRef.registry.Add<MeshComponent>(entity, std::move(component));
      });
  m_editor->SetTransformCallback([this](const Editor::SceneObject& object) {
    if (!m_scene)
      return;

    const auto runtimeEntityIt = object.props.find("_eid");
    if (runtimeEntityIt != object.props.end()) {
      try {
        const Entity entity = static_cast<Entity>(std::stoul(runtimeEntityIt->second));
        if (m_scene->registry.IsAlive(entity) && m_scene->registry.Has<TransformComponent>(entity)) {
          auto& transform = m_scene->registry.Get<TransformComponent>(entity);
          transform.current.position = object.position;
          transform.previous.position = object.position;
          transform.current.scale = object.scale;
          transform.previous.scale = object.scale;
          transform.current.rotation = Quaternion::FromEuler(
              ToRadians(object.pitch), ToRadians(object.yaw), ToRadians(object.roll));
          transform.previous.rotation = transform.current.rotation;
        }
      } catch (...) {
      }
    }

    std::string lightError;
    if (!m_runtime->UpdateLiveLight(object, &lightError) && !lightError.empty() &&
        object.type == Editor::SceneObjectType::Light) {
      LOG_WARN("[Standalone] Live light update failed: %s", lightError.c_str());
    }
  });
}

bool StandaloneEditorShell::OpenProject(const fs::path& projectPath, std::string* outError) {
  if (outError)
    outError->clear();

  const fs::path projectRoot = NormalizeProjectRootInput(projectPath);
  if (projectRoot.empty()) {
    if (outError)
      *outError = "Project path is empty.";
    return false;
  }
  if (!IsStandaloneProjectRoot(projectRoot)) {
    if (outError)
      *outError = "Project manifest not found at " + ResolveProjectManifestPath(projectRoot).string();
    return false;
  }

  const StandaloneProjectDocument projectDocument = LoadProjectManifestDocument(projectRoot);
  if (projectDocument.parseError) {
    if (outError)
      *outError = projectDocument.error;
    return false;
  }

  Editor::SceneDocument sceneDocument;
  try {
    const fs::path scenePath = ResolveAssetPath(
        (projectRoot / projectDocument.manifest.defaultScene).lexically_normal().generic_string());
    sceneDocument = Editor::SceneSerializer::LoadFromFile(scenePath.string());
  } catch (const std::exception& e) {
    if (outError)
      *outError = e.what();
    return false;
  }

  m_processRunner.Stop();
  if (m_runtime && m_runtime->GetCoordinator().IsActive()) {
    const SceneRuntimeOperationResult unload = m_runtime->Unload();
    if (!unload.ok)
      LOG_WARN("[Standalone] Runtime unload during project switch failed: %s", unload.error.c_str());
  }
  if (m_scene)
    m_scene->Clear();

  if (m_editor)
    m_editor->SaveWorkspaceStateNow();
  m_projectRoot = projectRoot;
  m_projectDocument = projectDocument;
  ProjectPath::SetProjectRoot(projectRoot);

  if (m_editor) {
    m_editor->ReloadWorkspaceStateFromDisk();
    m_editor->SetProjectBrowserRoot(projectRoot);
    m_editor->LoadDocument(sceneDocument);
    if (!m_editor->IsActive())
      m_editor->Toggle();
  }

  if (m_runtime) {
    const SceneRuntimeOperationResult load = m_runtime->LoadDocument(sceneDocument);
    if (!load.ok) {
      if (outError)
        *outError = load.error;
      CloseProject();
      return false;
    }
  }

  if (m_editor && m_scene)
    m_editor->SyncRuntimeEntityIds(m_scene->registry);
  RefreshCameraFromSceneCamera();
  RememberRecentProject(&m_homeDocument, projectRoot);
  std::string saveError;
  SaveEditorHomeDocument(&m_homeDocument, &saveError);
  m_launcherError.clear();
  LOG_INFO("[Standalone] Opened project: %s", projectRoot.string().c_str());
  return true;
}

void StandaloneEditorShell::CloseProject() {
  m_processRunner.Stop();

  if (m_runtime && m_runtime->GetCoordinator().IsActive()) {
    const SceneRuntimeOperationResult unload = m_runtime->Unload();
    if (!unload.ok)
      LOG_WARN("[Standalone] Runtime unload failed while closing project: %s", unload.error.c_str());
  }

  if (m_editor)
    m_editor->SaveWorkspaceStateNow();

  if (m_editor) {
    if (m_editor->IsActive())
      m_editor->Toggle();
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

void StandaloneEditorShell::Update() {
  m_processRunner.Poll();
  HandlePendingSceneReload();

  if (HasActiveProject() && m_editor && !m_editor->IsActive())
    CloseProject();
}

void StandaloneEditorShell::RenderOverlay() {
  if (!HasActiveProject())
    RenderLauncher();
  else
    RenderProjectToolbar();
}

void StandaloneEditorShell::HandlePendingSceneReload() {
  if (!HasActiveProject() || !m_editor || !m_runtime || !m_editor->WantsSceneReload())
    return;

  const SceneRuntimeOperationResult reload = m_runtime->ReloadDocument(m_editor->GetPendingDocument());
  if (!reload.ok)
    LOG_ERROR("[Standalone] Runtime reload failed: %s", reload.error.c_str());
  else {
    if (m_scene)
      m_editor->SyncRuntimeEntityIds(m_scene->registry);
    RefreshCameraFromSceneCamera();
  }
  m_editor->AcknowledgeReload();
}

void StandaloneEditorShell::RefreshCameraFromSceneCamera() {
  if (!m_camera || !m_runtime || !m_runtime->GetSceneCamera().has_value())
    return;

  const RuntimeSceneCamera& sceneCamera = *m_runtime->GetSceneCamera();
  m_camera->position = sceneCamera.position;
  m_camera->target = sceneCamera.position + ForwardFromYawPitch(sceneCamera.yaw, sceneCamera.pitch);
  m_camera->fovY = sceneCamera.fovY;
  m_camera->zNear = sceneCamera.nearClip;
  m_camera->zFar = sceneCamera.farClip;
}

bool StandaloneEditorShell::OpenProjectFromPicker(std::string* outError) {
  const fs::path pickedPath =
      PickFolderPath("Select Horo project folder",
                     DefaultBrowseDirectory(m_homeDocument.state.lastProjectPath));
  if (pickedPath.empty()) {
    if (outError)
      outError->clear();
    return false;
  }

  return OpenProject(pickedPath, outError);
}

void StandaloneEditorShell::RenderLauncher() {
  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(viewport->Pos);
  ImGui::SetNextWindowSize(viewport->Size);
  const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;
  ImGui::Begin("Horo Launcher", nullptr, flags);

  const float outerPadding = 20.0f;
  const float contentWidth = std::min(viewport->Size.x - outerPadding * 2.0f, 1120.0f);
  const float panelX = std::max(outerPadding, (viewport->Size.x - contentWidth) * 0.5f);

  const auto centerText = [&](const char* text, const float scale = 1.0f) {
    ImGui::SetWindowFontScale(scale);
    const float textWidth = ImGui::CalcTextSize(text).x;
    if (textWidth < contentWidth)
      ImGui::SetCursorPosX(panelX + (contentWidth - textWidth) * 0.5f);
    ImGui::TextUnformatted(text);
    ImGui::SetWindowFontScale(1.0f);
  };

  const auto modeButton = [&](const char* title, const bool selected, const ImVec2& size) -> bool {
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(18.0f, 18.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f);
    if (selected) {
      ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.24f, 0.54f, 0.93f, 1.0f));
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.28f, 0.58f, 0.97f, 1.0f));
      ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.20f, 0.45f, 0.82f, 1.0f));
    } else {
      ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.14f, 0.18f, 0.25f, 1.0f));
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.18f, 0.24f, 0.34f, 1.0f));
      ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.16f, 0.22f, 0.31f, 1.0f));
    }
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.94f, 0.96f, 1.0f, 1.0f));
    const bool pressed = ImGui::Button(title, size);
    ImGui::PopStyleColor(4);
    ImGui::PopStyleVar(2);
    return pressed;
  };

  const auto primaryButton = [&](const char* label, const ImVec2& size) -> bool {
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(18.0f, 16.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.41f, 0.68f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.24f, 0.46f, 0.75f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.18f, 0.36f, 0.62f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.96f, 0.97f, 1.0f, 1.0f));
    const bool pressed = ImGui::Button(label, size);
    ImGui::PopStyleColor(4);
    ImGui::PopStyleVar(2);
    return pressed;
  };

  const auto secondaryButton = [&](const char* label, const ImVec2& size) -> bool {
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(14.0f, 12.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f, 0.15f, 0.20f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.16f, 0.20f, 0.28f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.14f, 0.18f, 0.24f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.92f, 0.94f, 0.98f, 1.0f));
    const bool pressed = ImGui::Button(label, size);
    ImGui::PopStyleColor(4);
    ImGui::PopStyleVar(2);
    return pressed;
  };

  const auto recentProjectButton = [&](const char* title, const ImVec2& size) -> bool {
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(14.0f, 14.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.10f, 0.12f, 0.16f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.16f, 0.20f, 0.28f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.15f, 0.18f, 0.24f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.94f, 0.96f, 1.0f, 1.0f));
    const bool pressed = ImGui::Button(title, size);
    ImGui::PopStyleColor(4);
    ImGui::PopStyleVar(2);
    return pressed;
  };

  const auto labeledInput = [](const char* title, const char* id, char* buffer, const size_t bufferSize) {
    ImGui::TextDisabled("%s", title);
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputText(id, buffer, bufferSize);
  };

  const auto centeredEmptyState = [](const char* text) {
    const float regionHeight = ImGui::GetContentRegionAvail().y;
    if (regionHeight > 40.0f)
      ImGui::SetCursorPosY(ImGui::GetCursorPosY() + regionHeight * 0.25f);
    const float textWidth = ImGui::CalcTextSize(text).x;
    const float availableWidth = ImGui::GetContentRegionAvail().x;
    if (textWidth < availableWidth)
      ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (availableWidth - textWidth) * 0.5f);
    ImGui::TextDisabled("%s", text);
  };

  ImGui::SetCursorPos(ImVec2(panelX, outerPadding));
  ImGui::BeginGroup();

  ImGui::Dummy(ImVec2(0.0f, 6.0f));
  centerText("Welcome to Horo Editor", 1.95f);
  ImGui::Dummy(ImVec2(0.0f, 6.0f));
  centerText("Open an existing game project or start a new one.", 1.28f);
  ImGui::Dummy(ImVec2(0.0f, 16.0f));

  if (!m_launcherError.empty()) {
    ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.45f, 1.0f), "%s", m_launcherError.c_str());
    ImGui::Dummy(ImVec2(0.0f, 6.0f));
  }

  const float actionGap = 12.0f;
  const float actionWidth = (contentWidth - actionGap) * 0.5f;
  const ImVec2 actionSize(actionWidth, 56.0f);
  if (modeButton("Open Existing Project", false, actionSize)) {
    std::string openError;
    if (!OpenProjectFromPicker(&openError) && !openError.empty())
      m_launcherError = openError;
  }
  ImGui::SameLine(0.0f, actionGap);
  modeButton("Create New Project", true, actionSize);

  ImGui::Dummy(ImVec2(0.0f, 14.0f));

  const float panelHeight = 252.0f;
  ImGui::BeginChild("LauncherPanel",
                    ImVec2(contentWidth, panelHeight),
                    true,
                    ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

  ImGui::TextUnformatted("Create New Project");
  ImGui::TextDisabled("Pick a location and Horo will scaffold a minimal standalone-ready project.");
  ImGui::Dummy(ImVec2(0.0f, 8.0f));

  labeledInput("Project Name", "##new-project-name", m_newProjectNameInput.data(), m_newProjectNameInput.size());
  ImGui::Dummy(ImVec2(0.0f, 6.0f));
  labeledInput("Project Path", "##new-project-path", m_newProjectPathInput.data(), m_newProjectPathInput.size());
  ImGui::Dummy(ImVec2(0.0f, 6.0f));
  if (secondaryButton("Browse Location...", ImVec2(-1.0f, 34.0f))) {
    const fs::path pickedLocation =
        PickFolderPath("Select project location",
                       DefaultBrowseDirectory(BufferToString(m_newProjectPathInput)));
    if (!pickedLocation.empty()) {
      const std::string projectName = BufferToString(m_newProjectNameInput);
      fs::path resolvedProjectPath = pickedLocation;
      if (!projectName.empty() && resolvedProjectPath.filename() != projectName)
        resolvedProjectPath /= projectName;
      CopyToBuffer(&m_newProjectPathInput, resolvedProjectPath.lexically_normal().string());
      m_launcherError.clear();
    }
  }
  ImGui::Dummy(ImVec2(0.0f, 10.0f));
  if (primaryButton("Create Project", ImVec2(-1.0f, 40.0f))) {
    std::string createError;
    if (!CreateProjectFromLauncher(&createError))
      m_launcherError = createError;
  }
  ImGui::EndChild();

  ImGui::Dummy(ImVec2(0.0f, 12.0f));
  ImGui::TextUnformatted("Recent Projects");
  ImGui::TextDisabled("Resume work from your latest standalone projects.");
  ImGui::Dummy(ImVec2(0.0f, 6.0f));

  const float recentHeight =
      std::max(160.0f, std::min(260.0f, viewport->Size.y - outerPadding * 2.0f - 320.0f));
  ImGui::BeginChild("RecentProjectsList", ImVec2(contentWidth, recentHeight), true);
  if (m_homeDocument.state.recentProjects.empty()) {
    centeredEmptyState("No recent projects yet.");
  } else {
    for (const std::string& recentPath : m_homeDocument.state.recentProjects) {
      const fs::path path(recentPath);
      const std::string title = path.filename().empty() ? recentPath : path.filename().string();
      if (recentProjectButton(title.c_str(), ImVec2(-1.0f, 38.0f))) {
        std::string openError;
        if (!OpenProject(path, &openError))
          m_launcherError = openError;
      }
      ImGui::TextDisabled("%s", recentPath.c_str());
      ImGui::Dummy(ImVec2(0.0f, 8.0f));
    }
  }
  ImGui::EndChild();

  ImGui::EndGroup();
  ImGui::End();
}

void StandaloneEditorShell::RenderProjectToolbar() {
  ImGui::SetNextWindowPos(ImVec2(16.0f, 16.0f), ImGuiCond_Always);
  ImGui::SetNextWindowBgAlpha(0.92f);
  const ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse |
                                 ImGuiWindowFlags_NoSavedSettings;
  ImGui::Begin("Standalone Project", nullptr, flags);

  ImGui::TextUnformatted(m_projectDocument.manifest.projectName.c_str());
  ImGui::TextDisabled("%s", m_projectRoot.string().c_str());
  ImGui::Separator();

  if (!m_processRunner.IsActive()) {
    if (ImGui::Button("Configure"))
      ExecuteManifestCommand(m_projectDocument.manifest.configureCommand, "configure");
    ImGui::SameLine();
    if (ImGui::Button("Build"))
      ExecuteManifestCommand(m_projectDocument.manifest.buildCommand, "build");
    ImGui::SameLine();
    if (ImGui::Button("Run Game"))
      ExecuteManifestCommand(m_projectDocument.manifest.runCommand, "run");
  } else {
    if (ImGui::Button("Stop Process"))
      m_processRunner.Stop();
  }

  ImGui::SameLine();
  if (ImGui::Button("Back To Home"))
    CloseProject();

  const ExternalProcessStatus& status = m_processRunner.GetStatus();
  if (!status.label.empty()) {
    ImGui::Separator();
    ImGui::Text("Last command: %s", status.label.c_str());
    if (!status.commandLine.empty())
      ImGui::TextWrapped("%s", status.commandLine.c_str());
    if (status.active)
      ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.55f, 1.0f), "Running");
    else if (status.finished && status.terminatedByUser)
      ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.35f, 1.0f), "Stopped by user");
    else if (status.finished)
      ImGui::Text("Exit code: %d", status.exitCode);
    if (!status.error.empty())
      ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.45f, 1.0f), "%s", status.error.c_str());
  }

  ImGui::End();
}

void StandaloneEditorShell::ExecuteManifestCommand(const StandaloneProjectCommand& command,
                                                   const std::string& label) {
  std::string resolveError;
  ResolvedStandaloneCommand resolved;
  if (!ResolveStandaloneCommand(command,
                                m_projectRoot,
                                ProjectPath::SdkRoot(),
                                &resolved,
                                &resolveError)) {
    m_launcherError = resolveError;
    return;
  }

  std::string startError;
  if (!m_processRunner.Start(resolved, label, &startError))
    m_launcherError = startError;
}

bool StandaloneEditorShell::CreateProjectFromLauncher(std::string* outError) {
  StandaloneProjectDocument createdProject;
  const StandaloneProjectTemplateRequest request{
      .projectRoot = BufferToString(m_newProjectPathInput),
      .projectName = BufferToString(m_newProjectNameInput),
      .sdkRoot = ProjectPath::SdkRoot(),
  };
  if (!CreateStandaloneProjectTemplate(request, &createdProject, outError))
    return false;

  return OpenProject(request.projectRoot, outError);
}

fs::path StandaloneEditorShell::NormalizeProjectRootInput(const fs::path& rawPath) const {
  if (rawPath.empty())
    return {};

  std::error_code ec;
  fs::path normalized = rawPath;
  if (fs::is_regular_file(normalized, ec) && normalized.filename() == "project.json")
    normalized = normalized.parent_path().parent_path();

  normalized = fs::weakly_canonical(normalized, ec);
  if (ec)
    normalized = fs::absolute(rawPath, ec);
  if (ec)
    normalized = rawPath;
  return normalized.lexically_normal();
}

fs::path StandaloneEditorShell::ResolveAssetPath(const std::string& rawPath) const {
  if (rawPath.empty())
    return {};

  fs::path path(rawPath);
  if (path.is_absolute())
    return path;
  return ProjectPath::Resolve(rawPath);
}

fs::path StandaloneEditorShell::ResolveShaderPath(const char* fileName) const {
  const std::array<fs::path, 4> candidates = {
      ProjectPath::ResolveSdk(std::string("renderer/shaders/") + fileName),
      ProjectPath::ResolveSdk(std::string("bin/shaders/") + fileName),
      ProjectPath::ResolveSdk(std::string("sdk/renderer/shaders/") + fileName),
      ProjectPath::Root() / "renderer" / "shaders" / fileName,
  };

  for (const fs::path& candidate : candidates) {
    std::error_code ec;
    if (fs::is_regular_file(candidate, ec) && !ec)
      return candidate;
  }
  return candidates.front();
}

std::shared_ptr<Shader> StandaloneEditorShell::EnsureSceneShader() {
  if (!m_sceneShader) {
    m_sceneShader = std::make_shared<Shader>(
        Shader::FromFiles(ResolveShaderPath("basic.vert").string(), ResolveShaderPath("basic.frag").string()));
  }
  return m_sceneShader;
}

std::shared_ptr<Mesh> StandaloneEditorShell::LoadMeshForTag(const std::string& meshTag) {
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
  } catch (const std::exception& e) {
    LOG_WARN("[Standalone] Failed to load mesh '%s': %s", meshTag.c_str(), e.what());
    return {};
  }

  m_meshCache[meshTag] = mesh;
  return mesh;
}

std::shared_ptr<Texture> StandaloneEditorShell::LoadTexture(const std::string& rawPath) {
  if (rawPath.empty())
    return {};

  const fs::path path = ResolveAssetPath(rawPath);
  const std::string key = path.generic_string();
  if (const auto it = m_textureCache.find(key); it != m_textureCache.end())
    return it->second;

  try {
    auto texture = std::make_shared<Texture>(Texture::FromFile(path.string()));
    m_textureCache[key] = texture;
    return texture;
  } catch (const std::exception& e) {
    LOG_WARN("[Standalone] Failed to load texture '%s': %s", rawPath.c_str(), e.what());
    return {};
  }
}

}  // namespace Monolith::Standalone
