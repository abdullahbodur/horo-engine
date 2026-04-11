#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <memory>
#include <string>
#include <vector>

#if defined(MONOLITH_HAS_VULKAN)
#include <GLFW/glfw3.h>
#endif

#include "math/Mat4.h"
#include "renderer/IRenderBackend.h"
#include "renderer/Material.h"
#include "renderer/Mesh.h"
#include "renderer/RenderBackend.h"
#include "renderer/Renderer.h"
#include "renderer/RenderTargetHandle.h"
#include "renderer/RenderViewUtils.h"
#include "renderer/VulkanRenderBackend.h"
#include "scene/Registry.h"
#include "scene/components/MeshComponent.h"
#include "scene/components/TransformComponent.h"
#include "scene/systems/RenderSystem.h"

using namespace Monolith;

namespace
{

  class FakeRenderBackend : public IRenderBackend
  {
  public:
    void BeginFrame(const RenderFrameConfig &frame) override
    {
      events.push_back("begin-frame");
      lastFrame = frame;
    }

    void EndFrame() override
    {
      events.push_back("end-frame");
    }

    void BeginPass(const RenderPassConfig &pass) override
    {
      events.push_back("begin-pass");
      lastPass = pass;
    }

    void EndPass() override
    {
      events.push_back("end-pass");
    }

    void DrawMesh(const MeshDrawCommand &command) override
    {
      events.push_back("draw-mesh");
      lastMesh = command;
      ++drawCalls;
    }

    void DrawSkinnedMesh(const SkinnedMeshDrawCommand &command) override
    {
      events.push_back("draw-skinned");
      lastSkinned = command;
      ++drawCalls;
    }

    void DrawWireframe(const WireframeDrawCommand &command) override
    {
      events.push_back("draw-wireframe");
      lastWireframe = command;
      ++drawCalls;
    }

    RenderBackendId GetBackendId() const override { return RenderBackendId::OpenGL; }
    RenderBackendCapabilities GetCapabilities() const override
    {
      return GetDefaultRenderBackendCapabilities(RenderBackendId::OpenGL);
    }
    int GetDrawCallCount() const override { return drawCalls; }
    bool ReadbackColorBgr8(int width,
                           int height,
                           std::vector<uint8_t> &outPixels,
                           std::string *outError) override
    {
      if (width <= 0 || height <= 0)
      {
        if (outError)
          *outError = "invalid color readback dimensions";
        return false;
      }
      outPixels.assign(static_cast<size_t>(width * height * 3), 0u);
      return true;
    }
    bool ReadbackDepth32F(int width,
                          int height,
                          std::vector<float> &outDepth,
                          std::string *outError) override
    {
      if (width <= 0 || height <= 0)
      {
        if (outError)
          *outError = "invalid depth readback dimensions";
        return false;
      }
      outDepth.assign(static_cast<size_t>(width * height), 1.0f);
      return true;
    }
    bool EnsureEditorViewportRenderTarget(uint32_t,
                                          uint32_t,
                                          std::string *outError) override
    {
      if (outError)
        *outError = "fake backend has no viewport targets";
      return false;
    }
    bool TryGetEditorViewportRenderTargetHandle(RenderTargetHandle *outHandle,
                                                bool,
                                                std::string *outError) override
    {
      if (outHandle)
        *outHandle = {};
      if (outError)
        *outError = "fake backend has no viewport target handle";
      return false;
    }
    std::vector<std::string> events;
    RenderFrameConfig lastFrame;
    RenderPassConfig lastPass;
    MeshDrawCommand lastMesh;
    SkinnedMeshDrawCommand lastSkinned;
    WireframeDrawCommand lastWireframe;
    int drawCalls = 0;
  };

#if defined(MONOLITH_HAS_VULKAN)
  struct OverlayRenderProbe
  {
    bool invoked = false;
    void *commandBufferHandle = nullptr;
  };

  void CaptureOverlayRenderProbe(void *userData, void *commandBufferHandle)
  {
    if (!userData)
      return;
    auto *probe = static_cast<OverlayRenderProbe *>(userData);
    probe->invoked = true;
    probe->commandBufferHandle = commandBufferHandle;
  }
#endif

} // namespace

