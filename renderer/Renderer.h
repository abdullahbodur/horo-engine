#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "math/Mat4.h"
#include "renderer/RenderBackend.h"
#include "renderer/IRenderBackend.h"
#include "renderer/RenderTargetHandle.h"
#include "renderer/SceneTextureResources.h"
#include "renderer/RenderTypes.h"

namespace Monolith
{

  class Mesh;
  class Shader;
  class Material;
  class SkinnedMesh;

  class Renderer
  {
  public:
    static RenderBackendInitResult InitializeBackend(const RenderBackendSelection &selection = {});
    static RenderBackendId GetBackendId();
    static RenderBackendCapabilities GetBackendCapabilities();
    static bool IsBackendSupported(RenderBackendId backendId);
    static IRenderBackend *GetBackendForInterop();

    // Test seam: temporarily override the active backend with an externally owned implementation.
    static void UseBackend(IRenderBackend *backend);
    static void ResetBackend();

    static void BeginFrame(const RenderFrameConfig &frame);
    static void EndFrame();
    static void BeginPass(const RenderPassConfig &pass);
    static void EndPass();
    static bool IsFrameActive();
    static bool IsPassActive();

    // Submit a mesh for rendering with a given model matrix and material
    static void Submit(const Mesh &mesh, const Mat4 &modelMatrix, Material &material);

    // Submit a skinned mesh — uploads boneMatrices to u_boneMatrices[0..N] via SetMat4Array.
    static void SubmitSkinned(const SkinnedMesh &mesh,
                              const Mat4 &modelMatrix,
                              Material &material,
                              const std::vector<Mat4> &boneMatrices);

    // Submit a mesh in wireframe mode using a plain color
    static void SubmitWireframe(const Mesh &mesh,
                                const Mat4 &modelMatrix,
                                Shader &shader,
                                float r = 0.2f,
                                float g = 0.8f,
                                float b = 0.2f);

    static int GetDrawCallCount();
    static bool ReadbackColorBgr8(int width,
                                  int height,
                                  std::vector<uint8_t> &outPixels,
                                  std::string *outError = nullptr);
    static bool ReadbackDepth32F(int width,
                                 int height,
                                 std::vector<float> &outDepth,
                                 std::string *outError = nullptr);
    static bool EnsureEditorViewportRenderTarget(uint32_t width,
                                                 uint32_t height,
                                                 std::string *outError = nullptr);
    static bool TryGetEditorViewportRenderTargetHandle(RenderTargetHandle *outHandle,
                                                       bool needsYFlip = false,
                                                       std::string *outError = nullptr);
    static bool EnsureSceneTextureResources(uint32_t width,
                                            uint32_t height,
                                            std::string *outError = nullptr);
    static bool TryGetSceneTextureCatalog(SceneTextureCatalog *outCatalog,
                                          std::string *outError = nullptr);
    static bool EnsureGiHistoryResources(uint32_t width,
                                         uint32_t height,
                                         std::string *outError = nullptr);
    static bool TryGetGiHistoryCatalog(GiHistoryCatalog *outCatalog,
                                       std::string *outError = nullptr);
    static bool TryGetTemporalReprojectionInputContract(TemporalReprojectionInputContract *outContract,
                                                        std::string *outError = nullptr);
    static bool InvalidateGiHistory(GiHistoryResetReason reason,
                                    std::string *outError = nullptr);

  private:
    static IRenderBackend *ActiveBackend();

    static bool s_frameActive;
    static bool s_passActive;
  };

} // namespace Monolith
