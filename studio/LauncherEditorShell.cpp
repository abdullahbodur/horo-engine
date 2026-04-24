#include "LauncherEditorShell.h"

#include <algorithm>
#include <cmath>
#include <string_view>
#include <stdexcept>
#include <system_error>

#include "core/Logger.h"
#include "core/ProjectPath.h"
#include "SceneSerializer.h"
#include "math/MathUtils.h"
#include "math/Quaternion.h"
#include "renderer/ObjLoader.h"
#include "scene/Entity.h"
#include "scene/components/MeshComponent.h"
#include "scene/components/TransformComponent.h"
#include "NativeFolderDialog.h"
#include "LauncherProjectTemplate.h"

namespace Monolith::Launcher {

namespace fs = std::filesystem;

namespace {

std::string BufferToString(const std::array<char, 512>& buffer) {
  return std::string(buffer.data());
}

std::string BufferToString(const std::array<char, 256>& buffer) {
  return std::string(buffer.data());
}

template <size_t N>
void CopyToBuffer(std::array<char, N>* buffer, std::string_view value) {
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

bool IsInstalledEnginePrefix(const fs::path& candidate) {
  if (candidate.empty())
    return false;

  std::error_code ec;
  return fs::is_regular_file(candidate / "lib" / "cmake" / "MonolithEngine" / "MonolithEngineConfig.cmake",
                             ec) &&
         !ec;
}

bool IsBuildTreeEnginePrefix(const fs::path& candidate) {
  if (candidate.empty())
    return false;

  std::error_code ec;
  const bool hasConfig = fs::is_regular_file(candidate / "MonolithEngineConfig.cmake", ec) && !ec;
  ec.clear();
  const bool hasTargets = fs::is_regular_file(candidate / "MonolithEngineTargets.cmake", ec) && !ec;
  return hasConfig && hasTargets;
}

fs::path NormalizePathForLookup(const fs::path& rawPath) {
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

void ApplyTransformUpdateFromObject(Scene* scene, const Editor::SceneObject& object) {
  if (!scene)
    return;
  if (const auto runtimeEntityIt = object.props.find("_eid"); runtimeEntityIt != object.props.end()) {
    try {
      const auto entity = static_cast<Entity>(std::stoul(runtimeEntityIt->second));
      if (scene->registry.IsAlive(entity) && scene->registry.Has<TransformComponent>(entity)) {
        auto& transform = scene->registry.Get<TransformComponent>(entity);
        transform.current.position = object.position;
        transform.previous.position = object.position;
        transform.current.scale = object.scale;
        transform.previous.scale = object.scale;
        transform.current.rotation = Quaternion::FromEuler(
            ToRadians(object.pitch), ToRadians(object.yaw), ToRadians(object.roll));
        transform.previous.rotation = transform.current.rotation;
      }
    } catch (const std::invalid_argument& e) {
      // UI state may still reference transient runtime ids while scene reload is in progress.
      LOG_DEBUG("[Launcher] Ignoring invalid runtime entity id '%s': %s",
                runtimeEntityIt->second.c_str(),
                e.what());
    } catch (const std::out_of_range& e) {
      // Ignore malformed runtime ids coming from stale serialized props.
      LOG_DEBUG("[Launcher] Ignoring out-of-range runtime entity id '%s': %s",
                runtimeEntityIt->second.c_str(),
                e.what());
    }
  }
}

}  // namespace

void LauncherEditorShell::Attach(Editor::EditorLayer* editor,
                                   Scene* scene,
                                   SceneReferenceRuntime* runtime,
                                   Camera* camera) {
  m_editor = editor;
  m_scene = scene;
  m_runtime = runtime;
  m_camera = camera;
}

void LauncherEditorShell::Initialize() {
  m_homeDocument = LoadEditorHomeDocument();
  if (m_homeDocument.parseError)
    LOG_WARN("[Launcher] Editor home settings load fallback: %s", m_homeDocument.error.c_str());
  PruneMissingRecentProjects(&m_homeDocument);
  std::string saveError;
  SaveEditorHomeDocument(&m_homeDocument, &saveError);

  CopyToBuffer(&m_newProjectNameInput, "MyHoroGame");
  CopyToBuffer(&m_newProjectPathInput, (fs::current_path() / "MyHoroGame").string());

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
    ApplyTransformUpdateFromObject(m_scene, object);

    std::string lightError;
    if (!m_runtime->UpdateLiveLight(object, &lightError) && !lightError.empty() &&
        object.type == Editor::SceneObjectType::Light) {
      LOG_WARN("[Launcher] Live light update failed: %s", lightError.c_str());
    }
  });
}

bool LauncherEditorShell::OpenProject(const fs::path& projectPath, std::string* outError) {  // NOSONAR: validates and wires project/runtime/editor state transitions
  if (outError)
    outError->clear();

  const fs::path projectRoot = NormalizeProjectRootInput(projectPath);
  if (projectRoot.empty()) {
    if (outError)
      *outError = "Project path is empty.";
    return false;
  }
  if (!IsLauncherProjectRoot(projectRoot)) {
    if (outError)
      *outError = "Project manifest not found at " + ResolveProjectManifestPath(projectRoot).string();
    return false;
  }

  const LauncherProjectDocument projectDocument = LoadProjectManifestDocument(projectRoot);
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
  } catch (const Editor::SceneSerializerException& e) {
    if (outError)
      *outError = e.what();
    return false;
  }

