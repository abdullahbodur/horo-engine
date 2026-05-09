/** @file LauncherEditorShell.h
 *  @brief Orchestrates the launcher UI and manages the active project lifecycle. */
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

/** @brief Transparent heterogeneous hash functor for string-keyed maps. */
struct StringHash {
  using is_transparent = void;

  /** @brief Hashes a string_view.
   *  @param sv The string view to hash.
   *  @return The computed hash value. */
  size_t operator()(std::string_view sv) const noexcept {
    return std::hash<std::string_view>{}(sv);
  }

  /** @brief Hashes a std::string.
   *  @param s The string to hash.
   *  @return The computed hash value. */
  size_t operator()(const std::string &s) const noexcept {
    return std::hash<std::string>{}(s);
  }
};

/** @brief Describes the screen-space hit region used for project import drag-and-drop. */
struct ImportProjectDropTarget {
  bool valid = false;   /**< Whether the drop target is currently active. */
  float minX = 0.0f;   /**< Left edge of the drop region in screen coordinates. */
  float minY = 0.0f;   /**< Top edge of the drop region in screen coordinates. */
  float maxX = 0.0f;   /**< Right edge of the drop region in screen coordinates. */
  float maxY = 0.0f;   /**< Bottom edge of the drop region in screen coordinates. */
};

/** @brief Top-level controller that owns launcher UI state, project loading, and scene rendering. */
class LauncherEditorShell {
public:
  /** @brief Binds the shell to the engine subsystems it delegates to.
   *  @param editor  The editor layer that manages the scene document.
   *  @param scene   The active scene instance.
   *  @param runtime The scene reference runtime used for entity evaluation.
   *  @param camera  The camera used to view the loaded scene. */
  void Attach(Editor::EditorLayer *editor, Scene *scene,
              SceneReferenceRuntime *runtime, Camera *camera);

  /** @brief Performs one-time initialization after Attach. */
  void Initialize();

  /** @brief Tears down all active state before the application exits. */
  void Shutdown();

  /** @brief Opens a project from the given path, populating outError on failure.
   *  @param projectPath Absolute path to the project root directory.
   *  @param outError    Receives a human-readable error message on failure.
   *  @return True if the project was opened successfully. */
  bool OpenProject(const std::filesystem::path &projectPath,
                   std::string *outError);

  /** @brief Unloads the currently open project and returns to the launcher home screen. */
  void CloseProject();

  /** @brief Drives per-frame logic for pending reloads, process polling, and state updates. */
  void Update();

  /** @brief Renders the launcher or in-editor overlay on top of the current frame. */
  void RenderOverlay();

  /** @brief Handles file-system paths dropped onto the launcher window.
   *  @param pathCount  Number of paths in the drop event.
   *  @param utf8Paths  Null-terminated UTF-8 path strings.
   *  @param dropX      Horizontal drop position in screen coordinates.
   *  @param dropY      Vertical drop position in screen coordinates. */
  void OnPathsDropped(int pathCount, const char **utf8Paths, float dropX,
                      float dropY);

  /** @brief Returns true when a project is currently open.
   *  @return True if a project root is set. */
  bool HasActiveProject() const { return !m_projectRoot.empty(); }

  /** @brief Stores a launcher-level error string for display in the UI.
   *  @param error The error message to surface. */
  void SetLauncherError(std::string error) {
    m_launcherError = std::move(error);
  }

private:
  Editor::EditorLayer *m_editor = nullptr;         /**< Bound editor layer, not owned. */
  Scene *m_scene = nullptr;                        /**< Active scene, not owned. */
  SceneReferenceRuntime *m_runtime = nullptr;      /**< Scene runtime, not owned. */
  Camera *m_camera = nullptr;                      /**< Scene camera, not owned. */

  EditorHomeDocument m_homeDocument;               /**< Persisted home screen state (recent projects). */
  LauncherProjectDocument m_projectDocument;       /**< Manifest and metadata for the open project. */
  std::filesystem::path m_projectRoot;             /**< Root directory of the currently open project. */
  ExternalProcessRunner m_processRunner;           /**< Manages the build/run child process. */

  std::shared_ptr<Shader> m_sceneShader;           /**< Shader used to render the scene preview. */
  std::unordered_map<std::string, std::shared_ptr<Mesh>, StringHash,
                     std::equal_to<>>
      m_meshCache;                                 /**< Cache of loaded meshes keyed by tag. */
  std::unordered_map<std::string, std::shared_ptr<Texture>, StringHash,
                     std::equal_to<>>
      m_textureCache;                              /**< Cache of loaded textures keyed by path. */
  std::shared_ptr<Texture> m_launcherLogoTexture; /**< Cached launcher logo texture. */
  std::shared_ptr<Texture> m_discordIconTexture;  /**< Cached Discord icon texture. */

  std::string m_launcherError;                     /**< Last error message shown in the launcher UI. */
  std::array<char, 256> m_newProjectNameInput{};   /**< Input buffer for the new project name field. */
  std::array<char, 512> m_newProjectPathInput{};   /**< Input buffer for the new project path field. */
  int m_newProjectRendererBackendIndex = 0;        /**< Selected renderer backend index in the creation form. */
  bool m_newProjectAdvancedSettingsOpen = false;   /**< Whether the advanced settings section is expanded. */
  ImportProjectDropTarget m_importProjectDropTarget; /**< Active drag-and-drop import region. */

  /** @brief Registers runtime event callbacks for scene lifecycle notifications. */
  void ConfigureRuntimeCallbacks();

