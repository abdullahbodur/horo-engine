#pragma once

#include <array>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>

#include "editor/EditorLayer.h"
#include "renderer/Camera.h"
#include "renderer/Material.h"
#include "renderer/Mesh.h"
#include "renderer/Shader.h"
#include "renderer/Texture.h"
#include "scene/Scene.h"
#include "scene/SceneReferenceRuntime.h"
#include "standalone/EditorHomeSettings.h"
#include "standalone/ExternalProcessRunner.h"
#include "standalone/StandaloneProject.h"

namespace Monolith::Standalone {

class StandaloneEditorShell {
 public:
  void Attach(Editor::EditorLayer* editor,
              Scene* scene,
              SceneReferenceRuntime* runtime,
              Camera* camera);
  void Initialize();
  void Shutdown();

  bool OpenProject(const std::filesystem::path& projectPath, std::string* outError);
  void CloseProject();
  void Update();
  void RenderOverlay();

  bool HasActiveProject() const { return !m_projectRoot.empty(); }
  void SetLauncherError(std::string error) { m_launcherError = std::move(error); }

 private:
  enum class LauncherPanel {
    OpenExistingProject,
    CreateNewProject,
  };

  Editor::EditorLayer* m_editor = nullptr;
  Scene* m_scene = nullptr;
  SceneReferenceRuntime* m_runtime = nullptr;
  Camera* m_camera = nullptr;

  EditorHomeDocument m_homeDocument;
  StandaloneProjectDocument m_projectDocument;
  std::filesystem::path m_projectRoot;
  ExternalProcessRunner m_processRunner;

  std::shared_ptr<Shader> m_sceneShader;
  std::unordered_map<std::string, std::shared_ptr<Mesh>> m_meshCache;
  std::unordered_map<std::string, std::shared_ptr<Texture>> m_textureCache;

  std::string m_launcherError;
  std::array<char, 512> m_openProjectInput{};
  std::array<char, 256> m_newProjectNameInput{};
  std::array<char, 512> m_newProjectPathInput{};
  LauncherPanel m_launcherPanel = LauncherPanel::OpenExistingProject;

  void ConfigureRuntimeCallbacks();
  void HandlePendingSceneReload();
  void RefreshCameraFromSceneCamera();
  void RenderLauncher();
  void RenderProjectToolbar();
  void ExecuteManifestCommand(const StandaloneProjectCommand& command, const std::string& label);
  bool CreateProjectFromLauncher(std::string* outError);
  std::filesystem::path NormalizeProjectRootInput(const std::filesystem::path& rawPath) const;
  std::filesystem::path ResolveAssetPath(const std::string& rawPath) const;
  std::filesystem::path ResolveShaderPath(const char* fileName) const;
  std::shared_ptr<Shader> EnsureSceneShader();
  std::shared_ptr<Mesh> LoadMeshForTag(const std::string& meshTag);
  std::shared_ptr<Texture> LoadTexture(const std::string& rawPath);
};

}  // namespace Monolith::Standalone