  m_processRunner.Stop();
  if (m_runtime && m_runtime->GetCoordinator().IsActive()) {
    const SceneRuntimeOperationResult unload = m_runtime->Unload();
    if (!unload.ok)
      LOG_WARN("[Launcher] Runtime unload during project switch failed: %s", unload.error.c_str());
  }
  if (m_scene)
    m_scene->Clear();

  m_projectRoot = projectRoot;
  m_projectDocument = projectDocument;
  ProjectPath::SetProjectRoot(projectRoot);

  if (m_editor) {
    m_editor->SetProjectBrowserRoot(projectRoot);
    m_editor->LoadDocument(sceneDocument);
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
  LOG_INFO("[Launcher] Opened project: %s", projectRoot.string().c_str());
  return true;
}

void LauncherEditorShell::CloseProject() {
  m_processRunner.Stop();

  if (m_runtime && m_runtime->GetCoordinator().IsActive()) {
    const SceneRuntimeOperationResult unload = m_runtime->Unload();
    if (!unload.ok)
      LOG_WARN("[Launcher] Runtime unload failed while closing project: %s", unload.error.c_str());
  }

  if (m_scene)
    m_scene->Clear();

  ProjectPath::SetProjectRoot({});
  if (m_editor) {
    m_editor->SetProjectBrowserRoot({});
  }
  m_projectDocument = {};
  m_projectRoot.clear();
}

void LauncherEditorShell::Update() {
  m_processRunner.Poll();
  HandlePendingSceneReload();
}

void LauncherEditorShell::RenderOverlay() {
  if (!HasActiveProject())
    RenderLauncher();
}

void LauncherEditorShell::HandlePendingSceneReload() {
  if (!HasActiveProject() || !m_editor || !m_runtime || !m_editor->WantsSceneReload())
    return;

  if (const SceneRuntimeOperationResult reload = m_runtime->ReloadDocument(m_editor->GetPendingDocument());
      !reload.ok) {
    LOG_ERROR("[Launcher] Runtime reload failed: %s", reload.error.c_str());
  } else {
    if (m_scene)
      m_editor->SyncRuntimeEntityIds(m_scene->registry);
    RefreshCameraFromSceneCamera();
  }
  m_editor->AcknowledgeReload();
}

void LauncherEditorShell::RefreshCameraFromSceneCamera() {
  if (!m_camera || !m_runtime || !m_runtime->GetSceneCamera().has_value())
    return;

  const RuntimeSceneCamera& sceneCamera = *m_runtime->GetSceneCamera();
  m_camera->position = sceneCamera.position;
  m_camera->target = sceneCamera.position + ForwardFromYawPitch(sceneCamera.yaw, sceneCamera.pitch);
  m_camera->fovY = sceneCamera.fovY;
  m_camera->zNear = sceneCamera.nearClip;
  m_camera->zFar = sceneCamera.farClip;
}

bool LauncherEditorShell::OpenProjectFromPicker(std::string* outError) {
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

void LauncherEditorShell::RenderLauncher() {
  // ImGui launcher overlay removed — project is in MCP backend-only mode.
  // Project selection is now handled via MCP commands.
}

void LauncherEditorShell::RenderProjectToolbar() {
  // ImGui toolbar removed — project is in MCP backend-only mode.
}

void LauncherEditorShell::ExecuteManifestCommand(const LauncherProjectCommand& command,
                                                   const std::string& label) {
  std::string resolveError;
  ResolvedLauncherCommand resolved;
  if (!ResolveLauncherCommand(
          command, m_projectRoot, ResolveCommandSdkRoot(), &resolved, &resolveError)) {
    m_launcherError = resolveError;
    return;
  }

  std::string startError;
  if (!m_processRunner.Start(resolved, label, &startError))
    m_launcherError = startError;
}

bool LauncherEditorShell::CreateProjectFromLauncher(std::string* outError) {
  LauncherProjectDocument createdProject;
  const LauncherProjectTemplateRequest request{
      .projectRoot = BufferToString(m_newProjectPathInput),
      .projectName = BufferToString(m_newProjectNameInput),
      .sdkRoot = ResolveCommandSdkRoot(),
  };
  if (!CreateLauncherProjectTemplate(request, &createdProject, outError))
    return false;

  return OpenProject(request.projectRoot, outError);
}

bool LauncherEditorShell::CreateProject(const std::string& name, const fs::path& projectPath,
                                         std::string* outError) {
  const LauncherProjectTemplateRequest request{
      .projectRoot = NormalizeProjectRootInput(projectPath),
      .projectName = name,
      .sdkRoot = ResolveCommandSdkRoot(),
  };
  LauncherProjectDocument createdProject;
  if (!CreateLauncherProjectTemplate(request, &createdProject, outError))
    return false;
  return OpenProject(request.projectRoot, outError);
}

std::string LauncherEditorShell::GetProjectName() const {
  if (!m_projectDocument.manifest.projectName.empty())
    return m_projectDocument.manifest.projectName;
  return m_projectRoot.filename().string();
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
  if (const fs::path exeDir = NormalizePathForLookup(fs::current_path(ec)); !ec && !exeDir.empty()) {
    candidates.push_back(exeDir);
    candidates.push_back(exeDir.parent_path());
    candidates.push_back(exeDir.parent_path().parent_path());
  }

  for (const fs::path& candidate : candidates) {
    if (IsInstalledEnginePrefix(candidate) || IsBuildTreeEnginePrefix(candidate))
      return candidate;
  }

  if (!assetSdkRoot.empty() && assetSdkRoot.filename() == "sdk")
    return assetSdkRoot.parent_path();
  return assetSdkRoot;
}

fs::path LauncherEditorShell::NormalizeProjectRootInput(const fs::path& rawPath) const {
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

fs::path LauncherEditorShell::ResolveAssetPath(const std::string& rawPath) const {
  if (rawPath.empty())
    return {};

  if (fs::path path(rawPath); path.is_absolute())
    return path;
  return ProjectPath::Resolve(rawPath);
}

fs::path LauncherEditorShell::ResolveShaderPath(const char* fileName) const {
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

std::shared_ptr<Shader> LauncherEditorShell::EnsureSceneShader() {
  if (!m_sceneShader) {
    m_sceneShader = std::make_shared<Shader>(
        Shader::FromFiles(ResolveShaderPath("basic.vert").string(), ResolveShaderPath("basic.frag").string()));
  }
  return m_sceneShader;
}

std::shared_ptr<Mesh> LauncherEditorShell::LoadMeshForTag(const std::string& meshTag) {
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
  } catch (const ObjLoader::ObjLoaderException& e) {
    LOG_WARN("[Launcher] Failed to load mesh '%s': %s", meshTag.c_str(), e.what());
    return {};
  }

  m_meshCache[meshTag] = mesh;
  return mesh;
}

std::shared_ptr<Texture> LauncherEditorShell::LoadTexture(const std::string& rawPath) {
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

}  // namespace Monolith::Launcher
