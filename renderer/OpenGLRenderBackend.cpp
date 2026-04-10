#include "renderer/OpenGLRenderBackend.h"

#include <algorithm>
#include <string>

#include <glad/glad.h>

#include "core/Assert.h"
#include "renderer/Material.h"
#include "renderer/Mesh.h"
#include "renderer/Shader.h"
#include "renderer/SkinnedMesh.h"

namespace Monolith {

namespace {

constexpr float kWireframeOverlayLineWidth = 1.5f;

static Shader* ResolveMaterialShader(const Material& material) {
  return material.shader && material.shader->IsValid() ? material.shader.get() : nullptr;
}

static void BindMaterial(const Material& material) {
  Shader* shader = ResolveMaterialShader(material);
  if (!shader)
    return;

  shader->Bind();
  shader->SetVec4("u_color", material.color);
  shader->SetFloat("u_roughness", material.roughness);
  shader->SetFloat("u_metallic", material.metallic);
  shader->SetInt("u_albedoMap", 0);
  shader->SetFloat("u_uvScale", material.uvScale);

  const bool hasTexture = material.albedoMap && material.albedoMap->IsValid();
  if (hasTexture)
    material.albedoMap->Bind(0);
  shader->SetInt("u_hasTexture", hasTexture ? 1 : 0);
}

}  // namespace

void OpenGLRenderBackend::BeginFrame(const RenderFrameConfig& frame) {
  MONOLITH_ASSERT(!m_frameActive, "OpenGLRenderBackend::BeginFrame called while a frame is active");
  m_lights = frame.lights;
  if (m_lights.size() > 8)
    m_lights.resize(8);
  m_drawCalls = 0;
  m_lastLightProgram = 0;
  m_frameActive = true;
}

void OpenGLRenderBackend::EndFrame() {
  if (m_passActive)
    EndPass();
  m_frameActive = false;
  m_lastLightProgram = 0;
}

void OpenGLRenderBackend::BeginPass(const RenderPassConfig& pass) {
  MONOLITH_ASSERT(m_frameActive, "OpenGLRenderBackend::BeginPass called without an active frame");
  MONOLITH_ASSERT(!m_passActive, "OpenGLRenderBackend::BeginPass called while a pass is active");
  if (!m_frameActive || m_passActive)
    return;

  m_activeView = pass.view;
  m_activePassId = pass.id;
  m_passActive = true;
  m_lastLightProgram = 0;

  if (pass.id == RenderPassId::WireframeOverlay) {
    m_previousDepthTestEnabled = glIsEnabled(GL_DEPTH_TEST) == GL_TRUE;
    glDisable(GL_DEPTH_TEST);
    glLineWidth(kWireframeOverlayLineWidth);
    m_hasPassStateOverride = true;
  }
}

void OpenGLRenderBackend::EndPass() {
  if (m_hasPassStateOverride && m_activePassId == RenderPassId::WireframeOverlay) {
    if (m_previousDepthTestEnabled)
      glEnable(GL_DEPTH_TEST);
    else
      glDisable(GL_DEPTH_TEST);
    glLineWidth(1.0f);
    m_hasPassStateOverride = false;
  }

  m_passActive = false;
  m_lastLightProgram = 0;
}

void OpenGLRenderBackend::UploadLights(const Shader& shader) {
  const unsigned int programId = shader.GetProgramID();
  if (programId == m_lastLightProgram)
    return;

  const int lightCount = static_cast<int>(m_lights.size());
  shader.SetInt("u_lightCount", lightCount);
  for (int i = 0; i < lightCount; ++i) {
    const std::string base = "u_lights[" + std::to_string(i) + "].";
    shader.SetInt(base + "type", static_cast<int>(m_lights[static_cast<size_t>(i)].type));
    shader.SetVec3(base + "position", m_lights[static_cast<size_t>(i)].position);
    shader.SetVec3(base + "direction", m_lights[static_cast<size_t>(i)].direction);
    shader.SetVec3(base + "color",
                   m_lights[static_cast<size_t>(i)].color *
                       m_lights[static_cast<size_t>(i)].intensity);
    shader.SetFloat(base + "radius", m_lights[static_cast<size_t>(i)].radius);
  }
  m_lastLightProgram = programId;
}

void OpenGLRenderBackend::DrawMesh(const MeshDrawCommand& command) {
  if (!m_passActive || !command.mesh || !command.material)
    return;

  BindMaterial(*command.material);
  Shader* shader = ResolveMaterialShader(*command.material);
  if (!shader)
    return;

  shader->SetMat4("u_model", command.modelMatrix);
  shader->SetMat4("u_view", m_activeView.view);
  shader->SetMat4("u_projection", m_activeView.projection);
  shader->SetVec3("u_cameraPos", m_activeView.cameraPosition);
  UploadLights(*shader);

  command.mesh->Draw();
  ++m_drawCalls;
}

void OpenGLRenderBackend::DrawSkinnedMesh(const SkinnedMeshDrawCommand& command) {
  if (!m_passActive || !command.mesh || !command.material)
    return;

  BindMaterial(*command.material);
  Shader* shader = ResolveMaterialShader(*command.material);
  if (!shader)
    return;

  shader->SetMat4("u_model", command.modelMatrix);
  shader->SetMat4("u_view", m_activeView.view);
  shader->SetMat4("u_projection", m_activeView.projection);
  shader->SetVec3("u_cameraPos", m_activeView.cameraPosition);

  const std::vector<Mat4>* boneMatrices = command.boneMatrices;
  const int boneCount =
      boneMatrices ? std::min(static_cast<int>(boneMatrices->size()), 64) : 0;
  if (boneCount > 0)
    shader->SetMat4Array("u_boneMatrices", boneCount, (*boneMatrices)[0].Data());

  UploadLights(*shader);

  command.mesh->Draw();
  ++m_drawCalls;
}

void OpenGLRenderBackend::DrawWireframe(const WireframeDrawCommand& command) {
  if (!m_passActive || !command.mesh || !command.shader || !command.shader->IsValid())
    return;

  command.shader->Bind();
  command.shader->SetMat4("u_model", command.modelMatrix);
  command.shader->SetMat4("u_view", m_activeView.view);
  command.shader->SetMat4("u_projection", m_activeView.projection);
  command.shader->SetVec4("u_color", command.color);

  command.mesh->DrawWireframe();
  ++m_drawCalls;
}

}  // namespace Monolith
