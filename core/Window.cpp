#include "core/Window.h"

// clang-format off
#include <glad/glad.h>   // must precede GLFW
#include <GLFW/glfw3.h>
// clang-format on

#include <cstdlib>
#include <stdexcept>

#include "core/Logger.h"

namespace {

int ReadEnvNonNegativeInt(const char* name, int fallback) {
  const char* value = std::getenv(name);
  if (!value || !*value)
    return fallback;
  char* end = nullptr;
  const long parsed = std::strtol(value, &end, 10);
  const bool valid = (end != value && *end == '\0');
  if (!valid || parsed < 0 || parsed > 32)
    return fallback;
  return static_cast<int>(parsed);
}

}  // namespace

namespace Monolith {

WindowGraphicsApiTraits GetWindowGraphicsApiTraits(WindowGraphicsApi graphicsApi) {
  switch (graphicsApi) {
    case WindowGraphicsApi::OpenGL:
      return {.createsClientContext = true,
              .windowOwnsPresentation = true,
              .windowOwnsVSync = true,
              .windowOwnsViewportResize = true,
              .requestsMsaaSamples = true};
    case WindowGraphicsApi::Vulkan:
      return {.createsClientContext = false,
              .windowOwnsPresentation = false,
              .windowOwnsVSync = false,
              .windowOwnsViewportResize = false,
              .requestsMsaaSamples = false};
  }

  return {};
}

Window::Window(const WindowSpec& spec) : m_width(spec.width), m_height(spec.height) {
  if (!glfwInit())
    throw std::runtime_error("glfwInit failed");

  m_graphicsApi = spec.graphicsApi;
  m_vsync = spec.vsync;
  const WindowGraphicsApiTraits traits = GetGraphicsApiTraits();

  if (traits.createsClientContext) {
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
  if (traits.requestsMsaaSamples) {
    // Default 4x MSAA; set MONOLITH_GLFW_SAMPLES=0 in headless/CI (llvmpipe) to avoid driver crashes.
    const int samples = ReadEnvNonNegativeInt("MONOLITH_GLFW_SAMPLES", 4);
    glfwWindowHint(GLFW_SAMPLES, samples);
  }

  m_window = glfwCreateWindow(spec.width, spec.height, spec.title.c_str(), nullptr, nullptr);
  if (!m_window) {
    glfwTerminate();
    throw std::runtime_error("glfwCreateWindow failed");
  }

  if (traits.createsClientContext) {
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
  if (OwnsPresentation())
    glfwSwapBuffers(m_window);
}

bool Window::ShouldClose() const {
  return glfwWindowShouldClose(m_window);
}

void Window::SetVSync(bool enabled) {
  m_vsync = enabled;
  if (OwnsVSync())
    glfwSwapInterval(enabled ? 1 : 0);
}

float Window::GetAspect() const {
  return m_height > 0 ? static_cast<float>(m_width) / m_height : 1.0f;
}

WindowGraphicsApiTraits Window::GetGraphicsApiTraits() const {
  return Monolith::GetWindowGraphicsApiTraits(m_graphicsApi);
}

bool Window::OwnsPresentation() const {
  return GetGraphicsApiTraits().windowOwnsPresentation;
}

bool Window::OwnsVSync() const {
  return GetGraphicsApiTraits().windowOwnsVSync;
}

bool Window::OwnsViewportResize() const {
  return GetGraphicsApiTraits().windowOwnsViewportResize;
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
  if (self->OwnsViewportResize())
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
