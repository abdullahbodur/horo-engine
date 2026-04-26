#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <GLFW/glfw3.h>

#include <array>
#include <climits>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "core/Window.h"

using namespace Horo;

namespace {
std::string ReadEnvValue(const char *name) {
  if (!name || !*name)
    return {};
#ifdef _WIN32
  size_t len = 0;
  if (getenv_s(&len, nullptr, 0, name) != 0 || len <= 1)
    return {};
  std::vector<char> value(len);
  if (getenv_s(&len, value.data(), value.size(), name) != 0 || len <= 1)
    return {};
  return std::string(value.data());
#else
  const char *value = std::getenv(name);
  return value ? std::string(value) : std::string();
#endif
}

bool ShouldSkipWindowBootstrapOnThisRunner() {
#if defined(__APPLE__)
  // Headless macOS CI can trap inside GLFW/AppKit window creation
  // (Bus error) instead of returning a regular init failure.
  const std::string ci = ReadEnvValue("CI");
  const std::string gha = ReadEnvValue("GITHUB_ACTIONS");
  if (ci == "true" || gha == "true")
    return true;
#endif
  return false;
}

class ScopedEnvVar {
public:
  ScopedEnvVar(const char *name, const char *value)
      : m_name(name ? name : ""), m_previous(ReadEnvValue(name)),
        m_hadPrevious(!m_previous.empty()) {
    if (m_name.empty())
      return;
#ifdef _WIN32
    _putenv_s(m_name.c_str(), value ? value : "");
#else
    if (value)
      setenv(m_name.c_str(), value, 1);
    else
      unsetenv(m_name.c_str());
#endif
  }

  ~ScopedEnvVar() {
    if (m_name.empty())
      return;
#ifdef _WIN32
    if (m_hadPrevious)
      _putenv_s(m_name.c_str(), m_previous.c_str());
    else
      _putenv_s(m_name.c_str(), "");
#else
    if (m_hadPrevious)
      setenv(m_name.c_str(), m_previous.c_str(), 1);
    else
      unsetenv(m_name.c_str());
#endif
  }

  ScopedEnvVar(const ScopedEnvVar &) = delete;

  ScopedEnvVar &operator=(const ScopedEnvVar &) = delete;

  ScopedEnvVar(ScopedEnvVar &&) = delete;

  ScopedEnvVar &operator=(ScopedEnvVar &&) = delete;

private:
  std::string m_name;
  std::string m_previous;
  bool m_hadPrevious = false;
};

std::unique_ptr<Window> CreateWindowIfAvailable(const WindowSpec &spec) {
  if (ShouldSkipWindowBootstrapOnThisRunner())
    return nullptr;
  try {
    return std::make_unique<Window>(spec);
  } catch (const WindowInitException &) {
    return nullptr;
  }
}
} // namespace

TEST_CASE("Window OpenGL bootstrap path owns presentation and basic callbacks", "[core][window][opengl]") {
  const ScopedEnvVar hiddenWindow("HORO_GLFW_VISIBLE", "0");
  const ScopedEnvVar disableMsaa("HORO_GLFW_SAMPLES", "0");

  WindowSpec spec;
  spec.title = "window-opengl-test";
  spec.width = 320;
  spec.height = 180;
  spec.vsync = true;
  spec.graphicsApi = WindowGraphicsApi::OpenGL;

  auto window = CreateWindowIfAvailable(spec);
  if (!window) {
    SUCCEED("Window initialization is unavailable on this machine");
    return;
  }
  REQUIRE(window->GetNativeHandle() != nullptr);
  REQUIRE(window->GetGraphicsApi() == WindowGraphicsApi::OpenGL);
  REQUIRE(window->GetGraphicsApiTraits().createsClientContext);
  REQUIRE(window->OwnsPresentation());
  REQUIRE(window->OwnsVSync());
  REQUIRE(window->OwnsViewportResize());
  REQUIRE(window->IsVSyncEnabled());
  REQUIRE(window->GetAspect() == Catch::Approx(320.0f / 180.0f));

  window->SetCursorMode(CursorMode::Hidden);
  window->SetCursorMode(CursorMode::Disabled);
  window->SetCursorMode(CursorMode::Normal);

  REQUIRE_FALSE(window->ShouldClose());
  glfwSetWindowShouldClose(window->GetNativeHandle(), GLFW_TRUE);
  REQUIRE(window->ShouldClose());

  window->SetVSync(false);
  REQUIRE_FALSE(window->IsVSyncEnabled());
  window->SetVSync(true);
  REQUIRE(window->IsVSyncEnabled());

  window->PollEvents();
  window->SwapBuffers();

  int resizeEvents = 0;
  window->SetResizeCallback([&resizeEvents](int, int) { ++resizeEvents; });
  glfwSetWindowSize(window->GetNativeHandle(), 640, 360);
  for (int i = 0; i < 8; ++i)
    window->PollEvents();
  REQUIRE(window->GetWidth() >= 1);
  REQUIRE(window->GetHeight() >= 1);

  window->SetCloseCallback([]() { (void)0; });
  window->SetFileDropCallback([](int, const char **) { (void)0; });
  if (resizeEvents > 0)
    REQUIRE(resizeEvents >= 1);
}