TEST_CASE("Renderer routes explicit frame and pass commands through backend seam",
          "[renderer][foundation]")
{
  FakeRenderBackend backend;
  Renderer::UseBackend(&backend);

  const Camera camera;
  std::vector<Light> lights(2);
  Mesh mesh;
  Material material;

  Renderer::BeginFrame(RenderFrameConfig{lights, "test-frame"});
  Renderer::BeginPass(RenderPassConfig{RenderPassId::OpaqueScene, BuildRenderView(camera), "main"});
  Renderer::Submit(mesh, Mat4::Identity(), material);
  Renderer::EndPass();
  Renderer::EndFrame();

  REQUIRE(backend.events ==
          std::vector<std::string>{"begin-frame", "begin-pass", "draw-mesh", "end-pass", "end-frame"});
  REQUIRE(backend.lastFrame.lights.size() == 2);
  REQUIRE(backend.lastPass.id == RenderPassId::OpaqueScene);
  REQUIRE(backend.lastMesh.mesh == &mesh);
  REQUIRE(backend.lastMesh.material == &material);
  REQUIRE(Renderer::GetDrawCallCount() == 1);
  std::vector<uint8_t> colorReadback;
  std::vector<float> depthReadback;
  std::string readbackError;
  REQUIRE(Renderer::ReadbackColorBgr8(2, 2, colorReadback, &readbackError));
  REQUIRE(colorReadback.size() == 12u);
  REQUIRE(Renderer::ReadbackDepth32F(2, 2, depthReadback, &readbackError));
  REQUIRE(depthReadback.size() == 4u);
  RenderTargetHandle viewportHandle;
  REQUIRE_FALSE(Renderer::EnsureEditorViewportRenderTarget(320u, 180u, &readbackError));
  REQUIRE(readbackError.find("no viewport targets") != std::string::npos);
  REQUIRE_FALSE(Renderer::TryGetEditorViewportRenderTargetHandle(&viewportHandle, false, &readbackError));
  REQUIRE(readbackError.find("no viewport target handle") != std::string::npos);
  REQUIRE_FALSE(viewportHandle.IsValid());

  Renderer::ResetBackend();
}

TEST_CASE("Renderer forwards frame output config through the backend seam",
          "[renderer][foundation][frame-output]")
{
  FakeRenderBackend backend;
  Renderer::UseBackend(&backend);

  const Vec4 clearColor{0.3f, 0.2f, 0.1f, 1.0f};
  Renderer::BeginFrame(RenderFrameConfig{{}, "frame-output", clearColor, false, true});
  Renderer::EndFrame();

  REQUIRE(backend.events == std::vector<std::string>{"begin-frame", "end-frame"});
  REQUIRE(backend.lastFrame.debugLabel == "frame-output");
  REQUIRE(backend.lastFrame.clearColor == clearColor);
  REQUIRE_FALSE(backend.lastFrame.clearColorBuffer);
  REQUIRE(backend.lastFrame.clearDepthBuffer);

  Renderer::ResetBackend();
}

TEST_CASE("Renderer initializes the default OpenGL backend through a typed selection",
          "[renderer][foundation][backend]")
{
  const RenderBackendInitResult init = Renderer::InitializeBackend({RenderBackendId::Auto});

  REQUIRE(init.ok);
  REQUIRE(init.requested == RenderBackendId::Auto);
  REQUIRE(init.selected == RenderBackendId::OpenGL);
  REQUIRE(Renderer::GetBackendId() == RenderBackendId::OpenGL);

  const RenderBackendCapabilities caps = Renderer::GetBackendCapabilities();
  REQUIRE(caps.supportsDebugDraw);
  REQUIRE(caps.supportsWireframeOverlay);
  REQUIRE(caps.supportsOffscreenTargets);
  REQUIRE(caps.supportsNativeTextureHandles);
  REQUIRE(caps.supportsReadback);
  REQUIRE(caps.supportsDepthReadback);
  REQUIRE(caps.supportsDebugHud);
  REQUIRE_FALSE(caps.supportsComputePasses);
}

TEST_CASE("Renderer rejects unsupported backend requests without replacing the active backend",
          "[renderer][foundation][backend]")
{
  REQUIRE(Renderer::InitializeBackend({RenderBackendId::OpenGL}).ok);

  const RenderBackendInitResult init = Renderer::InitializeBackend({RenderBackendId::Vulkan});

#if defined(MONOLITH_HAS_VULKAN)
  REQUIRE_FALSE(init.ok);
  REQUIRE(init.selected == RenderBackendId::Vulkan);
  REQUIRE(init.error.find("window handle") != std::string::npos);
  REQUIRE(Renderer::GetBackendId() == RenderBackendId::OpenGL);
  REQUIRE(Renderer::IsBackendSupported(RenderBackendId::Vulkan));

  const RenderBackendCapabilities vulkanCaps =
      GetDefaultRenderBackendCapabilities(RenderBackendId::Vulkan);
  REQUIRE_FALSE(vulkanCaps.supportsDebugDraw);
  REQUIRE_FALSE(vulkanCaps.supportsWireframeOverlay);
  REQUIRE(vulkanCaps.supportsOffscreenTargets);
  REQUIRE(vulkanCaps.supportsNativeTextureHandles);
  REQUIRE_FALSE(vulkanCaps.supportsReadback);
  REQUIRE_FALSE(vulkanCaps.supportsDepthReadback);
  REQUIRE_FALSE(vulkanCaps.supportsDebugHud);
#else
  REQUIRE_FALSE(init.ok);
  REQUIRE(init.selected == RenderBackendId::Vulkan);
  REQUIRE_FALSE(init.error.empty());
  REQUIRE(Renderer::GetBackendId() == RenderBackendId::OpenGL);
  REQUIRE_FALSE(Renderer::IsBackendSupported(RenderBackendId::Vulkan));
#endif
}

TEST_CASE("RenderBackendSelection preserves native window handles for backend bootstrap",
          "[renderer][foundation][backend][selection]")
{
  RenderBackendSelection selection;
  selection.requested = RenderBackendId::Vulkan;
  selection.nativeWindowHandle = reinterpret_cast<void *>(0x1234);

  REQUIRE(selection.requested == RenderBackendId::Vulkan);
  REQUIRE(selection.nativeWindowHandle == reinterpret_cast<void *>(0x1234));
}

