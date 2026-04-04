#pragma once
#include <vector>

#include "math/Mat4.h"
#include "renderer/Camera.h"
#include "renderer/Light.h"

namespace Monolith {

class Mesh;
class Shader;
class Material;
class SkinnedMesh;

class Renderer {
 public:
  static void BeginScene(const Camera& camera);
  static void EndScene();

  // Upload lights once per frame (call before Submit calls)
  static void SetLights(const std::vector<Light>& lights);

  // Submit a mesh for rendering with a given model matrix and material
  static void Submit(const Mesh& mesh, const Mat4& modelMatrix, Material& material);

  // Submit a skinned mesh — uploads boneMatrices to u_boneMatrices[0..N] via SetMat4Array.
  static void SubmitSkinned(const SkinnedMesh& mesh,
                             const Mat4& modelMatrix,
                             Material& material,
                             const std::vector<Mat4>& boneMatrices);

  // Submit a mesh in wireframe mode using a plain color
  static void SubmitWireframe(const Mesh& mesh,
                              const Mat4& modelMatrix,
                              Shader& shader,
                              float r = 0.2f,
                              float g = 0.8f,
                              float b = 0.2f);

  static int GetDrawCallCount() { return s_drawCalls; }

 private:
  static Mat4 s_view;
  static Mat4 s_projection;
  static Vec3 s_cameraPos;
  static std::vector<Light> s_lights;
  static int s_drawCalls;
  static unsigned int s_lastLightProgram;
};

}  // namespace Monolith