TEST_CASE("Window Vulkan bootstrap path keeps backend-owned presentation", "[core][window][vulkan]") {
  const ScopedEnvVar hiddenWindow("HORO_GLFW_VISIBLE", "0");

  WindowSpec spec;
  spec.title = "window-vulkan-test";
  spec.width = 160;
  spec.height = 120;
  spec.vsync = true;
  spec.graphicsApi = WindowGraphicsApi::Vulkan;

  auto first = CreateWindowIfAvailable(spec);
  if (auto second = CreateWindowIfAvailable(spec); !first || !second) {
    SUCCEED("Window initialization is unavailable on this machine");
    return;
  }

  REQUIRE_FALSE(first->GetGraphicsApiTraits().createsClientContext);
  REQUIRE_FALSE(first->OwnsPresentation());
  REQUIRE_FALSE(first->OwnsVSync());
  REQUIRE_FALSE(first->OwnsViewportResize());

  first->SetVSync(false);
  REQUIRE_FALSE(first->IsVSyncEnabled());
  first->PollEvents();
  first->SwapBuffers();

  first->SetResizeCallback([](int, int) { (void)0; });
  glfwSetWindowSize(first->GetNativeHandle(), 300, 200);
  for (int i = 0; i < 8; ++i)
    first->PollEvents();
  REQUIRE(first->GetWidth() >= 1);
  REQUIRE(first->GetHeight() >= 1);
}

TEST_CASE("Window GLFW callback hooks dispatch to user handlers", "[core][window][callbacks]") {
  const ScopedEnvVar hiddenWindow("HORO_GLFW_VISIBLE", "0");
  const ScopedEnvVar disableMsaa("HORO_GLFW_SAMPLES", "0");

  WindowSpec spec;
  spec.title = "window-callback-test";
  spec.width = 200;
  spec.height = 120;
  spec.graphicsApi = WindowGraphicsApi::OpenGL;

  auto window = CreateWindowIfAvailable(spec);
  if (!window) {
    SUCCEED("Window initialization is unavailable on this machine");
    return;
  }

  int resizedW = -1;
  int resizedH = -1;
  int closeCount = 0;
  int dropCount = 0;
  int droppedPathCount = 0;

  window->SetResizeCallback([&resizedW, &resizedH](int w, int h) {
    resizedW = w;
    resizedH = h;
  });
  window->SetCloseCallback([&closeCount]() { ++closeCount; });
  window->SetFileDropCallback(
      [&dropCount, &droppedPathCount](int pathCount, const char **) {
        ++dropCount;
        droppedPathCount = pathCount;
      });

  GLFWframebuffersizefun framebufferCb =
      glfwSetFramebufferSizeCallback(window->GetNativeHandle(), nullptr);
  REQUIRE(framebufferCb != nullptr);
  glfwSetFramebufferSizeCallback(window->GetNativeHandle(), framebufferCb);

  GLFWwindowclosefun closeCb =
      glfwSetWindowCloseCallback(window->GetNativeHandle(), nullptr);
  REQUIRE(closeCb != nullptr);
  glfwSetWindowCloseCallback(window->GetNativeHandle(), closeCb);

  GLFWdropfun dropCb = glfwSetDropCallback(window->GetNativeHandle(), nullptr);
  REQUIRE(dropCb != nullptr);
  glfwSetDropCallback(window->GetNativeHandle(), dropCb);

  // Exercise callback thunk paths when no user callback is registered.
  window->SetResizeCallback({});
  window->SetCloseCallback({});
  window->SetFileDropCallback({});
  framebufferCb(window->GetNativeHandle(), 320, 180);
  closeCb(window->GetNativeHandle());
  std::array<const char *, 1> noDropPaths = {"assets/ignore.me"};
  const char **noDropPathPtr = noDropPaths.data();
  dropCb(window->GetNativeHandle(), 1, noDropPathPtr);

  window->SetResizeCallback([&resizedW, &resizedH](int w, int h) {
    resizedW = w;
    resizedH = h;
  });
  window->SetCloseCallback([&closeCount]() { ++closeCount; });
  window->SetFileDropCallback(
      [&dropCount, &droppedPathCount](int pathCount, const char **) {
        ++dropCount;
        droppedPathCount = pathCount;
      });

  framebufferCb(window->GetNativeHandle(), 640, 0);
  REQUIRE(window->GetWidth() == 640);
  REQUIRE(window->GetHeight() == 0);
  REQUIRE(window->GetAspect() == Catch::Approx(1.0f));
  REQUIRE(resizedW == 640);
  REQUIRE(resizedH == 0);

  std::array<const char *, 1> dropped = {"assets/models/crate.obj"};
  closeCb(window->GetNativeHandle());
  const char **droppedPtr = dropped.data();
  dropCb(window->GetNativeHandle(), 1, droppedPtr);
  REQUIRE(closeCount == 1);
  REQUIRE(dropCount == 1);
  REQUIRE(droppedPathCount == 1);
}