TEST_CASE("Backend capability defaults express the current parity matrix",
          "[renderer][foundation][backend][parity]")
{
  const RenderBackendCapabilities glCaps = GetDefaultRenderBackendCapabilities(RenderBackendId::OpenGL);
  REQUIRE(glCaps.supportsDebugDraw);
  REQUIRE(glCaps.supportsWireframeOverlay);
  REQUIRE(glCaps.supportsOffscreenTargets);
  REQUIRE(glCaps.supportsNativeTextureHandles);
  REQUIRE(glCaps.supportsReadback);
  REQUIRE(glCaps.supportsDepthReadback);
  REQUIRE(glCaps.supportsDebugHud);

  const RenderBackendCapabilities vkCaps = GetDefaultRenderBackendCapabilities(RenderBackendId::Vulkan);
  REQUIRE_FALSE(vkCaps.supportsDebugDraw);
  REQUIRE_FALSE(vkCaps.supportsWireframeOverlay);
  REQUIRE(vkCaps.supportsOffscreenTargets);
  REQUIRE(vkCaps.supportsNativeTextureHandles);
  REQUIRE_FALSE(vkCaps.supportsReadback);
  REQUIRE_FALSE(vkCaps.supportsDepthReadback);
  REQUIRE_FALSE(vkCaps.supportsDebugHud);
}

TEST_CASE("RenderTargetHandle constructors preserve backend-native metadata",
          "[renderer][foundation][handles]")
{
  const RenderTargetHandle glHandle = RenderTargetHandle::OpenGLTexture(42u, true);
  REQUIRE(glHandle.IsValid());
  REQUIRE(glHandle.backendId == RenderBackendId::OpenGL);
  REQUIRE(glHandle.nativeType == RenderNativeHandleType::OpenGLTexture2D);
  REQUIRE(glHandle.nativeHandle == 42u);
  REQUIRE(glHandle.width == 0u);
  REQUIRE(glHandle.height == 0u);
  REQUIRE(glHandle.generation == 0u);
  REQUIRE(glHandle.needsYFlip);

  void *fakeDescriptorSet = reinterpret_cast<void *>(0x1234);
  const RenderTargetHandle vkHandle =
      RenderTargetHandle::VulkanDescriptorSet(fakeDescriptorSet, false);
  REQUIRE(vkHandle.IsValid());
  REQUIRE(vkHandle.backendId == RenderBackendId::Vulkan);
  REQUIRE(vkHandle.nativeType == RenderNativeHandleType::VulkanImGuiDescriptorSet);
  REQUIRE(vkHandle.nativeHandle == static_cast<uint64_t>(0x1234));
  REQUIRE(vkHandle.width == 0u);
  REQUIRE(vkHandle.height == 0u);
  REQUIRE(vkHandle.generation == 0u);
  REQUIRE_FALSE(vkHandle.needsYFlip);
}

#if defined(MONOLITH_HAS_VULKAN)
TEST_CASE("Vulkan material translation snapshots backend-relevant material state",
          "[renderer][foundation][vulkan][translation]")
{
  Material material;
  material.color = {0.2f, 0.4f, 0.6f, 1.0f};
  material.roughness = 0.25f;
  material.metallic = 0.75f;
  material.uvScale = 2.5f;
  material.albedoMap = std::make_shared<Texture>();
  material.shader = std::make_shared<Shader>();

  const VulkanRenderBackend::TranslatedMaterialState translated =
      VulkanRenderBackend::TranslateMaterialState(material);

  REQUIRE(translated.baseColor == material.color);
  REQUIRE(translated.roughness == Catch::Approx(material.roughness));
  REQUIRE(translated.metallic == Catch::Approx(material.metallic));
  REQUIRE(translated.uvScale == Catch::Approx(material.uvScale));
  REQUIRE_FALSE(translated.usesAlbedoMap);
  REQUIRE_FALSE(translated.usesCustomShader);
}

TEST_CASE("Vulkan opaque pipeline keys track translated material feature usage",
          "[renderer][foundation][vulkan][pipeline]")
{
  VulkanRenderBackend::TranslatedMaterialState materialState;
  materialState.usesAlbedoMap = true;
  materialState.usesCustomShader = false;

  const VulkanRenderBackend::OpaquePipelineKey key =
      VulkanRenderBackend::BuildOpaquePipelineKey(materialState);

  REQUIRE(key.usesAlbedoMap);
  REQUIRE_FALSE(key.usesCustomShader);
  REQUIRE(key.writesDepth);
  REQUIRE(key.depthTestEnabled);
}

