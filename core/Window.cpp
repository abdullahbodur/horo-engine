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
#ifndef DWMWA_BORDER_COLOR
#define DWMWA_BORDER_COLOR 34
#endif
#ifndef DWMWA_CAPTION_COLOR
#define DWMWA_CAPTION_COLOR 35
#endif
#ifndef DWMWA_TEXT_COLOR
#define DWMWA_TEXT_COLOR 36
#endif
#endif
#ifdef __APPLE__
extern "C" void ApplyDarkTitleBarMac(GLFWwindow *glfwWindow);
extern "C" void ApplyAppIconMac(const char *iconPath);
extern "C" void SetupNativeMenuBarMac();

static std::function<void()> g_fileNewCallback;
static std::function<void()> g_fileOpenCallback;
static std::function<void()> g_fileResetLayoutCallback;
static std::function<void()> g_fileSettingsCallback;
static std::function<void()> g_fileCloseEditorCallback;
static std::function<void()> g_addPanelCallback;
static std::function<void()> g_addPropCallback;
static std::function<void()> g_addLightCallback;
static std::function<void()> g_addCameraCallback;
static std::function<void()> g_addPropFromAssetCallback;
static std::function<void()> g_editUndoCallback;
static std::function<void()> g_editRedoCallback;
static std::function<void()> g_editRenameCallback;
static std::function<void()> g_editCreatePrefabCallback;
static std::function<void()> g_editDuplicateCallback;
static std::function<void()> g_editDeleteCallback;
static std::function<void()> g_viewFlyModeCallback;
static std::function<void()> g_viewHelpCallback;
static std::function<void()> g_viewQuickOpenCallback;
static std::function<void()> g_viewCommandPaletteCallback;
static std::function<void()> g_viewResetLayoutCallback;

extern "C" void InvokeFileNew() { if (g_fileNewCallback) g_fileNewCallback(); }
extern "C" void InvokeFileOpen() { if (g_fileOpenCallback) g_fileOpenCallback(); }
extern "C" void InvokeFileResetLayout() { if (g_fileResetLayoutCallback) g_fileResetLayoutCallback(); }
extern "C" void InvokeFileSettings() { if (g_fileSettingsCallback) g_fileSettingsCallback(); }
extern "C" void InvokeFileCloseEditor() { if (g_fileCloseEditorCallback) g_fileCloseEditorCallback(); }
extern "C" void InvokeAddPanel() { if (g_addPanelCallback) g_addPanelCallback(); }
extern "C" void InvokeAddProp() { if (g_addPropCallback) g_addPropCallback(); }
extern "C" void InvokeAddLight() { if (g_addLightCallback) g_addLightCallback(); }
extern "C" void InvokeAddCamera() { if (g_addCameraCallback) g_addCameraCallback(); }
extern "C" void InvokeAddPropFromAsset() { if (g_addPropFromAssetCallback) g_addPropFromAssetCallback(); }
extern "C" void InvokeEditUndo() { if (g_editUndoCallback) g_editUndoCallback(); }
extern "C" void InvokeEditRedo() { if (g_editRedoCallback) g_editRedoCallback(); }
extern "C" void InvokeEditRename() { if (g_editRenameCallback) g_editRenameCallback(); }
extern "C" void InvokeEditCreatePrefab() { if (g_editCreatePrefabCallback) g_editCreatePrefabCallback(); }
extern "C" void InvokeEditDuplicate() { if (g_editDuplicateCallback) g_editDuplicateCallback(); }
extern "C" void InvokeEditDelete() { if (g_editDeleteCallback) g_editDeleteCallback(); }
extern "C" void InvokeViewFlyMode() { if (g_viewFlyModeCallback) g_viewFlyModeCallback(); }
extern "C" void InvokeViewHelp() { if (g_viewHelpCallback) g_viewHelpCallback(); }
extern "C" void InvokeViewQuickOpen() { if (g_viewQuickOpenCallback) g_viewQuickOpenCallback(); }
extern "C" void InvokeViewCommandPalette() { if (g_viewCommandPaletteCallback) g_viewCommandPaletteCallback(); }
extern "C" void InvokeViewResetLayout() { if (g_viewResetLayoutCallback) g_viewResetLayoutCallback(); }
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
  std::error_code cwdEc;
  const std::filesystem::path cwd = std::filesystem::current_path(cwdEc);
  const std::array<std::filesystem::path, 4> candidates = {
      requested,
      !cwdEc ? cwd / requested : std::filesystem::path{},
      !cwdEc ? cwd / ".." / "sdk" / requested : std::filesystem::path{},
      !cwdEc ? cwd / ".." / ".." / "sdk" / requested : std::filesystem::path{},
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

  const COLORREF captionColor = RGB(1, 7, 17);
  const COLORREF textColor = RGB(245, 247, 255);
  const COLORREF borderColor = RGB(20, 42, 70);
  DwmSetWindowAttribute(hwnd, DWMWA_CAPTION_COLOR, &captionColor,
                        sizeof(captionColor));
  DwmSetWindowAttribute(hwnd, DWMWA_TEXT_COLOR, &textColor, sizeof(textColor));
  DwmSetWindowAttribute(hwnd, DWMWA_BORDER_COLOR, &borderColor,
                        sizeof(borderColor));
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
#elif defined(__APPLE__)
  ApplyDarkTitleBarMac(m_window);
  {
    const std::filesystem::path iconPath = ResolveWindowIconPath(spec.iconFile);
    if (!iconPath.empty())
      ApplyAppIconMac(iconPath.string().c_str());
  }
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

void Window::SetupNativeMenuBar() const {
#ifdef __APPLE__
  SetupNativeMenuBarMac();
#endif
}

void Window::SetMenuCallbacks(MenuCallbacks callbacks) const {
#ifdef __APPLE__
  g_fileNewCallback = std::move(callbacks.fileNew);
  g_fileOpenCallback = std::move(callbacks.fileOpen);
  g_fileResetLayoutCallback = std::move(callbacks.fileResetLayout);
  g_fileSettingsCallback = std::move(callbacks.fileSettings);
  g_fileCloseEditorCallback = std::move(callbacks.fileCloseEditor);
  g_addPanelCallback = std::move(callbacks.addPanel);
  g_addPropCallback = std::move(callbacks.addProp);
  g_addLightCallback = std::move(callbacks.addLight);
  g_addCameraCallback = std::move(callbacks.addCamera);
  g_addPropFromAssetCallback = std::move(callbacks.addPropFromAsset);
  g_editUndoCallback = std::move(callbacks.editUndo);
  g_editRedoCallback = std::move(callbacks.editRedo);
  g_editRenameCallback = std::move(callbacks.editRename);
  g_editCreatePrefabCallback = std::move(callbacks.editCreatePrefab);
  g_editDuplicateCallback = std::move(callbacks.editDuplicate);
  g_editDeleteCallback = std::move(callbacks.editDelete);
  g_viewFlyModeCallback = std::move(callbacks.viewFlyMode);
  g_viewHelpCallback = std::move(callbacks.viewHelp);
  g_viewQuickOpenCallback = std::move(callbacks.viewQuickOpen);
  g_viewCommandPaletteCallback = std::move(callbacks.viewCommandPalette);
  g_viewResetLayoutCallback = std::move(callbacks.viewResetLayout);
#else
  (void)callbacks;
#endif
}
} // namespace Horo
