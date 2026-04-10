#pragma once
#include <string>
#include <vector>

#include "math/Mat4.h"
#include "renderer/RenderBackend.h"
#include "renderer/IRenderBackend.h"
#include "renderer/RenderTypes.h"

namespace Monolith {

class Mesh;
class Shader;
class Material;
class SkinnedMesh;

class Renderer {
 public:
  static RenderBackendInitResult InitializeBackend(const RenderBackendSelection& selection = {});
  static RenderBackendId GetBackendId();
  static RenderBackendCapabilities GetBackendCapabilities();
  static bool IsBackendSupported(RenderBackendId backendId);

  // Test seam: temporarily override the active backend with an externally owned implementation.
  static void UseBackend(IRenderBackend* backend);
  static void ResetBackend();

  static void BeginFrame(const RenderFrameConfig& frame);
  static void EndFrame();
  static void BeginPass(const RenderPassConfig& pass);
  static void EndPass();
  static bool IsFrameActive();
  static bool IsPassActive();

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

  static int GetDrawCallCount();

 private:
  static IRenderBackend* ActiveBackend();

  static bool s_frameActive;
  static bool s_passActive;
};

}  // namespace Monolith