TEST_CASE("Vulkan backend exposes opaque raster scaffold once initialized with a window",
          "[renderer][foundation][vulkan][scaffold]")
{
  if (!glfwInit())
    SKIP("GLFW initialization failed on this machine");

  if (!glfwVulkanSupported())
  {
    glfwTerminate();
    SKIP("GLFW reports Vulkan unsupported on this machine");
  }

  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
  GLFWwindow *window = glfwCreateWindow(64, 64, "vulkan-scaffold-test", nullptr, nullptr);
  if (!window)
  {
    glfwTerminate();
    SKIP("Unable to create hidden GLFW window for Vulkan raster scaffold test");
  }

  VulkanRenderBackend backend(window);
  REQUIRE(backend.IsInitialized());
  int framebufferWidth = 0;
  int framebufferHeight = 0;
  glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);
  framebufferWidth = std::max(framebufferWidth, 1);
  framebufferHeight = std::max(framebufferHeight, 1);
  REQUIRE(backend.HasOpaqueRasterScaffold());
  REQUIRE(backend.HasOpaquePipelineCreationScaffold());
  REQUIRE(backend.HasOpaqueShaderPipelineScaffold());
  REQUIRE(backend.HasOpaqueGraphicsPipelineScaffold());
  if (!backend.HasOpaqueDrawExecutionReady())
  {
    REQUIRE(backend.GetLastError().find("shader modules") != std::string::npos);
  }

  void *instanceHandle = nullptr;
  void *physicalDeviceHandle = nullptr;
  void *deviceHandle = nullptr;
  void *queueHandle = nullptr;
  void *renderPassHandle = nullptr;
  uint32_t queueFamily = 0;
  uint32_t imageCount = 0;
  REQUIRE(backend.TryGetImGuiVulkanInitData(&instanceHandle,
                                            &physicalDeviceHandle,
                                            &deviceHandle,
                                            &queueFamily,
                                            &queueHandle,
                                            &renderPassHandle,
                                            &imageCount));
  REQUIRE(instanceHandle != nullptr);
  REQUIRE(physicalDeviceHandle != nullptr);
  REQUIRE(deviceHandle != nullptr);
  REQUIRE(queueHandle != nullptr);
  REQUIRE(renderPassHandle != nullptr);
  REQUIRE(imageCount >= 2);

  glfwDestroyWindow(window);
  glfwTerminate();
}

TEST_CASE("Vulkan backend accepts opaque-scene submissions when initialized with a window handle",
          "[renderer][foundation][vulkan][opaque]")
{
  if (!glfwInit())
    SKIP("GLFW initialization failed on this machine");

  if (!glfwVulkanSupported())
  {
    glfwTerminate();
    SKIP("GLFW reports Vulkan unsupported on this machine");
  }

  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
  GLFWwindow *window = glfwCreateWindow(64, 64, "vulkan-test", nullptr, nullptr);
  if (!window)
  {
    glfwTerminate();
    SKIP("Unable to create hidden GLFW window for Vulkan backend test");
  }

  RenderBackendSelection selection;
  selection.requested = RenderBackendId::Vulkan;
  selection.nativeWindowHandle = window;
  const RenderBackendInitResult init = Renderer::InitializeBackend(selection);

  if (!init.ok)
  {
    glfwDestroyWindow(window);
    glfwTerminate();
    FAIL(init.error);
  }

  Mesh mesh;
  Material material;
  Camera camera;

  Renderer::BeginFrame({{}, "vulkan-opaque-scene"});
  Renderer::BeginPass({RenderPassId::OpaqueScene, BuildRenderView(camera), "vulkan-opaque-pass"});
  Renderer::Submit(mesh, Mat4::Identity(), material);
  Renderer::EndPass();
  Renderer::EndFrame();

  REQUIRE(Renderer::GetBackendId() == RenderBackendId::Vulkan);
  REQUIRE(Renderer::GetDrawCallCount() == 1);

  Renderer::ResetBackend();
  glfwDestroyWindow(window);
  glfwTerminate();
}

TEST_CASE("Vulkan backend executes opaque indexed draws when shader pipeline is ready",
          "[renderer][foundation][vulkan][opaque][draw-indexed]")
{
  if (!glfwInit())
    SKIP("GLFW initialization failed on this machine");

  if (!glfwVulkanSupported())
  {
    glfwTerminate();
    SKIP("GLFW reports Vulkan unsupported on this machine");
  }

  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
  GLFWwindow *window = glfwCreateWindow(64, 64, "vulkan-diagnostics-test", nullptr, nullptr);
  if (!window)
  {
    glfwTerminate();
    SKIP("Unable to create hidden GLFW window for Vulkan diagnostics test");
  }

  VulkanRenderBackend backend(window);
  REQUIRE(backend.IsInitialized());

  Mesh mesh = Mesh::CreateQuad();
  Material material;
  Camera camera;

  backend.BeginFrame({{}, "vulkan-opaque-diagnostics"});
  backend.BeginPass({RenderPassId::OpaqueScene, BuildRenderView(camera), "vulkan-opaque-pass"});
  backend.DrawMesh({&mesh, &material, Mat4::Identity()});
  backend.EndPass();
  backend.EndFrame();

  REQUIRE(backend.GetDrawCallCount() == 1);
  if (!backend.HasOpaqueDrawExecutionReady())
  {
    REQUIRE(backend.GetLastError().find("shader modules") != std::string::npos);
    REQUIRE(backend.GetExecutedOpaqueIndexedDrawCount() == 0);
  }
  else
  {
    REQUIRE(backend.GetExecutedOpaqueIndexedDrawCount() >= 1);
    REQUIRE(backend.GetLastError().find("draw submissions are queued") == std::string::npos);
  }

  glfwDestroyWindow(window);
  glfwTerminate();
}

