#pragma once

#include <cstdint>
#include <vector>

#include "renderer/RenderBackend.h"
#include "renderer/RenderTargetHandle.h"
#include "renderer/RenderTypes.h"
#include "renderer/SceneTextureResources.h"

namespace Monolith {

class IRenderBackend {
 public:
  virtual ~IRenderBackend() = default;

  virtual void BeginFrame(const RenderFrameConfig& frame) = 0;
  virtual void EndFrame() = 0;
  virtual void BeginPass(const RenderPassConfig& pass) = 0;
  virtual void EndPass() = 0;

  virtual void DrawMesh(const MeshDrawCommand& command) = 0;
  virtual void DrawSkinnedMesh(const SkinnedMeshDrawCommand& command) = 0;
  virtual void DrawWireframe(const WireframeDrawCommand& command) = 0;

  virtual RenderBackendId GetBackendId() const = 0;
  virtual RenderBackendCapabilities GetCapabilities() const = 0;
  virtual int GetDrawCallCount() const = 0;
  virtual bool ReadbackColorBgr8(int width,
                                 int height,
                                 std::vector<uint8_t>& outPixels,
                                 std::string* outError) = 0;
  virtual bool ReadbackDepth32F(int width,
                                int height,
                                std::vector<float>& outDepth,
                                std::string* outError) = 0;
  virtual bool EnsureEditorViewportRenderTarget(uint32_t width,
                                                uint32_t height,
                                                std::string* outError) = 0;
  virtual bool TryGetEditorViewportRenderTargetHandle(RenderTargetHandle* outHandle,
                                                      bool needsYFlip,
                                                      std::string* outError) = 0;
  virtual bool EnsureSceneTextureResources(uint32_t width,
                                           uint32_t height,
                                           std::string* outError) = 0;
  virtual bool TryGetSceneTextureCatalog(SceneTextureCatalog* outCatalog,
                                         std::string* outError) const = 0;
  virtual bool EnsureGiHistoryResources(uint32_t width,
                                        uint32_t height,
                                        std::string* outError) = 0;
  virtual bool TryGetGiHistoryCatalog(GiHistoryCatalog* outCatalog,
                                      std::string* outError) const = 0;
  virtual bool TryGetScreenSpaceReflectionPassContract(ScreenSpaceReflectionPassContract* outContract,
                                                       std::string* outError) const = 0;
  virtual bool TryGetScreenSpaceGlobalIlluminationPassContract(
      ScreenSpaceGlobalIlluminationPassContract* outContract, std::string* outError) const = 0;
  virtual bool InvalidateGiHistory(GiHistoryResetReason reason,
                                   std::string* outError) = 0;
};

}  // namespace Monolith
