#pragma once
#include <functional>
#include <stdexcept>
#include <string>

struct GLFWwindow;

namespace Horo {
enum class CursorMode {
  Normal,
  Hidden,
  Disabled,
};

// Window-level graphics API preference used during platform/window bootstrap.
// This lives in core so startup does not depend on renderer module headers.
enum class WindowGraphicsApi {
  OpenGL,
  Vulkan,
};

struct WindowGraphicsApiTraits {
  bool createsClientContext = false;
  bool windowOwnsPresentation = false;
  bool windowOwnsVSync = false;
  bool windowOwnsViewportResize = false;
  bool requestsMsaaSamples = false;
};

WindowGraphicsApiTraits
GetWindowGraphicsApiTraits(WindowGraphicsApi graphicsApi);

struct WindowSpec {
  std::string title = "Horo";
  int width = 1280;
  int height = 720;
  bool vsync = true;
  WindowGraphicsApi graphicsApi = WindowGraphicsApi::OpenGL;
  // Runtime window icon image. Relative paths are resolved from the
  // executable directory and the adjacent SDK folder.
  std::string iconFile;
};

class WindowInitException : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

class Window {
public:
  using ResizeCallback = std::function<void(int w, int h)>;
  using CloseCallback = std::function<void()>;
  using FileDropCallback =
      std::function<void(int pathCount, const char **utf8Paths)>;

  explicit Window(const WindowSpec &spec);

  ~Window();

  Window(const Window &) = delete;

  Window &operator=(const Window &) = delete;

  void PollEvents() const;

  void SwapBuffers();

  bool ShouldClose() const;

  void SetVSync(bool enabled);

  int GetWidth() const { return m_width; }
  int GetHeight() const { return m_height; }

  float GetAspect() const;

  bool IsVSyncEnabled() const { return m_vsync; }
  WindowGraphicsApi GetGraphicsApi() const { return m_graphicsApi; }

  WindowGraphicsApiTraits GetGraphicsApiTraits() const;

  bool OwnsPresentation() const;

  bool OwnsVSync() const;

  bool OwnsViewportResize() const;

  GLFWwindow *GetNativeHandle() const { return m_window; }

  void SetResizeCallback(ResizeCallback cb) { m_resizeCb = std::move(cb); }
  void SetCloseCallback(CloseCallback cb) { m_closeCb = std::move(cb); }

  void SetFileDropCallback(FileDropCallback cb) {
    m_fileDropCb = std::move(cb);
  }

  void SetCursorMode(CursorMode mode);

  void SetupNativeMenuBar() const;
  using MenuCallback = std::function<void()>;

  /** @brief File menu callback group. */
  struct FileMenuCallbacks {
    MenuCallback fileNew;          /**< File -> New Scene. */
    MenuCallback fileOpen;         /**< File -> Open Scene. */
    MenuCallback fileResetLayout;  /**< File -> Reset Layout. */
    MenuCallback fileSettings;     /**< File -> Settings. */
    MenuCallback fileCloseEditor;  /**< File -> Close Editor. */
  };

  /** @brief Add menu callback group. */
  struct AddMenuCallbacks {
    MenuCallback addPanel;          /**< Add -> Panel. */
    MenuCallback addProp;           /**< Add -> Prop. */
    MenuCallback addLight;          /**< Add -> Light. */
    MenuCallback addCamera;         /**< Add -> Camera. */
    MenuCallback addPropFromAsset;  /**< Add -> Prop from Selected Asset. */
  };

  /** @brief Edit menu callback group. */
  struct EditMenuCallbacks {
    MenuCallback editUndo;          /**< Edit -> Undo. */
    MenuCallback editRedo;          /**< Edit -> Redo. */
    MenuCallback editRename;        /**< Edit -> Rename. */
    MenuCallback editCreatePrefab;  /**< Edit -> Create Prefab. */
    MenuCallback editDuplicate;     /**< Edit -> Duplicate. */
    MenuCallback editDelete;        /**< Edit -> Delete. */
  };

  /** @brief View menu callback group. */
  struct ViewMenuCallbacks {
    MenuCallback viewFlyMode;         /**< View -> Viewport Nav. */
    MenuCallback viewHelp;            /**< View -> Help. */
    MenuCallback viewQuickOpen;       /**< View -> Quick Open. */
    MenuCallback viewCommandPalette;  /**< View -> Command Palette. */
    MenuCallback viewResetLayout;     /**< View -> Reset Layout. */
  };

  /** @brief Build menu callback group. */
  struct BuildMenuCallbacks {
    MenuCallback buildRelease;  /**< Build -> Build & Release. */
  };

  /** @brief Groups all native menu bar callbacks into a single parameter object. */
  struct MenuCallbacks {
    FileMenuCallbacks file;  /**< File menu callbacks. */
    AddMenuCallbacks add;    /**< Add menu callbacks. */
    EditMenuCallbacks edit;  /**< Edit menu callbacks. */
    ViewMenuCallbacks view;  /**< View menu callbacks. */
    BuildMenuCallbacks build; /**< Build menu callbacks. */
  };
  void SetMenuCallbacks(const MenuCallbacks& callbacks) const;

private:
  GLFWwindow *m_window = nullptr;
  int m_width = 0;
  int m_height = 0;
  bool m_vsync = true;
  WindowGraphicsApi m_graphicsApi = WindowGraphicsApi::OpenGL;
  ResizeCallback m_resizeCb;
  CloseCallback m_closeCb;
  FileDropCallback m_fileDropCb;

  static void FramebufferSizeCallback(GLFWwindow *win, int w, int h);

  static void WindowCloseCallback(GLFWwindow *win);

  static void DropPathsThunk(GLFWwindow *win, int count, const char **paths);
};
} // namespace Horo