TEST_CASE("Vulkan backend executes queued overlay callbacks while recording frame commands",
          "[renderer][foundation][vulkan][overlay]")
{
  if (!glfwInit())
    SKIP("GLFW initialization failed on this machine");

  if (!glfwVulkanSupported())
  {
    glfwTerminate();
    SKIP("GLFW reports Vulkan unsupported on this machine");
  }

  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
  GLFWwindow *window = glfwCreateWindow(64, 64, "vulkan-overlay-test", nullptr, nullptr);
  if (!window)
  {
    glfwTerminate();
    SKIP("Unable to create hidden GLFW window for Vulkan overlay callback test");
  }

  VulkanRenderBackend backend(window);
  REQUIRE(backend.IsInitialized());

  OverlayRenderProbe overlayProbe;

  // Frame-external queueing should be ignored by the backend.
  backend.QueueOverlayRenderCallback(&CaptureOverlayRenderProbe, &overlayProbe);
  REQUIRE_FALSE(overlayProbe.invoked);
  REQUIRE(backend.GetLastError().find("active frame") != std::string::npos);

  backend.BeginFrame({{}, "vulkan-overlay-frame"});
  backend.QueueOverlayRenderCallback(&CaptureOverlayRenderProbe, &overlayProbe);
  backend.EndFrame();

  REQUIRE(overlayProbe.invoked);
  REQUIRE(overlayProbe.commandBufferHandle != nullptr);

  glfwDestroyWindow(window);
  glfwTerminate();
}

TEST_CASE("Vulkan backend manages offscreen render-target lifecycle and resize metadata",
          "[renderer][foundation][vulkan][offscreen]")
{
  if (!glfwInit())
    SKIP("GLFW initialization failed on this machine");

  if (!glfwVulkanSupported())
  {
    glfwTerminate();
    SKIP("GLFW reports Vulkan unsupported on this machine");
  }

  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
  GLFWwindow *window = glfwCreateWindow(64, 64, "vulkan-offscreen-test", nullptr, nullptr);
  if (!window)
  {
    glfwTerminate();
    SKIP("Unable to create hidden GLFW window for Vulkan offscreen target test");
  }

  VulkanRenderBackend backend(window);
  REQUIRE(backend.IsInitialized());
  const RenderBackendCapabilities caps = backend.GetCapabilities();
  REQUIRE(caps.supportsOffscreenTargets);
  REQUIRE(caps.supportsNativeTextureHandles);

  const std::string targetKey = "editor-thumb:mesh-a";
  REQUIRE_FALSE(backend.EnsureOffscreenRenderTarget(targetKey, 0, 128));
  REQUIRE(backend.GetLastError().find("non-zero") != std::string::npos);

  REQUIRE(backend.EnsureOffscreenRenderTarget(targetKey, 128, 128));
  VulkanRenderBackend::OffscreenTargetMetadata metadata{};
  REQUIRE(backend.TryGetOffscreenRenderTargetMetadata(targetKey, &metadata));
  REQUIRE(metadata.width == 128u);
  REQUIRE(metadata.height == 128u);
  REQUIRE(metadata.generation == 1u);
  REQUIRE(metadata.readyForSampling);
  REQUIRE_FALSE(metadata.hasImGuiDescriptor);

  RenderTargetHandle initialHandle{};
  const bool initialHandleOk = backend.TryGetOffscreenRenderTargetHandle(targetKey, &initialHandle, false);
  if (initialHandleOk)
  {
    REQUIRE(initialHandle.IsValid());
    REQUIRE(initialHandle.backendId == RenderBackendId::Vulkan);
    REQUIRE(initialHandle.nativeType == RenderNativeHandleType::VulkanImGuiDescriptorSet);
    REQUIRE(initialHandle.width == 128u);
    REQUIRE(initialHandle.height == 128u);
    REQUIRE(initialHandle.generation == metadata.generation);
    REQUIRE_FALSE(initialHandle.needsYFlip);

    RenderTargetHandle flippedHandle{};
    REQUIRE(backend.TryGetOffscreenRenderTargetHandle(targetKey, &flippedHandle, true));
    REQUIRE(flippedHandle.IsValid());
    REQUIRE(flippedHandle.nativeHandle == initialHandle.nativeHandle);
    REQUIRE(flippedHandle.width == initialHandle.width);
    REQUIRE(flippedHandle.height == initialHandle.height);
    REQUIRE(flippedHandle.generation == initialHandle.generation);
    REQUIRE(flippedHandle.needsYFlip);
  }
  else
  {
    REQUIRE(backend.GetLastError().find("ImGui Vulkan texture registration is not ready") != std::string::npos);
  }

  REQUIRE(backend.TryGetOffscreenRenderTargetMetadata(targetKey, &metadata));
  REQUIRE(metadata.hasImGuiDescriptor == initialHandleOk);

  REQUIRE(backend.EnsureOffscreenRenderTarget(targetKey, 256, 128));
  VulkanRenderBackend::OffscreenTargetMetadata resizedMetadata{};
  REQUIRE(backend.TryGetOffscreenRenderTargetMetadata(targetKey, &resizedMetadata));
  REQUIRE(resizedMetadata.width == 256u);
  REQUIRE(resizedMetadata.height == 128u);
  REQUIRE(resizedMetadata.generation == metadata.generation + 1u);
  REQUIRE(resizedMetadata.readyForSampling);
  RenderTargetHandle resizedHandle{};
  const bool resizedHandleOk = backend.TryGetOffscreenRenderTargetHandle(targetKey, &resizedHandle, false);
  if (resizedHandleOk)
  {
    REQUIRE(resizedHandle.IsValid());
    REQUIRE(resizedHandle.width == resizedMetadata.width);
    REQUIRE(resizedHandle.height == resizedMetadata.height);
    REQUIRE(resizedHandle.generation == resizedMetadata.generation);
  }
  else
  {
    REQUIRE(backend.GetLastError().find("ImGui Vulkan texture registration is not ready") != std::string::npos);
  }
  REQUIRE(resizedMetadata.hasImGuiDescriptor == resizedHandleOk);

  backend.DestroyOffscreenRenderTarget(targetKey);
  REQUIRE_FALSE(backend.TryGetOffscreenRenderTargetMetadata(targetKey, &resizedMetadata));
  REQUIRE_FALSE(backend.TryGetOffscreenRenderTargetHandle(targetKey, &resizedHandle, false));
  REQUIRE(backend.GetLastError().find("unknown target key") != std::string::npos);

  glfwDestroyWindow(window);
  glfwTerminate();
}

