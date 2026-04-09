#include "renderer/Renderer.h"

#include "renderer/OpenGLRenderBackend.h"

namespace Monolith {

namespace {

OpenGLRenderBackend g_defaultBackend;
IRenderBackend* g_backend = &g_defaultBackend;

}  // namespace

std::vector<Light> Renderer::s_compatLights;
bool Renderer::s_frameActive = false;
bool Renderer::s_passActive = false;
bool Renderer::s_compatibilitySceneActive = false;

IRenderBackend* Renderer::ActiveBackend() {
  return g_backend ? g_backend : &g_defaultBackend;
}

void Renderer::UseBackend(IRenderBackend* backend) {
  g_backend = backend ? backend : &g_defaultBackend;
  s_frameActive = false;
  s_passActive = false;
  s_compatibilitySceneActive = false;
  s_compatLights.clear();
}

void Renderer::ResetBackend() {
  UseBackend(&g_defaultBackend);
}

void Renderer::BeginFrame(const RenderFrameConfig& frame) {
  if (s_frameActive)
    EndFrame();

  ActiveBackend()->BeginFrame(frame);
  s_frameActive = true;
  s_compatibilitySceneActive = false;
}

void Renderer::EndFrame() {
  if (!s_frameActive)
    return;

  if (s_passActive)
    EndPass();

  ActiveBackend()->EndFrame();
  s_frameActive = false;
  s_compatibilitySceneActive = false;
}

void Renderer::BeginPass(const RenderPassConfig& pass) {
  if (!s_frameActive)
    return;

  if (s_passActive)
    EndPass();

  ActiveBackend()->BeginPass(pass);
  s_passActive = true;
}

void Renderer::EndPass() {
  if (!s_passActive)
    return;

  ActiveBackend()->EndPass();
  s_passActive = false;
}

void Renderer::BeginScene(const Camera& camera) {
  if (s_compatibilitySceneActive)
    EndScene();

  BeginFrame(RenderFrameConfig{s_compatLights, "compatibility-scene"});
  BeginPass(RenderPassConfig{
      RenderPassId::CompatibilityScene,
      RenderView::FromCamera(camera),
      "compatibility-scene-pass",
  });
  s_compatibilitySceneActive = true;
}

void Renderer::EndScene() {
  if (!s_compatibilitySceneActive)
    return;

  EndFrame();
}

void Renderer::SetLights(const std::vector<Light>& lights) {
  s_compatLights = lights;
}

void Renderer::Submit(const Mesh& mesh, const Mat4& modelMatrix, Material& material) {
  ActiveBackend()->DrawMesh(MeshDrawCommand{&mesh, &material, modelMatrix});
}

void Renderer::SubmitSkinned(const SkinnedMesh& mesh,
                              const Mat4& modelMatrix,
                              Material& material,
                              const std::vector<Mat4>& boneMatrices) {
  ActiveBackend()->DrawSkinnedMesh(
      SkinnedMeshDrawCommand{&mesh, &material, modelMatrix, &boneMatrices});
}

void Renderer::SubmitWireframe(
    const Mesh& mesh, const Mat4& model, Shader& shader, float r, float g, float b) {
  ActiveBackend()->DrawWireframe(
      WireframeDrawCommand{&mesh, &shader, model, {r, g, b, 1.0f}});
}

int Renderer::GetDrawCallCount() {
  return ActiveBackend()->GetDrawCallCount();
}

}  // namespace Monolith
