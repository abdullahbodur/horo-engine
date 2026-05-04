#pragma once

#include <array>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

#include "ui/editor/EditorLayer.h"
#include "ui/launcher/EditorHomeSettings.h"
#include "ui/launcher/ExternalProcessRunner.h"
#include "ui/launcher/LauncherProject.h"
#include "renderer/Camera.h"
#include "renderer/Material.h"
#include "renderer/Mesh.h"
#include "renderer/Shader.h"
#include "renderer/Texture.h"
#include "scene/Scene.h"
#include "scene/SceneReferenceRuntime.h"

namespace Horo::Launcher {
struct StringHash {
  using is_transparent = void;

  size_t operator()(std::string_view sv) const noexcept {
    return std::hash<std::string_view>{}(sv);
  }

  size_t operator()(const std::string &s) const noexcept {
    return std::hash<std::string>{}(s);
  }
};

struct ImportProjectDropTarget {
  bool valid = false;
  float minX = 0.0f;
  float minY = 0.0f;
  float maxX = 0.0f;
  float maxY = 0.0f;
};

class LauncherEditorShell {
public:
  void Attach(Editor::EditorLayer *editor, Scene *scene,
              SceneReferenceRuntime *runtime, Camera *camera);

  void Initialize();

  void Shutdown();

  bool OpenProject(const std::filesystem::path &projectPath,
                   std::string *outError);

  void CloseProject();

  void Update();

  void RenderOverlay();

  void OnPathsDropped(int pathCount, const char **utf8Paths, float dropX,
                      float dropY);

  bool HasActiveProject() const { return !m_projectRoot.empty(); }

  void SetLauncherError(std::string error) {
    m_launcherError = std::move(error);
  }

private:
  Editor::EditorLayer *m_editor = nullptr;
  Scene *m_scene = nullptr;
  SceneReferenceRuntime *m_runtime = nullptr;
  Camera *m_camera = nullptr;

  EditorHomeDocument m_homeDocument;
  LauncherProjectDocument m_projectDocument;
  std::filesystem::path m_projectRoot;
  ExternalProcessRunner m_processRunner;

  std::shared_ptr<Shader> m_sceneShader;
  std::unordered_map<std::string, std::shared_ptr<Mesh>, StringHash,
                     std::equal_to<>>
      m_meshCache;
  std::unordered_map<std::string, std::shared_ptr<Texture>, StringHash,
                     std::equal_to<>>
      m_textureCache;
  std::shared_ptr<Texture> m_launcherLogoTexture;
  std::shared_ptr<Texture> m_discordIconTexture;

  std::string m_launcherError;
  std::array<char, 256> m_newProjectNameInput{};
  std::array<char, 512> m_newProjectPathInput{};
  int m_newProjectRendererBackendIndex = 0;
  bool m_newProjectAdvancedSettingsOpen = false;
  ImportProjectDropTarget m_importProjectDropTarget;

  void ConfigureRuntimeCallbacks();

  void HandlePendingSceneReload();

  void RefreshCameraFromSceneCamera();

  void UnloadCurrentProjectState();

  void SetupEditorForProject(const std::filesystem::path &projectRoot,
                             const Editor::SceneDocument &sceneDocument);

  bool OpenProjectFromPicker(std::string *outError);

  void RenderLauncher();

  void RenderLauncherSidebar(float sidebarWidth, float fullHeight);

  void RenderLauncherMainContent(float mainWidth, float fullHeight);

  void RenderLauncherHero(float contentWidth);

  void RenderNewProjectPanel(float contentWidth);

  void RenderNewProjectFormRow(float innerWidth, float rowStartX,
                               float rowStartY);

  void RenderNewProjectActions(float innerWidth, float rowStartX,
                               float advancedRowY);

  void RenderRecentProjectsList(float contentWidth, float panelHeight);

  void RenderRecentProjectCard(const std::string &recentPath, int cardIndex);

  std::shared_ptr<Texture> EnsureLauncherLogoTexture();

  std::shared_ptr<Texture> EnsureDiscordIconTexture();

  bool CreateProjectFromLauncher(std::string *outError);

  std::filesystem::path ResolveCommandSdkRoot() const;

  std::filesystem::path
  NormalizeProjectRootInput(const std::filesystem::path &rawPath) const;

  std::filesystem::path ResolveAssetPath(const std::string &rawPath) const;

  std::filesystem::path ResolveShaderPath(const char *fileName) const;

  std::shared_ptr<Shader> EnsureSceneShader();

  std::shared_ptr<Mesh> LoadMeshForTag(const std::string &meshTag);

  std::shared_ptr<Texture> LoadTexture(const std::string &rawPath);
};
} // namespace Horo::Launcher
