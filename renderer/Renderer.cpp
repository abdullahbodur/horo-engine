#include "renderer/Renderer.h"

#include "core/Assert.h"
#include "renderer/OpenGLRenderBackend.h"

namespace Monolith {

namespace {

OpenGLRenderBackend g_defaultBackend;
IRenderBackend* g_backend = &g_defaultBackend;

}  // namespace

bool Renderer::s_frameActive = false;
bool Renderer::s_passActive = false;

IRenderBackend* Renderer::ActiveBackend() {
  return g_backend ? g_backend : &g_defaultBackend;
}

void Renderer::UseBackend(IRenderBackend* backend) {
  MONOLITH_ASSERT(!s_frameActive && !s_passActive,
                  "Cannot swap render backend while a frame or pass is active");
  g_backend = backend ? backend : &g_defaultBackend;
}

void Renderer::ResetBackend() {
  UseBackend(&g_defaultBackend);
}

void Renderer::BeginFrame(const RenderFrameConfig& frame) {
  MONOLITH_ASSERT(!s_frameActive, "Renderer::BeginFrame called while a frame is already active");

  ActiveBackend()->BeginFrame(frame);
  s_frameActive = true;
}

void Renderer::EndFrame() {
  MONOLITH_ASSERT(s_frameActive, "Renderer::EndFrame called without an active frame");
  if (!s_frameActive)
    return;

  if (s_passActive)
    EndPass();

  ActiveBackend()->EndFrame();
  s_frameActive = false;
}

void Renderer::BeginPass(const RenderPassConfig& pass) {
  MONOLITH_ASSERT(s_frameActive, "Renderer::BeginPass called without an active frame");
  MONOLITH_ASSERT(!s_passActive, "Renderer::BeginPass called while another pass is still active");
  if (!s_frameActive || s_passActive)
    return;

  ActiveBackend()->BeginPass(pass);
  s_passActive = true;
}

void Renderer::EndPass() {
  MONOLITH_ASSERT(s_passActive, "Renderer::EndPass called without an active pass");
  if (!s_passActive)
    return;

  ActiveBackend()->EndPass();
  s_passActive = false;
}

bool Renderer::IsFrameActive() {
  return s_frameActive;
}

bool Renderer::IsPassActive() {
  return s_passActive;
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
