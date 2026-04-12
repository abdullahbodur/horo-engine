#pragma once

#include <cstdint>
#include <vector>

#include "renderer/IRenderBackend.h"

namespace Monolith {

class OpenGLRenderBackend : public IRenderBackend {
 public:
  void BeginFrame(const RenderFrameConfig& frame) override;
  void EndFrame() override;
  void BeginPass(const RenderPassConfig& pass) override;
  void EndPass() override;

  void DrawMesh(const MeshDrawCommand& command) override;
  void DrawSkinnedMesh(const SkinnedMeshDrawCommand& command) override;
  void DrawWireframe(const WireframeDrawCommand& command) override;

  RenderBackendId GetBackendId() const override { return RenderBackendId::OpenGL; }
  RenderBackendCapabilities GetCapabilities() const override;
  int GetDrawCallCount() const override { return m_drawCalls; }
  bool ReadbackColorBgr8(int width,
                         int height,
                         std::vector<uint8_t>& outPixels,
                         std::string* outError) override;
  bool ReadbackDepth32F(int width,
                        int height,
                        std::vector<float>& outDepth,
                        std::string* outError) override;
  bool EnsureEditorViewportRenderTarget(uint32_t width,
                                        uint32_t height,
                                        std::string* outError) override;
  bool TryGetEditorViewportRenderTargetHandle(RenderTargetHandle* outHandle,
                                              bool needsYFlip,
                                              std::string* outError) override;
  bool EnsureSceneTextureResources(uint32_t width,
                                   uint32_t height,
                                   std::string* outError) override;
  bool TryGetSceneTextureCatalog(SceneTextureCatalog* outCatalog,
                                 std::string* outError) const override;
  bool EnsureGiHistoryResources(uint32_t width,
                                uint32_t height,
                                std::string* outError) override;
  bool TryGetGiHistoryCatalog(GiHistoryCatalog* outCatalog,
                              std::string* outError) const override;
  bool TryGetScreenSpaceReflectionPassContract(ScreenSpaceReflectionPassContract* outContract,
                                               std::string* outError) const override;
  bool TryGetScreenSpaceGlobalIlluminationPassContract(
      ScreenSpaceGlobalIlluminationPassContract* outContract, std::string* outError) const override;
  bool TryGetTemporalGiResolvePassContract(TemporalGiResolvePassContract* outContract,
                                           std::string* outError) const override;
  bool TryGetLightingCompositePassContract(LightingCompositePassContract* outContract,
                                           std::string* outError) const override;
  bool TryGetSceneTracingRepresentationContract(SceneTracingRepresentationContract* outContract,
                                                std::string* outError) const override;
  bool TryGetCachedHitLightingRepresentationContract(
      CachedHitLightingRepresentationContract* outContract,
      std::string* outError) const override;
  bool TryGetRadianceCacheFinalGatherContract(
      RadianceCacheFinalGatherContract* outContract,
      std::string* outError) const override;
  bool TryGetGiReflectionDebugVisualizationContract(
      GiReflectionDebugVisualizationContract* outContract,
      std::string* outError) const override;
  bool InvalidateGiHistory(GiHistoryResetReason reason, std::string* outError) override;

 private:
  void UploadLights(const Shader& shader);

  RenderView m_activeView;
  RenderPassId m_activePassId = RenderPassId::OpaqueScene;
  std::vector<Light> m_lights;
  int m_drawCalls = 0;
  unsigned int m_lastLightProgram = 0;
  bool m_frameActive = false;
  bool m_passActive = false;
  bool m_previousDepthTestEnabled = true;
  bool m_hasPassStateOverride = false;
};

}  // namespace Monolith
