#include "core/Window.h"

// clang-format off
#include <glad/glad.h>   // must precede GLFW
#include <GLFW/glfw3.h>
// clang-format on

#include <stdexcept>

#include "core/Logger.h"

namespace Monolith {

Window::Window(const WindowSpec& spec) : m_width(spec.width), m_height(spec.height) {
  if (!glfwInit())
    throw std::runtime_error("glfwInit failed");

  m_graphicsApi = spec.graphicsApi;
  m_vsync = spec.vsync;

  if (m_graphicsApi == WindowGraphicsApi::OpenGL) {
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif
  } else {
    // Vulkan path: no client graphics context should be created by GLFW.
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  }

  glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
  if (m_graphicsApi == WindowGraphicsApi::OpenGL)
    glfwWindowHint(GLFW_SAMPLES, 4);  // 4x MSAA

  m_window = glfwCreateWindow(spec.width, spec.height, spec.title.c_str(), nullptr, nullptr);
  if (!m_window) {
    glfwTerminate();
    throw std::runtime_error("glfwCreateWindow failed");
  }

  if (m_graphicsApi == WindowGraphicsApi::OpenGL) {
    glfwMakeContextCurrent(m_window);
    if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress))) {
      glfwDestroyWindow(m_window);
      glfwTerminate();
      throw std::runtime_error("gladLoadGLLoader failed");
    }
    SetVSync(spec.vsync);
    LOG_INFO("OpenGL %s — %s", glGetString(GL_VERSION), glGetString(GL_RENDERER));
  } else {
    // Vulkan presentation pacing is backend-owned; the window stores the preference.
    LOG_INFO("Window initialized for Vulkan backend (GLFW no-client-api surface).");
  }

  // Store this pointer for callbacks
  glfwSetWindowUserPointer(m_window, this);
  glfwSetFramebufferSizeCallback(m_window, FramebufferSizeCallback);
  glfwSetWindowCloseCallback(m_window, WindowCloseCallback);
  glfwSetDropCallback(m_window, DropPathsThunk);
}

Window::~Window() {
  if (m_window)
    glfwDestroyWindow(m_window);
  glfwTerminate();
}

void Window::PollEvents() {
  glfwPollEvents();
}

void Window::SwapBuffers() {
  if (m_graphicsApi == WindowGraphicsApi::OpenGL)
    glfwSwapBuffers(m_window);
}

bool Window::ShouldClose() const {
  return glfwWindowShouldClose(m_window);
}

void Window::SetVSync(bool enabled) {
  m_vsync = enabled;
  if (m_graphicsApi == WindowGraphicsApi::OpenGL)
    glfwSwapInterval(enabled ? 1 : 0);
}

float Window::GetAspect() const {
  return m_height > 0 ? static_cast<float>(m_width) / m_height : 1.0f;
}

void Window::SetCursorMode(CursorMode mode)
{
  int glfwMode;
  switch (mode)
  {
    case CursorMode::Hidden:   glfwMode = GLFW_CURSOR_HIDDEN;   break;
    case CursorMode::Disabled: glfwMode = GLFW_CURSOR_DISABLED; break;
    default:                   glfwMode = GLFW_CURSOR_NORMAL;   break;
  }
  glfwSetInputMode(m_window, GLFW_CURSOR, glfwMode);
}

void Window::FramebufferSizeCallback(GLFWwindow* win, int w, int h) {
  auto* self = static_cast<Window*>(glfwGetWindowUserPointer(win));
  self->m_width = w;
  self->m_height = h;
  if (self->m_graphicsApi == WindowGraphicsApi::OpenGL)
    glViewport(0, 0, w, h);
  if (self->m_resizeCb)
    self->m_resizeCb(w, h);
}

void Window::WindowCloseCallback(GLFWwindow* win) {
  auto* self = static_cast<Window*>(glfwGetWindowUserPointer(win));
  if (self->m_closeCb)
    self->m_closeCb();
}

void Window::DropPathsThunk(GLFWwindow* win, int count, const char** paths) {
  auto* self = static_cast<Window*>(glfwGetWindowUserPointer(win));
  if (self->m_fileDropCb)
    self->m_fileDropCb(count, paths);
}

}  // namespace Monolith