  /** @brief Applies a queued scene reload if one has been requested since the last frame. */
  void HandlePendingSceneReload();

  /** @brief Synchronizes the render camera to match the scene's configured camera entity. */
  void RefreshCameraFromSceneCamera();

  /** @brief Releases all resources and editor state associated with the current project. */
  void UnloadCurrentProjectState();

  /** @brief Applies project-specific editor configuration after a project has been loaded.
   *  @param projectRoot   Root directory of the project.
   *  @param sceneDocument The scene document loaded for the project. */
  void SetupEditorForProject(const std::filesystem::path &projectRoot,
                             const Editor::SceneDocument &sceneDocument);

  /** @brief Presents a native folder picker and attempts to open the selected project.
   *  @param outError Receives a human-readable error message on failure.
   *  @return True if the project was opened successfully. */
  bool OpenProjectFromPicker(std::string *outError);

  /** @brief Renders the full launcher home screen when no project is open. */
  void RenderLauncher();

  /** @brief Renders the left navigation sidebar of the launcher.
   *  @param sidebarWidth Width allocated for the sidebar in pixels.
   *  @param fullHeight   Total available height in pixels. */
  void RenderLauncherSidebar(float sidebarWidth, float fullHeight);

  /** @brief Renders the main content area of the launcher to the right of the sidebar.
   *  @param mainWidth  Width allocated for the content area in pixels.
   *  @param fullHeight Total available height in pixels. */
  void RenderLauncherMainContent(float mainWidth, float fullHeight);

  /** @brief Renders the hero banner image and headline at the top of the content area.
   *  @param contentWidth Available width for the hero section in pixels. */
  void RenderLauncherHero(float contentWidth);

  /** @brief Renders the new project creation panel.
   *  @param contentWidth Available width for the panel in pixels. */
  void RenderNewProjectPanel(float contentWidth);

  /** @brief Renders the name and path input row for the new project form.
   *  @param innerWidth  Usable inner width of the form in pixels.
   *  @param rowStartX   Left edge of the row in screen coordinates.
   *  @param rowStartY   Top edge of the row in screen coordinates. */
  void RenderNewProjectFormRow(float innerWidth, float rowStartX,
                               float rowStartY);

  /** @brief Renders the action buttons and advanced settings toggle for the new project form.
   *  @param innerWidth    Usable inner width of the actions area in pixels.
   *  @param rowStartX     Left edge of the action row in screen coordinates.
   *  @param advancedRowY  Vertical position where the advanced settings section begins. */
  void RenderNewProjectActions(float innerWidth, float rowStartX,
                               float advancedRowY);

  /** @brief Renders the scrollable list of recent projects.
   *  @param contentWidth Available width for the list panel in pixels.
   *  @param panelHeight  Available height for the list panel in pixels. */
  void RenderRecentProjectsList(float contentWidth, float panelHeight);

  /** @brief Renders a single recent project card entry.
   *  @param recentPath Absolute path to the project root.
   *  @param cardIndex  Zero-based index used for stable ImGui IDs. */
  void RenderRecentProjectCard(const std::string &recentPath, int cardIndex);

  /** @brief Returns the launcher logo texture, loading it on first call.
   *  @return Shared pointer to the launcher logo texture. */
  std::shared_ptr<Texture> EnsureLauncherLogoTexture();

  /** @brief Returns the Discord icon texture, loading it on first call.
   *  @return Shared pointer to the Discord icon texture. */
  std::shared_ptr<Texture> EnsureDiscordIconTexture();

  /** @brief Creates a new project from the values entered in the launcher form.
   *  @param outError Receives a human-readable error message on failure.
   *  @return True if the project was created and opened successfully. */
  bool CreateProjectFromLauncher(std::string *outError);

  /** @brief Determines the SDK root directory relative to the running executable.
   *  @return Resolved SDK root path. */
  std::filesystem::path ResolveCommandSdkRoot() const;

  /** @brief Normalizes and expands a raw project root path entered by the user.
   *  @param rawPath The user-supplied path, potentially relative or unexpanded.
   *  @return Canonical absolute path for the project root. */
  std::filesystem::path
  NormalizeProjectRootInput(const std::filesystem::path &rawPath) const;

  /** @brief Resolves a raw asset path string to an absolute path within the project.
   *  @param rawPath The asset path as stored in the scene or manifest.
   *  @return Absolute path to the asset. */
  std::filesystem::path ResolveAssetPath(const std::string &rawPath) const;

  /** @brief Resolves the absolute path to a named shader source file.
   *  @param fileName The shader file name relative to the shaders directory.
   *  @return Absolute path to the shader file. */
  std::filesystem::path ResolveShaderPath(const char *fileName) const;

  /** @brief Returns the scene shader, compiling it on first call.
   *  @return Shared pointer to the compiled scene shader. */
  std::shared_ptr<Shader> EnsureSceneShader();

  /** @brief Loads a mesh identified by the given tag, returning a cached result when available.
   *  @param meshTag Scene-defined tag identifying the mesh to load.
   *  @return Shared pointer to the loaded mesh, or nullptr on failure. */
  std::shared_ptr<Mesh> LoadMeshForTag(const std::string &meshTag);

  /** @brief Loads a texture from a raw path, returning a cached result when available.
   *  @param rawPath The texture path as stored in the scene or manifest.
   *  @return Shared pointer to the loaded texture, or nullptr on failure. */
  std::shared_ptr<Texture> LoadTexture(const std::string &rawPath);
};
} // namespace Horo::Launcher