TEST_CASE("Vulkan editor viewport target stays stable across frame recording and resize",
          "[renderer][foundation][vulkan][viewport]")
{
  if (!glfwInit())
    SKIP("GLFW initialization failed on this machine");

  if (!glfwVulkanSupported())
  {
    glfwTerminate();
    SKIP("GLFW reports Vulkan unsupported on this machine");
  }

  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
  GLFWwindow *window = glfwCreateWindow(96, 96, "vulkan-editor-viewport-test", nullptr, nullptr);
  if (!window)
  {
    glfwTerminate();
    SKIP("Unable to create hidden GLFW window for Vulkan editor viewport test");
  }

  VulkanRenderBackend backend(window);
  REQUIRE(backend.IsInitialized());

  std::string error;
  RenderTargetHandle missingViewportHandle{};
  REQUIRE_FALSE(backend.TryGetEditorViewportRenderTargetHandle(&missingViewportHandle, false, &error));
  REQUIRE(error.find("unknown target key") != std::string::npos);
  REQUIRE_FALSE(missingViewportHandle.IsValid());

  error.clear();
  REQUIRE_FALSE(backend.EnsureEditorViewportRenderTarget(0u, 64u, &error));
  REQUIRE(error.find("non-zero") != std::string::npos);

  error.clear();
  REQUIRE(backend.EnsureEditorViewportRenderTarget(96u, 64u, &error));

  VulkanRenderBackend::OffscreenTargetMetadata initialMetadata{};
  REQUIRE(backend.TryGetOffscreenRenderTargetMetadata("__editor.viewport.scene", &initialMetadata));
  REQUIRE(initialMetadata.width == 96u);
  REQUIRE(initialMetadata.height == 64u);
  REQUIRE(initialMetadata.readyForSampling);

  RenderTargetHandle initialViewportHandle{};
  const bool initialViewportHandleOk =
      backend.TryGetEditorViewportRenderTargetHandle(&initialViewportHandle, false, &error);
  if (initialViewportHandleOk)
  {
    REQUIRE(initialViewportHandle.IsValid());
    REQUIRE(initialViewportHandle.width == initialMetadata.width);
    REQUIRE(initialViewportHandle.height == initialMetadata.height);
    REQUIRE(initialViewportHandle.generation == initialMetadata.generation);
    REQUIRE_FALSE(initialViewportHandle.needsYFlip);
  }
  else
  {
    REQUIRE(error.find("ImGui Vulkan texture registration is not ready") != std::string::npos);
  }

  backend.BeginFrame({{}, "vulkan-editor-viewport-frame"});
  backend.QueueOverlayRenderCallback(&CaptureOverlayRenderProbe, nullptr);
  backend.EndFrame();
  REQUIRE(backend.GetLastError().empty());

  VulkanRenderBackend::OffscreenTargetMetadata afterFrameMetadata{};
  REQUIRE(backend.TryGetOffscreenRenderTargetMetadata("__editor.viewport.scene", &afterFrameMetadata));
  REQUIRE(afterFrameMetadata.width == initialMetadata.width);
  REQUIRE(afterFrameMetadata.height == initialMetadata.height);
  REQUIRE(afterFrameMetadata.readyForSampling);
  REQUIRE(afterFrameMetadata.generation == initialMetadata.generation);

  RenderTargetHandle afterFrameViewportHandle{};
  const bool afterFrameViewportHandleOk =
      backend.TryGetEditorViewportRenderTargetHandle(&afterFrameViewportHandle, false, &error);
  REQUIRE(afterFrameViewportHandleOk == initialViewportHandleOk);
  if (afterFrameViewportHandleOk)
  {
    REQUIRE(afterFrameViewportHandle.IsValid());
    REQUIRE(afterFrameViewportHandle.generation == initialViewportHandle.generation);
  }

  REQUIRE(backend.EnsureEditorViewportRenderTarget(144u, 72u, &error));
  VulkanRenderBackend::OffscreenTargetMetadata resizedMetadata{};
  REQUIRE(backend.TryGetOffscreenRenderTargetMetadata("__editor.viewport.scene", &resizedMetadata));
  REQUIRE(resizedMetadata.width == 144u);
  REQUIRE(resizedMetadata.height == 72u);
  REQUIRE(resizedMetadata.generation == initialMetadata.generation + 1u);
  REQUIRE(resizedMetadata.readyForSampling);

  RenderTargetHandle resizedViewportHandle{};
  const bool resizedViewportHandleOk =
      backend.TryGetEditorViewportRenderTargetHandle(&resizedViewportHandle, true, &error);
  REQUIRE(resizedViewportHandleOk == initialViewportHandleOk);
  if (resizedViewportHandleOk)
  {
    REQUIRE(resizedViewportHandle.IsValid());
    REQUIRE(resizedViewportHandle.width == resizedMetadata.width);
    REQUIRE(resizedViewportHandle.height == resizedMetadata.height);
    REQUIRE(resizedViewportHandle.generation == resizedMetadata.generation);
    REQUIRE(resizedViewportHandle.needsYFlip);
  }

  glfwDestroyWindow(window);
  glfwTerminate();
}

