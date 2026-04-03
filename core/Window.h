#pragma once
#include <functional>
#include <string>

struct GLFWwindow;

namespace Horo {

struct WindowSpec {
  std::string title = "Horo";
  int width = 1280;
  int height = 720;
  bool vsync = true;
};

class Window {
 public:
  using ResizeCallback = std::function<void(int w, int h)>;
  using CloseCallback = std::function<void()>;

  explicit Window(const WindowSpec& spec);
  ~Window();

  Window(const Window&) = delete;
  Window& operator=(const Window&) = delete;

  void PollEvents();
  void SwapBuffers();
  bool ShouldClose() const;

  int GetWidth() const { return m_width; }
  int GetHeight() const { return m_height; }
  float GetAspect() const;

  GLFWwindow* GetNativeHandle() const { return m_window; }

  void SetResizeCallback(ResizeCallback cb) { m_resizeCb = cb; }
  void SetCloseCallback(CloseCallback cb) { m_closeCb = cb; }

 private:
  GLFWwindow* m_window = nullptr;
  int m_width = 0;
  int m_height = 0;
  ResizeCallback m_resizeCb;
  CloseCallback m_closeCb;

  static void FramebufferSizeCallback(GLFWwindow* win, int w, int h);
  static void WindowCloseCallback(GLFWwindow* win);
};

}  // namespace Horo
