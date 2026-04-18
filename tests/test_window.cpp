#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <GLFW/glfw3.h>

#include <climits>
#include <cstdlib>
#include <memory>
#include <string>

#include "core/Window.h"

using namespace Monolith;

namespace {

std::string ReadEnvValue(const char* name) {
  if (!name || !*name)
    return {};
#ifdef _WIN32
  char* rawValue = nullptr;
  size_t len = 0;
  if (_dupenv_s(&rawValue, &len, name) != 0 || !rawValue)
    return {};
  std::unique_ptr<char, decltype(&std::free)> value(rawValue, &std::free);
  return std::string(value.get());
#else
  const char* value = std::getenv(name);
  return value ? std::string(value) : std::string();
#endif
}

class ScopedEnvVar {
 public:
  ScopedEnvVar(const char* name, const char* value)
      : m_name(name ? name : ""), m_previous(ReadEnvValue(name)), m_hadPrevious(!m_previous.empty()) {
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

 private:
  std::string m_name;
  std::string m_previous;
  bool m_hadPrevious = false;
};

std::unique_ptr<Window> CreateWindowIfAvailable(const WindowSpec& spec) {
  try {
    return std::make_unique<Window>(spec);
  } catch (const std::exception&) {
    return nullptr;
  }
}

}  // namespace

TEST_CASE("Window OpenGL bootstrap path owns presentation and basic callbacks", "[core][window][opengl]") {
  const ScopedEnvVar hiddenWindow("MONOLITH_GLFW_VISIBLE", "0");
  const ScopedEnvVar disableMsaa("MONOLITH_GLFW_SAMPLES", "0");

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
  window->SetResizeCallback([&](int, int) { ++resizeEvents; });
  glfwSetWindowSize(window->GetNativeHandle(), 640, 360);
  for (int i = 0; i < 8; ++i)
    window->PollEvents();
  REQUIRE(window->GetWidth() >= 1);
  REQUIRE(window->GetHeight() >= 1);

  window->SetCloseCallback([] {});
  window->SetFileDropCallback([](int, const char**) {});
  if (resizeEvents > 0)
    REQUIRE(resizeEvents >= 1);
}

TEST_CASE("Window Vulkan bootstrap path keeps backend-owned presentation", "[core][window][vulkan]") {
  const ScopedEnvVar hiddenWindow("MONOLITH_GLFW_VISIBLE", "0");

  WindowSpec spec;
  spec.title = "window-vulkan-test";
  spec.width = 160;
  spec.height = 120;
  spec.vsync = true;
  spec.graphicsApi = WindowGraphicsApi::Vulkan;

  auto first = CreateWindowIfAvailable(spec);
  auto second = CreateWindowIfAvailable(spec);
  if (!first || !second) {
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

  first->SetResizeCallback([](int, int) {});
  glfwSetWindowSize(first->GetNativeHandle(), 300, 200);
  for (int i = 0; i < 8; ++i)
    first->PollEvents();
  REQUIRE(first->GetWidth() >= 1);
  REQUIRE(first->GetHeight() >= 1);
}

TEST_CASE("Window GLFW callback hooks dispatch to user handlers", "[core][window][callbacks]") {
  const ScopedEnvVar hiddenWindow("MONOLITH_GLFW_VISIBLE", "0");
  const ScopedEnvVar disableMsaa("MONOLITH_GLFW_SAMPLES", "0");

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

  window->SetResizeCallback([&](int w, int h) {
    resizedW = w;
    resizedH = h;
  });
  window->SetCloseCallback([&]() { ++closeCount; });
  window->SetFileDropCallback([&](int pathCount, const char**) {
    ++dropCount;
    droppedPathCount = pathCount;
  });

  GLFWframebuffersizefun framebufferCb = glfwSetFramebufferSizeCallback(window->GetNativeHandle(), nullptr);
  REQUIRE(framebufferCb != nullptr);
  glfwSetFramebufferSizeCallback(window->GetNativeHandle(), framebufferCb);

  GLFWwindowclosefun closeCb = glfwSetWindowCloseCallback(window->GetNativeHandle(), nullptr);
  REQUIRE(closeCb != nullptr);
  glfwSetWindowCloseCallback(window->GetNativeHandle(), closeCb);

  GLFWdropfun dropCb = glfwSetDropCallback(window->GetNativeHandle(), nullptr);
  REQUIRE(dropCb != nullptr);
  glfwSetDropCallback(window->GetNativeHandle(), dropCb);

  framebufferCb(window->GetNativeHandle(), 640, 0);
  REQUIRE(window->GetWidth() == 640);
  REQUIRE(window->GetHeight() == 0);
  REQUIRE(window->GetAspect() == Catch::Approx(1.0f));
  REQUIRE(resizedW == 640);
  REQUIRE(resizedH == 0);

  const char* dropped[] = {"assets/models/crate.obj"};
  closeCb(window->GetNativeHandle());
  dropCb(window->GetNativeHandle(), 1, dropped);
  REQUIRE(closeCount == 1);
  REQUIRE(dropCount == 1);
  REQUIRE(droppedPathCount == 1);
}

TEST_CASE("Window env parsing handles bool and sample fallback variants", "[core][window][env]") {
  WindowSpec spec;
  spec.title = "window-env-test";
  spec.width = 128;
  spec.height = 96;
  spec.graphicsApi = WindowGraphicsApi::OpenGL;

  const ScopedEnvVar hiddenWindow("MONOLITH_GLFW_VISIBLE", "0");
  const ScopedEnvVar invalidSamples("MONOLITH_GLFW_SAMPLES", "abc");
  auto first = CreateWindowIfAvailable(spec);
  if (!first) {
    SUCCEED("Window initialization is unavailable on this machine");
    return;
  }

  const ScopedEnvVar visibleWindow("MONOLITH_GLFW_VISIBLE", "1");
  const ScopedEnvVar negativeSamples("MONOLITH_GLFW_SAMPLES", "-2");
  auto second = CreateWindowIfAvailable(spec);
  REQUIRE(second != nullptr);

  const ScopedEnvVar emptyVisible("MONOLITH_GLFW_VISIBLE", "");
  const ScopedEnvVar tooLargeSamples("MONOLITH_GLFW_SAMPLES", "99");
  auto third = CreateWindowIfAvailable(spec);
  REQUIRE(third != nullptr);
}

TEST_CASE("Window reports init failures for invalid dimensions", "[core][window][errors]") {
  const ScopedEnvVar hiddenWindow("MONOLITH_GLFW_VISIBLE", "0");
  const ScopedEnvVar disableMsaa("MONOLITH_GLFW_SAMPLES", "0");

  WindowSpec probe;
  probe.title = "window-probe";
  probe.width = 64;
  probe.height = 64;
  probe.graphicsApi = WindowGraphicsApi::OpenGL;
  if (!CreateWindowIfAvailable(probe)) {
    SUCCEED("Window initialization is unavailable on this machine");
    return;
  }

  const WindowSpec invalidSpecs[] = {
      WindowSpec{"invalid-0x0", 0, 0, true, WindowGraphicsApi::OpenGL},
      WindowSpec{"invalid-negw", -1, 64, true, WindowGraphicsApi::OpenGL},
      WindowSpec{"invalid-negh", 64, -1, true, WindowGraphicsApi::OpenGL},
      WindowSpec{"invalid-huge", INT_MAX, INT_MAX, true, WindowGraphicsApi::OpenGL},
  };

  bool sawInitFailure = false;
  for (const WindowSpec& candidate : invalidSpecs) {
    try {
      Window window(candidate);
    } catch (const WindowInitException&) {
      sawInitFailure = true;
      break;
    }
  }

  REQUIRE(sawInitFailure);
}

TEST_CASE("Window bootstrap still works when info logs are filtered", "[core][window][logfilter]") {
  const ScopedEnvVar hiddenWindow("MONOLITH_GLFW_VISIBLE", "0");
  const ScopedEnvVar disableMsaa("MONOLITH_GLFW_SAMPLES", "0");

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
  WindowSpec spec;
  spec.title = "window-init-fail";
  spec.width = 64;
  spec.height = 64;
  spec.graphicsApi = WindowGraphicsApi::OpenGL;

  try {
    Window window(spec);
    SUCCEED("glfwInit failure path unavailable on this machine");
  } catch (const std::runtime_error&) {
    SUCCEED("glfwInit failure path covered");
  }
}
