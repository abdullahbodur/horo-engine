#include "renderer/Renderer.h"

#include <algorithm>
#include <string>

#include "renderer/Material.h"
#include "renderer/Mesh.h"
#include "renderer/Shader.h"

namespace Horo {

Mat4 Renderer::s_view;
Mat4 Renderer::s_projection;
Vec3 Renderer::s_cameraPos;
std::vector<Light> Renderer::s_lights;
int Renderer::s_drawCalls = 0;
unsigned int Renderer::s_lastLightProgram = 0;

void Renderer::BeginScene(const Camera& camera) {
  s_view = camera.GetView();
  s_projection = camera.GetProjection();
  s_cameraPos = camera.position;
  s_drawCalls = 0;
  s_lastLightProgram = 0;
}

void Renderer::EndScene() {}

void Renderer::SetLights(const std::vector<Light>& lights) {
  s_lights = lights;
  if (s_lights.size() > 8)
    s_lights.resize(8);
}

void Renderer::Submit(const Mesh& mesh, const Mat4& modelMatrix, Material& material) {
  material.Apply();
  if (!material.shader)
    return;

  material.shader->SetMat4("u_model", modelMatrix);
  material.shader->SetMat4("u_view", s_view);
  material.shader->SetMat4("u_projection", s_projection);
  material.shader->SetVec3("u_cameraPos", s_cameraPos);

  unsigned int progID = material.shader->GetProgramID();
  if (progID != s_lastLightProgram) {
    int count = static_cast<int>(s_lights.size());
    material.shader->SetInt("u_lightCount", count);
    for (int i = 0; i < count; ++i) {
      std::string b = "u_lights[" + std::to_string(i) + "].";
      material.shader->SetInt(b + "type", static_cast<int>(s_lights[i].type));
      material.shader->SetVec3(b + "position", s_lights[i].position);
      material.shader->SetVec3(b + "direction", s_lights[i].direction);
      material.shader->SetVec3(b + "color", s_lights[i].color * s_lights[i].intensity);
      material.shader->SetFloat(b + "radius", s_lights[i].radius);
    }
    s_lastLightProgram = progID;
  }

  mesh.Draw();
  ++s_drawCalls;
}

void Renderer::SubmitWireframe(
    const Mesh& mesh, const Mat4& model, Shader& shader, float r, float g, float b) {
  shader.Bind();
  shader.SetMat4("u_model", model);
  shader.SetMat4("u_view", s_view);
  shader.SetMat4("u_projection", s_projection);
  shader.SetVec4("u_color", {r, g, b, 1.0f});

  mesh.DrawWireframe();
  ++s_drawCalls;
}

}  // namespace Horo
