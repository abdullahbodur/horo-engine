#pragma once
#include <functional>
#include <string>

struct GLFWwindow;

namespace Monolith {

enum class CursorMode
{
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

struct WindowSpec {
  std::string title = "Monolith";
  int width = 1280;
  int height = 720;
  bool vsync = true;
  WindowGraphicsApi graphicsApi = WindowGraphicsApi::OpenGL;
};

class Window {
 public:
  using ResizeCallback = std::function<void(int w, int h)>;
  using CloseCallback = std::function<void()>;
  using FileDropCallback = std::function<void(int pathCount, const char** utf8Paths)>;

  explicit Window(const WindowSpec& spec);
  ~Window();

  Window(const Window&) = delete;
  Window& operator=(const Window&) = delete;

  void PollEvents();
  void SwapBuffers();
  bool ShouldClose() const;
  void SetVSync(bool enabled);

  int GetWidth() const { return m_width; }
  int GetHeight() const { return m_height; }
  float GetAspect() const;
  bool IsVSyncEnabled() const { return m_vsync; }
  WindowGraphicsApi GetGraphicsApi() const { return m_graphicsApi; }

  GLFWwindow* GetNativeHandle() const { return m_window; }

  void SetResizeCallback(ResizeCallback cb) { m_resizeCb = std::move(cb); }
  void SetCloseCallback(CloseCallback cb) { m_closeCb = std::move(cb); }
  void SetFileDropCallback(FileDropCallback cb) { m_fileDropCb = std::move(cb); }

  void SetCursorMode(CursorMode mode);

 private:
  GLFWwindow* m_window = nullptr;
  int m_width = 0;
  int m_height = 0;
  bool m_vsync = true;
  WindowGraphicsApi m_graphicsApi = WindowGraphicsApi::OpenGL;
  ResizeCallback m_resizeCb;
  CloseCallback m_closeCb;
  FileDropCallback m_fileDropCb;

  static void FramebufferSizeCallback(GLFWwindow* win, int w, int h);
  static void WindowCloseCallback(GLFWwindow* win);
  static void DropPathsThunk(GLFWwindow* win, int count, const char** paths);
};

}  // namespace Monolith
