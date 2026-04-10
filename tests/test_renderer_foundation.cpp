#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <memory>
#include <string>
#include <vector>

#include "math/Mat4.h"
#include "renderer/IRenderBackend.h"
#include "renderer/Material.h"
#include "renderer/Mesh.h"
#include "renderer/RenderBackend.h"
#include "renderer/Renderer.h"
#include "renderer/RenderViewUtils.h"
#include "scene/Registry.h"
#include "scene/components/MeshComponent.h"
#include "scene/components/TransformComponent.h"
#include "scene/systems/RenderSystem.h"

using namespace Monolith;

namespace {

class FakeRenderBackend : public IRenderBackend {
 public:
  void BeginFrame(const RenderFrameConfig& frame) override {
    events.push_back("begin-frame");
    lastFrame = frame;
  }

  void EndFrame() override {
    events.push_back("end-frame");
  }

  void BeginPass(const RenderPassConfig& pass) override {
    events.push_back("begin-pass");
    lastPass = pass;
  }

  void EndPass() override {
    events.push_back("end-pass");
  }

  void DrawMesh(const MeshDrawCommand& command) override {
    events.push_back("draw-mesh");
    lastMesh = command;
    ++drawCalls;
  }

  void DrawSkinnedMesh(const SkinnedMeshDrawCommand& command) override {
    events.push_back("draw-skinned");
    lastSkinned = command;
    ++drawCalls;
  }

  void DrawWireframe(const WireframeDrawCommand& command) override {
    events.push_back("draw-wireframe");
    lastWireframe = command;
    ++drawCalls;
  }

  RenderBackendId GetBackendId() const override { return RenderBackendId::OpenGL; }
  RenderBackendCapabilities GetCapabilities() const override {
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

}  // namespace

TEST_CASE("Renderer routes explicit frame and pass commands through backend seam",
          "[renderer][foundation]") {
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

TEST_CASE("Renderer initializes the default OpenGL backend through a typed selection",
          "[renderer][foundation][backend]") {
  const RenderBackendInitResult init = Renderer::InitializeBackend({RenderBackendId::Auto});

  REQUIRE(init.ok);
  REQUIRE(init.requested == RenderBackendId::Auto);
  REQUIRE(init.selected == RenderBackendId::OpenGL);
  REQUIRE(Renderer::GetBackendId() == RenderBackendId::OpenGL);

  const RenderBackendCapabilities caps = Renderer::GetBackendCapabilities();
  REQUIRE(caps.supportsWireframeOverlay);
  REQUIRE_FALSE(caps.supportsComputePasses);
}

TEST_CASE("Renderer rejects unsupported backend requests without replacing the active backend",
          "[renderer][foundation][backend]") {
  REQUIRE(Renderer::InitializeBackend({RenderBackendId::OpenGL}).ok);

  const RenderBackendInitResult init = Renderer::InitializeBackend({RenderBackendId::Vulkan});

  REQUIRE_FALSE(init.ok);
  REQUIRE(init.selected == RenderBackendId::Vulkan);
  REQUIRE_FALSE(init.error.empty());
  REQUIRE(Renderer::GetBackendId() == RenderBackendId::OpenGL);
  REQUIRE_FALSE(Renderer::IsBackendSupported(RenderBackendId::Vulkan));
}

TEST_CASE("Renderer supports multiple explicit passes within a single frame",
          "[renderer][foundation]") {
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
          "[renderer][foundation][scene]") {
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
          "[renderer][foundation][ownership]") {
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