TEST_CASE("Vulkan backend reports and executes readback support deterministically",
          "[renderer][foundation][vulkan][readback]")
{
  if (!glfwInit())
    SKIP("GLFW initialization failed on this machine");

  if (!glfwVulkanSupported())
  {
    glfwTerminate();
    SKIP("GLFW reports Vulkan unsupported on this machine");
  }

  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
  GLFWwindow *window = glfwCreateWindow(64, 64, "vulkan-readback-test", nullptr, nullptr);
  if (!window)
  {
    glfwTerminate();
    SKIP("Unable to create hidden GLFW window for Vulkan readback test");
  }

  VulkanRenderBackend backend(window);
  REQUIRE(backend.IsInitialized());
  int framebufferWidth = 0;
  int framebufferHeight = 0;
  glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);
  framebufferWidth = std::max(framebufferWidth, 1);
  framebufferHeight = std::max(framebufferHeight, 1);

  const RenderBackendCapabilities caps = backend.GetCapabilities();
  std::vector<uint8_t> colorReadback;
  std::vector<float> depthReadback;
  std::string readbackError;

  if (caps.supportsReadback)
  {
    REQUIRE_FALSE(backend.ReadbackColorBgr8(framebufferWidth, framebufferHeight, colorReadback, &readbackError));
    REQUIRE(readbackError.find("no captured frame yet") != std::string::npos);
  }
  else
  {
    REQUIRE_FALSE(backend.ReadbackColorBgr8(framebufferWidth, framebufferHeight, colorReadback, &readbackError));
    REQUIRE(readbackError.find("unavailable") != std::string::npos);
  }

  if (caps.supportsDepthReadback)
  {
    REQUIRE_FALSE(backend.ReadbackDepth32F(framebufferWidth, framebufferHeight, depthReadback, &readbackError));
    REQUIRE(readbackError.find("no captured frame yet") != std::string::npos);
  }
  else
  {
    REQUIRE_FALSE(backend.ReadbackDepth32F(framebufferWidth, framebufferHeight, depthReadback, &readbackError));
    REQUIRE(readbackError.find("unavailable") != std::string::npos);
  }

  Camera camera;
  Mesh mesh = Mesh::CreateQuad();
  Material material;
  backend.BeginFrame({{}, "vulkan-readback-frame"});
  backend.BeginPass({RenderPassId::OpaqueScene, BuildRenderView(camera), "vulkan-readback-pass"});
  backend.DrawMesh({&mesh, &material, Mat4::Identity()});
  backend.EndPass();
  backend.EndFrame();

  if (caps.supportsReadback)
  {
    REQUIRE_FALSE(backend.ReadbackColorBgr8(framebufferWidth + 1, framebufferHeight, colorReadback, &readbackError));
    REQUIRE(readbackError.find("must match swapchain extent") != std::string::npos);
    REQUIRE_FALSE(backend.ReadbackColorBgr8(0, framebufferHeight, colorReadback, &readbackError));
    REQUIRE(readbackError.find("positive dimensions") != std::string::npos);

    backend.BeginFrame({{}, "vulkan-readback-active-frame"});
    REQUIRE_FALSE(backend.ReadbackColorBgr8(framebufferWidth, framebufferHeight, colorReadback, &readbackError));
    REQUIRE(readbackError.find("outside active BeginFrame/EndFrame scope") != std::string::npos);
    backend.EndFrame();

    const bool colorOk =
        backend.ReadbackColorBgr8(framebufferWidth, framebufferHeight, colorReadback, &readbackError);
    if (colorOk)
      REQUIRE(colorReadback.size() == static_cast<size_t>(framebufferWidth * framebufferHeight * 3));
    else
      REQUIRE_FALSE(readbackError.empty());
  }
  else
  {
    REQUIRE_FALSE(backend.ReadbackColorBgr8(64, 64, colorReadback, &readbackError));
    REQUIRE_FALSE(readbackError.empty());
  }

  if (caps.supportsDepthReadback)
  {
    REQUIRE_FALSE(backend.ReadbackDepth32F(framebufferWidth, framebufferHeight + 1, depthReadback, &readbackError));
    REQUIRE(readbackError.find("must match swapchain extent") != std::string::npos);
    REQUIRE_FALSE(backend.ReadbackDepth32F(framebufferWidth, 0, depthReadback, &readbackError));
    REQUIRE(readbackError.find("positive dimensions") != std::string::npos);

    backend.BeginFrame({{}, "vulkan-depth-readback-active-frame"});
    REQUIRE_FALSE(backend.ReadbackDepth32F(framebufferWidth, framebufferHeight, depthReadback, &readbackError));
    REQUIRE(readbackError.find("outside active BeginFrame/EndFrame scope") != std::string::npos);
    backend.EndFrame();

    const bool depthOk =
        backend.ReadbackDepth32F(framebufferWidth, framebufferHeight, depthReadback, &readbackError);
    if (depthOk)
      REQUIRE(depthReadback.size() == static_cast<size_t>(framebufferWidth * framebufferHeight));
    else
      REQUIRE_FALSE(readbackError.empty());
  }
  else
  {
    REQUIRE_FALSE(backend.ReadbackDepth32F(64, 64, depthReadback, &readbackError));
    REQUIRE_FALSE(readbackError.empty());
  }

  glfwDestroyWindow(window);
  glfwTerminate();
}
#endif

