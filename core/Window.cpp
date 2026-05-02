#include "core/Window.h"

// clang-format off
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#ifdef _WIN32
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#include <dwmapi.h>
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#endif
// clang-format on

#include <array>
#include <bit>
#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <vector>

#include "core/Logger.h"
#include "renderer/Renderer.h"
#include "renderer/opengl/OpenGLContext.h"
#include <stb_image.h>

namespace {
#ifdef _WIN32
std::string ReadWin32EnvString(const char *name) {
  size_t len = 0;
  if (getenv_s(&len, nullptr, 0, name) != 0 || len <= 1)
    return {};
  std::vector<char> value(len);
  if (getenv_s(&len, value.data(), value.size(), name) != 0 || len <= 1)
    return {};
  return std::string(value.data());
}
#endif

int ReadEnvNonNegativeInt(const char *name, int fallback) {
  if (!name || !*name)
    return fallback;
#ifdef _WIN32
  const std::string value = ReadWin32EnvString(name);
  if (value.empty())
    return fallback;
  char *end = nullptr;
  const long parsed = std::strtol(value.c_str(), &end, 10);
  if (const bool valid = (end != value.c_str() && *end == '\0');
      !valid || parsed < 0 || parsed > 32)
    return fallback;
#else
  const char *value = std::getenv(name);
  if (!value || !*value)
    return fallback;
  char *end = nullptr;
  const long parsed = std::strtol(value, &end, 10);
  if (const bool valid = (end != value && *end == '\0');
      !valid || parsed < 0 || parsed > 32)
    return fallback;
#endif
  return static_cast<int>(parsed);
}

bool ReadEnvBool(const char *name, bool fallback) {
  if (!name || !*name)
    return fallback;
#ifdef _WIN32
  const std::string parsed = ReadWin32EnvString(name);
  if (parsed.empty())
    return fallback;
#else
  const char *value = std::getenv(name);
  if (!value || !*value)
    return fallback;
  const std::string parsed(value);
#endif
  return parsed != "0";
}

const char *SafeGlfwErrorString() {
  const char *description = nullptr;
  if (const int code = glfwGetError(&description); code == GLFW_NO_ERROR)
    return "no-error";
  return description ? description : "unknown-glfw-error";
}

void *GladLoadProcBridge(const char *name) {
  return std::bit_cast<void *>(glfwGetProcAddress(name));
}

std::filesystem::path ResolveWindowIconPath(const std::string &rawPath) {
  if (rawPath.empty())
    return {};

  const std::filesystem::path requested(rawPath);
  const std::array<std::filesystem::path, 4> candidates = {
      requested,
      std::filesystem::current_path() / requested,
      std::filesystem::current_path() / ".." / "sdk" / requested,
      std::filesystem::current_path() / ".." / ".." / "sdk" / requested,
  };
  for (const std::filesystem::path &candidate : candidates) {
    std::error_code ec;
    if (std::filesystem::is_regular_file(candidate, ec) && !ec)
      return candidate.lexically_normal();
  }
  return {};
}

void ApplyRuntimeWindowIcon(GLFWwindow *window, const std::string &iconFile) {
  if (!window || iconFile.empty())
    return;

  const std::filesystem::path iconPath = ResolveWindowIconPath(iconFile);
  if (iconPath.empty()) {
    LogWarn("Window icon not found: {}", iconFile);
    return;
  }

  int width = 0;
  int height = 0;
  int channels = 0;
  unsigned char *pixels =
      stbi_load(iconPath.string().c_str(), &width, &height, &channels, 4);
  if (!pixels) {
    LogWarn("Window icon failed to load '{}': {}", iconPath.string(),
            stbi_failure_reason());
    return;
  }

  GLFWimage image{width, height, pixels};
  glfwSetWindowIcon(window, 1, &image);
  stbi_image_free(pixels);
}

#ifdef _WIN32
void ApplyDarkTitleBar(GLFWwindow *window) {
  if (!window)
    return;

  HWND hwnd = glfwGetWin32Window(window);
  if (!hwnd)
    return;

  BOOL useDarkMode = TRUE;
  if (FAILED(DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE,
                                   &useDarkMode, sizeof(useDarkMode)))) {
    constexpr DWORD kLegacyDarkModeAttribute = 19;
    DwmSetWindowAttribute(hwnd, kLegacyDarkModeAttribute, &useDarkMode,
                          sizeof(useDarkMode));
  }
}
#endif
} // namespace

namespace {
class GlfwContext {
public:
  static void Init() {
    if (s_refCount++ == 0 && !glfwInit())
      throw Horo::WindowInitException("glfwInit failed");
  }

  static void Shutdown() {
    if (--s_refCount == 0) {
      glfwTerminate();
    }
  }

private:
  static inline int s_refCount = 0;
};
} // namespace

