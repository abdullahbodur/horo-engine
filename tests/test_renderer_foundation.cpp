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

    std::vector<std::string> events;
    RenderFrameConfig lastFrame;
    RenderPassConfig lastPass;
    MeshDrawCommand lastMesh;
    SkinnedMeshDrawCommand lastSkinned;
    WireframeDrawCommand lastWireframe;
    int drawCalls = 0;
  };

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
  REQUIRE_FALSE(vulkanCaps.supportsOffscreenTargets);
  REQUIRE_FALSE(vulkanCaps.supportsNativeTextureHandles);
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
  REQUIRE_FALSE(vkCaps.supportsOffscreenTargets);
  REQUIRE_FALSE(vkCaps.supportsNativeTextureHandles);
  REQUIRE_FALSE(vkCaps.supportsReadback);
  REQUIRE_FALSE(vkCaps.supportsDepthReadback);
  REQUIRE_FALSE(vkCaps.supportsDebugHud);
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
  REQUIRE(backend.HasOpaqueRasterScaffold());
  REQUIRE(backend.HasOpaquePipelineCreationScaffold());
  REQUIRE(backend.HasOpaqueShaderPipelineScaffold());
  REQUIRE(backend.HasOpaqueGraphicsPipelineScaffold());

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

TEST_CASE("Vulkan backend reports actionable diagnostics for scaffold-only opaque draw execution",
          "[renderer][foundation][vulkan][opaque][diagnostics]")
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

  Mesh mesh;
  Material material;
  Camera camera;

  backend.BeginFrame({{}, "vulkan-opaque-diagnostics"});
  backend.BeginPass({RenderPassId::OpaqueScene, BuildRenderView(camera), "vulkan-opaque-pass"});
  backend.DrawMesh({&mesh, &material, Mat4::Identity()});
  backend.EndPass();
  backend.EndFrame();

  REQUIRE(backend.GetDrawCallCount() == 1);
  REQUIRE(backend.GetLastError().find("draw submissions are queued") != std::string::npos);
  REQUIRE(backend.GetLastError().find("Pending draws: 1") != std::string::npos);

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