TEST_CASE("Renderer supports multiple explicit passes within a single frame",
          "[renderer][foundation]")
{
  FakeRenderBackend backend;
  Renderer::UseBackend(&backend);

  Camera camera;
  camera.position = {4.0f, 5.0f, 6.0f};

  std::vector<Light> lights(3);
  Mesh mesh;
  Material material;
  Shader shader;

  Renderer::BeginFrame(RenderFrameConfig{lights, "multi-pass-frame"});
  Renderer::BeginPass(
      RenderPassConfig{RenderPassId::OpaqueScene, BuildRenderView(camera), "opaque"});
  Renderer::Submit(mesh, Mat4::Identity(), material);
  Renderer::EndPass();
  Renderer::BeginPass(
      RenderPassConfig{RenderPassId::WireframeOverlay, BuildRenderView(camera), "wireframe"});
  Renderer::SubmitWireframe(mesh, Mat4::Identity(), shader, 0.3f, 0.7f, 0.2f);
  Renderer::EndPass();
  Renderer::EndFrame();

  REQUIRE(backend.events ==
          std::vector<std::string>{"begin-frame",
                                   "begin-pass",
                                   "draw-mesh",
                                   "end-pass",
                                   "begin-pass",
                                   "draw-wireframe",
                                   "end-pass",
                                   "end-frame"});
  REQUIRE(backend.lastFrame.lights.size() == 3);
  REQUIRE(backend.lastPass.id == RenderPassId::WireframeOverlay);
  REQUIRE(backend.lastPass.view.cameraPosition.x == Catch::Approx(4.0f));
  REQUIRE(Renderer::GetDrawCallCount() == 2);

  Renderer::ResetBackend();
}

TEST_CASE("RenderSystem submits visible mesh entities into the active explicit pass",
          "[renderer][foundation][scene]")
{
  FakeRenderBackend backend;
  Renderer::UseBackend(&backend);

  Registry registry;
  const Entity entity = registry.Create();

  TransformComponent transform;
  transform.current.position = {1.0f, 2.0f, 3.0f};
  transform.previous.position = transform.current.position;
  registry.Add<TransformComponent>(entity, transform);

  auto mesh = std::make_shared<Mesh>();
  auto material = std::make_shared<Material>();
  MeshComponent meshComponent;
  meshComponent.mesh = mesh;
  meshComponent.material = material;
  meshComponent.visible = true;
  registry.Add<MeshComponent>(entity, meshComponent);

  Camera camera;
  float alpha = 1.0f;
  RenderSystem system(camera, alpha);

  Renderer::BeginFrame({{}, "scene-frame"});
  Renderer::BeginPass({RenderPassId::OpaqueScene, BuildRenderView(camera), "scene-pass"});
  system.OnUpdate(registry, 0.0f);
  Renderer::EndPass();
  Renderer::EndFrame();

  REQUIRE(backend.events ==
          std::vector<std::string>{"begin-frame", "begin-pass", "draw-mesh", "end-pass", "end-frame"});
  REQUIRE(backend.lastMesh.mesh == mesh.get());
  REQUIRE(backend.lastMesh.material == material.get());

  Renderer::ResetBackend();
}

TEST_CASE("Material copies share shader and texture resource handles",
          "[renderer][foundation][ownership]")
{
  auto shader = std::make_shared<Shader>();
  auto texture = std::make_shared<Texture>();

  Material original;
  original.shader = shader;
  original.albedoMap = texture;

  Material copy = original;

  REQUIRE(copy.shader == shader);
  REQUIRE(copy.albedoMap == texture);
  REQUIRE_FALSE(copy.HasShader());
}
