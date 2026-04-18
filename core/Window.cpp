#include "core/Window.h"

// clang-format off
#include <glad/glad.h>   // must precede GLFW
#include <GLFW/glfw3.h>
// clang-format on

#include <cstdlib>
#include <memory>
#include <stdexcept>

#include "core/Logger.h"

namespace {

#ifdef _WIN32
std::string ReadWin32EnvString(const char* name) {
  char* rawValue = nullptr;
  if (size_t len = 0; _dupenv_s(&rawValue, &len, name) != 0 || !rawValue)
    return {};
  std::unique_ptr<char, decltype(&std::free)> value(rawValue, &std::free);
  return std::string(value.get());
}
#endif

int ReadEnvNonNegativeInt(const char* name, int fallback) {
  if (!name || !*name)
    return fallback;
#ifdef _WIN32
  const std::string value = ReadWin32EnvString(name);
  if (value.empty())
    return fallback;
  char* end = nullptr;
  const long parsed = std::strtol(value.c_str(), &end, 10);
  if (const bool valid = (end != value.c_str() && *end == '\0'); !valid || parsed < 0 || parsed > 32)
    return fallback;
#else
  const char* value = std::getenv(name);
  if (!value || !*value)
    return fallback;
  char* end = nullptr;
  const long parsed = std::strtol(value, &end, 10);
  if (const bool valid = (end != value && *end == '\0'); !valid || parsed < 0 || parsed > 32)
    return fallback;
#endif
  return static_cast<int>(parsed);
}

bool ReadEnvBool(const char* name, bool fallback) {
  if (!name || !*name)
    return fallback;
#ifdef _WIN32
  const std::string parsed = ReadWin32EnvString(name);
  if (parsed.empty())
    return fallback;
#else
  const char* value = std::getenv(name);
  if (!value || !*value)
    return fallback;
  const std::string parsed(value);
#endif
  return parsed != "0";
}

const char* SafeGlfwErrorString() {
  const char* description = nullptr;
  if (const int code = glfwGetError(&description); code == GLFW_NO_ERROR)
    return "no-error";
  return description ? description : "unknown-glfw-error";
}

}  // namespace

namespace {

class GlfwContext {
public:
  static void Init() {
    if (s_refCount++ == 0 && !glfwInit())
      throw std::runtime_error("glfwInit failed");
  }

  static void Shutdown() {
    if (--s_refCount == 0) {
      glfwTerminate();
    }
  }

private:
  static inline int s_refCount = 0;
};

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

Window::Window(const WindowSpec& spec)
    : m_width(spec.width), m_height(spec.height), m_vsync(spec.vsync), m_graphicsApi(spec.graphicsApi) {
  LOG_INFO("Window bootstrap begin: title='%s' size=%dx%d api=%d vsync=%d",
           spec.title.c_str(),
           spec.width,
           spec.height,
           static_cast<int>(spec.graphicsApi),
           spec.vsync ? 1 : 0);
  LOG_INFO("Calling glfwInit...");
  GlfwContext::Init();
  LOG_INFO("glfwInit succeeded.");

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
  const bool visible = ReadEnvBool("MONOLITH_GLFW_VISIBLE", true);
  glfwWindowHint(GLFW_VISIBLE, visible ? GLFW_TRUE : GLFW_FALSE);
  LOG_INFO("GLFW window hint: visible=%d", visible ? 1 : 0);
  if (traits.requestsMsaaSamples) {
    // Default 4x MSAA; set MONOLITH_GLFW_SAMPLES=0 in headless/CI (llvmpipe) to avoid driver crashes.
    const int samples = ReadEnvNonNegativeInt("MONOLITH_GLFW_SAMPLES", 4);
    glfwWindowHint(GLFW_SAMPLES, samples);
    LOG_INFO("GLFW window hint: samples=%d", samples);
  }

  LOG_INFO("Calling glfwCreateWindow...");
  m_window = glfwCreateWindow(spec.width, spec.height, spec.title.c_str(), nullptr, nullptr);
  if (!m_window) {
    GlfwContext::Shutdown();
    throw WindowInitException(std::string("glfwCreateWindow failed: ") + SafeGlfwErrorString());
  }
  LOG_INFO("glfwCreateWindow succeeded: window=%p", m_window);

  if (traits.createsClientContext) {
    LOG_INFO("Making GL context current...");
    glfwMakeContextCurrent(m_window);
    LOG_INFO("Loading GL via glad...");
    if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress))) {
      glfwDestroyWindow(m_window);
      GlfwContext::Shutdown();
      throw WindowInitException("gladLoadGLLoader failed");
    }
    LOG_INFO("gladLoadGLLoader succeeded.");
    SetVSync(spec.vsync);
    LOG_INFO("SetVSync complete: enabled=%d", spec.vsync ? 1 : 0);
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
  if (m_window) {
    glfwSetWindowUserPointer(m_window, nullptr);
    glfwSetFramebufferSizeCallback(m_window, nullptr);
    glfwSetWindowCloseCallback(m_window, nullptr);
    glfwSetDropCallback(m_window, nullptr);

    if (glfwGetCurrentContext() == m_window) {
      glfwMakeContextCurrent(nullptr);
    }

    glfwDestroyWindow(m_window);
    m_window = nullptr;
  }
  GlfwContext::Shutdown();
}

void Window::PollEvents() const {
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
  return m_height > 0 ? static_cast<float>(m_width) / static_cast<float>(m_height) : 1.0f;
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
  const auto* self = static_cast<const Window*>(glfwGetWindowUserPointer(win));
  if (self->m_closeCb)
    self->m_closeCb();
}

void Window::DropPathsThunk(GLFWwindow* win, int count, const char** paths) {
  const auto* self = static_cast<const Window*>(glfwGetWindowUserPointer(win));
  if (self->m_fileDropCb)
    self->m_fileDropCb(count, paths);
}

}  // namespace Monolith