namespace Horo {
WindowGraphicsApiTraits
GetWindowGraphicsApiTraits(WindowGraphicsApi graphicsApi) {
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

Window::Window(const WindowSpec &spec)
    : m_width(spec.width), m_height(spec.height), m_vsync(spec.vsync),
      m_graphicsApi(spec.graphicsApi) {
  LogInfo("Window bootstrap begin: title='{}' size={}x{} api={} vsync={}",
          spec.title, spec.width, spec.height,
          static_cast<int>(spec.graphicsApi), spec.vsync ? 1 : 0);
  LogInfo("Calling glfwInit...");
  GlfwContext::Init();
  LogInfo("glfwInit succeeded.");

  const WindowGraphicsApiTraits traits = GetGraphicsApiTraits();

  if (traits.createsClientContext) {
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
    // Allow the system to fall back to a software (offline) renderer when no
    // hardware-accelerated OpenGL is available, e.g. on headless CI runners.
    // NSOpenGLPFAAllowOfflineRenderers is set via this hint; the GPU is still
    // preferred when present.
    glfwWindowHint(GLFW_COCOA_GRAPHICS_SWITCHING, GLFW_TRUE);
#endif
  } else {
    // Vulkan path: no client graphics context should be created by GLFW.
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  }

  glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
  const bool visible = ReadEnvBool("HORO_GLFW_VISIBLE", true);
  glfwWindowHint(GLFW_VISIBLE, visible ? GLFW_TRUE : GLFW_FALSE);
  LogInfo("GLFW window hint: visible={}", visible ? 1 : 0);
  if (traits.requestsMsaaSamples) {
    // Default 4x MSAA; set HORO_GLFW_SAMPLES=0 in headless/CI (llvmpipe) to
    // avoid driver crashes.
    const int samples = ReadEnvNonNegativeInt("HORO_GLFW_SAMPLES", 4);
    glfwWindowHint(GLFW_SAMPLES, samples);
    LogInfo("GLFW window hint: samples={}", samples);
  }

  LogInfo("Calling glfwCreateWindow...");
  m_window = glfwCreateWindow(spec.width, spec.height, spec.title.c_str(),
                              nullptr, nullptr);
  if (!m_window) {
    GlfwContext::Shutdown();
    throw WindowInitException(std::string("glfwCreateWindow failed: ") +
                              SafeGlfwErrorString());
  }
  LogInfo("glfwCreateWindow succeeded: window={}",
          static_cast<void *>(m_window));
  ApplyRuntimeWindowIcon(m_window, spec.iconFile);
#ifdef _WIN32
  ApplyDarkTitleBar(m_window);
#endif

  if (traits.createsClientContext) {
    LogInfo("Making GL context current...");
    glfwMakeContextCurrent(m_window);
    LogInfo("Loading GL via glad...");
    if (!OpenGLContext::InitGlad(GladLoadProcBridge)) {
      glfwDestroyWindow(m_window);
      GlfwContext::Shutdown();
      throw WindowInitException("gladLoadGLLoader failed");
    }
    LogInfo("gladLoadGLLoader succeeded.");
    SetVSync(spec.vsync);
    LogInfo("SetVSync complete: enabled={}", spec.vsync ? 1 : 0);
    LogInfo("OpenGL {} — {}", OpenGLContext::GetGLVersion(),
            OpenGLContext::GetGLRenderer());
  } else {
    // Vulkan presentation pacing is backend-owned; the window stores the
    // preference.
    LogInfo(
        "Window initialized for Vulkan backend (GLFW no-client-api surface).");
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

void Window::PollEvents() const { glfwPollEvents(); }

void Window::SwapBuffers() {
  if (OwnsPresentation())
    glfwSwapBuffers(m_window);
}

bool Window::ShouldClose() const { return glfwWindowShouldClose(m_window); }

void Window::SetVSync(bool enabled) {
  m_vsync = enabled;
  if (OwnsVSync())
    glfwSwapInterval(enabled ? 1 : 0);
}

float Window::GetAspect() const {
  return m_height > 0
             ? static_cast<float>(m_width) / static_cast<float>(m_height)
             : 1.0f;
}

WindowGraphicsApiTraits Window::GetGraphicsApiTraits() const {
  return Horo::GetWindowGraphicsApiTraits(m_graphicsApi);
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

void Window::SetCursorMode(CursorMode mode) {
  int glfwMode;
  switch (mode) {
  case CursorMode::Hidden:
    glfwMode = GLFW_CURSOR_HIDDEN;
    break;
  case CursorMode::Disabled:
    glfwMode = GLFW_CURSOR_DISABLED;
    break;
  default:
    glfwMode = GLFW_CURSOR_NORMAL;
    break;
  }
  glfwSetInputMode(m_window, GLFW_CURSOR, glfwMode);
}

void Window::FramebufferSizeCallback(GLFWwindow *win, int w, int h) {
  auto *self = static_cast<Window *>(glfwGetWindowUserPointer(win));
  self->m_width = w;
  self->m_height = h;
  if (self->OwnsViewportResize())
    Renderer::SetViewport(0, 0, w, h);
  if (self->m_resizeCb)
    self->m_resizeCb(w, h);
}

void Window::WindowCloseCallback(GLFWwindow *win) {
  const auto *self = static_cast<const Window *>(glfwGetWindowUserPointer(win));
  if (self->m_closeCb)
    self->m_closeCb();
}

void Window::DropPathsThunk(GLFWwindow *win, int count, const char **paths) {
  const auto *self = static_cast<const Window *>(glfwGetWindowUserPointer(win));
  if (self->m_fileDropCb)
    self->m_fileDropCb(count, paths);
}
} // namespace Horo