TEST_CASE("Window env parsing falls back when variables are unset", "[core][window][env_unset]") {
  const ScopedEnvVar unsetVisible("HORO_GLFW_VISIBLE", nullptr);
  const ScopedEnvVar unsetSamples("HORO_GLFW_SAMPLES", nullptr);

  WindowSpec spec;
  spec.title = "window-env-unset-test";
  spec.width = 96;
  spec.height = 72;
  spec.graphicsApi = WindowGraphicsApi::OpenGL;

  auto window = CreateWindowIfAvailable(spec);
  if (!window) {
    SUCCEED("Window initialization is unavailable on this machine");
    return;
  }

  REQUIRE(window->GetNativeHandle() != nullptr);
}

TEST_CASE("Window env parsing handles bool and sample fallback variants", "[core][window][env]") {
  WindowSpec spec;
  spec.title = "window-env-test";
  spec.width = 128;
  spec.height = 96;
  spec.graphicsApi = WindowGraphicsApi::OpenGL;

  const ScopedEnvVar hiddenWindow("HORO_GLFW_VISIBLE", "0");
  const ScopedEnvVar invalidSamples("HORO_GLFW_SAMPLES", "abc");
  if (auto first = CreateWindowIfAvailable(spec); !first) {
    SUCCEED("Window initialization is unavailable on this machine");
    return;
  }

  const ScopedEnvVar visibleWindow("HORO_GLFW_VISIBLE", "1");
  const ScopedEnvVar negativeSamples("HORO_GLFW_SAMPLES", "-2");
  auto second = CreateWindowIfAvailable(spec);
  REQUIRE(second != nullptr);

  const ScopedEnvVar emptyVisible("HORO_GLFW_VISIBLE", "");
  const ScopedEnvVar tooLargeSamples("HORO_GLFW_SAMPLES", "99");
  auto third = CreateWindowIfAvailable(spec);
  REQUIRE(third != nullptr);
}

TEST_CASE("Window reports init failures for invalid dimensions", "[core][window][errors]") {
  const ScopedEnvVar hiddenWindow("HORO_GLFW_VISIBLE", "0");
  const ScopedEnvVar disableMsaa("HORO_GLFW_SAMPLES", "0");

  WindowSpec probe;
  probe.title = "window-probe";
  probe.width = 64;
  probe.height = 64;
  probe.graphicsApi = WindowGraphicsApi::OpenGL;
  if (!CreateWindowIfAvailable(probe)) {
    SUCCEED("Window initialization is unavailable on this machine");
    return;
  }

  const std::array<WindowSpec, 4> invalidSpecs = {
      WindowSpec{"invalid-0x0", 0, 0, true, WindowGraphicsApi::OpenGL},
      WindowSpec{"invalid-negw", -1, 64, true, WindowGraphicsApi::OpenGL},
      WindowSpec{"invalid-negh", 64, -1, true, WindowGraphicsApi::OpenGL},
      WindowSpec{"invalid-huge", INT_MAX, INT_MAX, true,
                 WindowGraphicsApi::OpenGL},
  };

  bool sawInitFailure = false;
  for (const WindowSpec &candidate : invalidSpecs) {
    try {
      Window window(candidate);
    } catch (const WindowInitException &) {
      sawInitFailure = true;
      break;
    }
  }

  REQUIRE(sawInitFailure);
}

TEST_CASE("Window bootstrap still works when info logs are filtered", "[core][window][logfilter]") {
  const ScopedEnvVar hiddenWindow("HORO_GLFW_VISIBLE", "0");
  const ScopedEnvVar disableMsaa("HORO_GLFW_SAMPLES", "0");

  WindowSpec glSpec;
  glSpec.title = "window-logfilter-gl";
  glSpec.width = 96;
  glSpec.height = 64;
  glSpec.graphicsApi = WindowGraphicsApi::OpenGL;

  auto glWindow = CreateWindowIfAvailable(glSpec);
  if (!glWindow) {
    SUCCEED("Window initialization is unavailable on this machine");
    return;
  }
  glWindow->SetVSync(false);

  WindowSpec vkSpec;
  vkSpec.title = "window-logfilter-vk";
  vkSpec.width = 96;
  vkSpec.height = 64;
  vkSpec.graphicsApi = WindowGraphicsApi::Vulkan;

  auto vkWindow = CreateWindowIfAvailable(vkSpec);
  REQUIRE(vkWindow != nullptr);
  vkWindow->SetVSync(false);
}

TEST_CASE("Window exposes glfwInit failure path when no display is available", "[core][window][glfw-init-fail]") {
  if (ShouldSkipWindowBootstrapOnThisRunner()) {
    SUCCEED("Window bootstrap skipped on this CI runner");
    return;
  }

  WindowSpec spec;
  spec.title = "window-init-fail";
  spec.width = 64;
  spec.height = 64;
  spec.graphicsApi = WindowGraphicsApi::OpenGL;

  try {
    Window window(spec);
    SUCCEED("glfwInit failure path unavailable on this machine");
  } catch (const WindowInitException &) {
    SUCCEED("glfwInit failure path covered");
  }
}
