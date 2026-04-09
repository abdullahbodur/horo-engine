#include "renderer/OpenGLRenderBackend.h"

#include <algorithm>
#include <string>

#include "renderer/Material.h"
#include "renderer/Mesh.h"
#include "renderer/Shader.h"
#include "renderer/SkinnedMesh.h"

namespace Monolith {

void OpenGLRenderBackend::BeginFrame(const RenderFrameConfig& frame) {
  m_lights = frame.lights;
  if (m_lights.size() > 8)
    m_lights.resize(8);
  m_drawCalls = 0;
  m_lastLightProgram = 0;
  m_frameActive = true;
}

void OpenGLRenderBackend::EndFrame() {
  m_passActive = false;
  m_frameActive = false;
  m_lastLightProgram = 0;
}

void OpenGLRenderBackend::BeginPass(const RenderPassConfig& pass) {
  if (!m_frameActive)
    return;

  m_activeView = pass.view;
  m_passActive = true;
  m_lastLightProgram = 0;
}

void OpenGLRenderBackend::EndPass() {
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

  command.material->Apply();
  if (!command.material->shader || !command.material->shader->IsValid())
    return;

  Shader& shader = *command.material->shader;
  shader.SetMat4("u_model", command.modelMatrix);
  shader.SetMat4("u_view", m_activeView.view);
  shader.SetMat4("u_projection", m_activeView.projection);
  shader.SetVec3("u_cameraPos", m_activeView.cameraPosition);
  UploadLights(shader);

  command.mesh->Draw();
  ++m_drawCalls;
}

void OpenGLRenderBackend::DrawSkinnedMesh(const SkinnedMeshDrawCommand& command) {
  if (!m_passActive || !command.mesh || !command.material)
    return;

  command.material->Apply();
  if (!command.material->shader || !command.material->shader->IsValid())
    return;

  Shader& shader = *command.material->shader;
  shader.SetMat4("u_model", command.modelMatrix);
  shader.SetMat4("u_view", m_activeView.view);
  shader.SetMat4("u_projection", m_activeView.projection);
  shader.SetVec3("u_cameraPos", m_activeView.cameraPosition);

  const std::vector<Mat4>* boneMatrices = command.boneMatrices;
  const int boneCount =
      boneMatrices ? std::min(static_cast<int>(boneMatrices->size()), 64) : 0;
  if (boneCount > 0)
    shader.SetMat4Array("u_boneMatrices", boneCount, (*boneMatrices)[0].Data());

  UploadLights(shader);

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
