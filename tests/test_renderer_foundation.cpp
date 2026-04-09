#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <string>
#include <vector>

#include "math/Mat4.h"
#include "renderer/IRenderBackend.h"
#include "renderer/Material.h"
#include "renderer/Mesh.h"
#include "renderer/Renderer.h"

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
  Renderer::BeginPass(RenderPassConfig{RenderPassId::OpaqueScene, RenderView::FromCamera(camera), "main"});
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

TEST_CASE("Renderer compatibility scene API is bridged through backend seam",
          "[renderer][foundation]") {
  FakeRenderBackend backend;
  Renderer::UseBackend(&backend);

  Camera camera;
  camera.position = {4.0f, 5.0f, 6.0f};

  std::vector<Light> lights(3);
  Mesh mesh;
  Material material;

  Renderer::SetLights(lights);
  Renderer::BeginScene(camera);
  Renderer::Submit(mesh, Mat4::Identity(), material);
  Renderer::EndScene();

  REQUIRE(backend.events ==
          std::vector<std::string>{"begin-frame", "begin-pass", "draw-mesh", "end-pass", "end-frame"});
  REQUIRE(backend.lastFrame.lights.size() == 3);
  REQUIRE(backend.lastPass.id == RenderPassId::CompatibilityScene);
  REQUIRE(backend.lastPass.view.cameraPosition.x == Catch::Approx(4.0f));

  Renderer::ResetBackend();
}
