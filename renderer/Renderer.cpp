#include "renderer/Renderer.h"

#include <memory>

#include "core/Assert.h"
#include "renderer/RenderBackendFactory.h"

namespace Monolith {

namespace {

std::unique_ptr<IRenderBackend> CreateDefaultOwnedBackend() {
  RenderBackendCreateResult createResult = CreateRenderBackend({});
  MONOLITH_ASSERT(createResult.backend != nullptr,
                  "Failed to create the default OpenGL render backend");
  return std::move(createResult.backend);
}

std::unique_ptr<IRenderBackend> g_ownedBackend = CreateDefaultOwnedBackend();
IRenderBackend* g_backend = g_ownedBackend.get();

IRenderBackend* ResetToDefaultBackend() {
  g_ownedBackend = CreateDefaultOwnedBackend();
  g_backend = g_ownedBackend.get();
  return g_backend;
}

}  // namespace

bool Renderer::s_frameActive = false;
bool Renderer::s_passActive = false;

IRenderBackend* Renderer::ActiveBackend() {
  MONOLITH_ASSERT(g_backend != nullptr, "Renderer backend pointer should never be null");
  return g_backend;
}

RenderBackendInitResult Renderer::InitializeBackend(const RenderBackendSelection& selection) {
  MONOLITH_ASSERT(!s_frameActive && !s_passActive,
                  "Cannot initialize render backend while a frame or pass is active");

  RenderBackendInitResult out;
  out.requested = selection.requested;

  RenderBackendCreateResult createResult = CreateRenderBackend(selection);
  out.selected = createResult.selected;
  out.error = std::move(createResult.error);
  if (!createResult.backend)
    return out;

  g_ownedBackend = std::move(createResult.backend);
  g_backend = g_ownedBackend.get();
  out.ok = true;
  out.selected = g_backend->GetBackendId();
  out.capabilities = g_backend->GetCapabilities();
  return out;
}

RenderBackendId Renderer::GetBackendId() {
  return ActiveBackend()->GetBackendId();
}

RenderBackendCapabilities Renderer::GetBackendCapabilities() {
  return ActiveBackend()->GetCapabilities();
}

IRenderBackend* Renderer::GetBackendForInterop() {
  return ActiveBackend();
}

bool Renderer::IsBackendSupported(RenderBackendId backendId) {
  return Monolith::IsRenderBackendSupported(backendId);
}

void Renderer::UseBackend(IRenderBackend* backend) {
  MONOLITH_ASSERT(!s_frameActive && !s_passActive,
                  "Cannot swap render backend while a frame or pass is active");
  g_ownedBackend.reset();
  g_backend = backend ? backend : ResetToDefaultBackend();
}

void Renderer::ResetBackend() {
  MONOLITH_ASSERT(!s_frameActive && !s_passActive,
                  "Cannot reset render backend while a frame or pass is active");
  ResetToDefaultBackend();
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
