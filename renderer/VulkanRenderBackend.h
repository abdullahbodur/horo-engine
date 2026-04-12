#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "renderer/IRenderBackend.h"
#include "renderer/RenderTargetHandle.h"

namespace Monolith
{

  class VulkanRenderBackend : public IRenderBackend
  {
  public:
    using OverlayRenderCallback = void (*)(void *userData, void *commandBufferHandle);

    explicit VulkanRenderBackend(void *nativeWindowHandle);
    ~VulkanRenderBackend() override;

    VulkanRenderBackend(const VulkanRenderBackend &) = delete;
    VulkanRenderBackend &operator=(const VulkanRenderBackend &) = delete;
    VulkanRenderBackend(VulkanRenderBackend &&) = delete;
    VulkanRenderBackend &operator=(VulkanRenderBackend &&) = delete;

    void BeginFrame(const RenderFrameConfig &frame) override;
    void EndFrame() override;
    void BeginPass(const RenderPassConfig &pass) override;
    void EndPass() override;

    void DrawMesh(const MeshDrawCommand &command) override;
    void DrawSkinnedMesh(const SkinnedMeshDrawCommand &command) override;
    void DrawWireframe(const WireframeDrawCommand &command) override;
    bool ReadbackColorBgr8(int width,
                           int height,
                           std::vector<uint8_t> &outPixels,
                           std::string *outError) override;
    bool ReadbackDepth32F(int width,
                          int height,
                          std::vector<float> &outDepth,
                          std::string *outError) override;
    bool EnsureEditorViewportRenderTarget(uint32_t width,
                                          uint32_t height,
                                          std::string *outError) override;
    bool TryGetEditorViewportRenderTargetHandle(RenderTargetHandle *outHandle,
                                                bool needsYFlip,
                                                std::string *outError) override;
    bool EnsureSceneTextureResources(uint32_t width,
                                     uint32_t height,
                                     std::string *outError) override;
    bool TryGetSceneTextureCatalog(SceneTextureCatalog *outCatalog,
                                   std::string *outError) const override;
    bool EnsureGiHistoryResources(uint32_t width,
                                  uint32_t height,
                                  std::string *outError) override;
    bool TryGetGiHistoryCatalog(GiHistoryCatalog *outCatalog,
                                std::string *outError) const override;
    bool TryGetScreenSpaceReflectionPassContract(ScreenSpaceReflectionPassContract *outContract,
                                                 std::string *outError) const override;
    bool InvalidateGiHistory(GiHistoryResetReason reason,
                             std::string *outError) override;

    RenderBackendId GetBackendId() const override { return RenderBackendId::Vulkan; }
    RenderBackendCapabilities GetCapabilities() const override;
    int GetDrawCallCount() const override { return m_drawCalls; }

    bool IsInitialized() const;
    bool HasOpaqueRasterScaffold() const;
    bool HasOpaquePipelineCreationScaffold() const;
    bool HasOpaqueShaderPipelineScaffold() const;
    bool HasOpaqueGraphicsPipelineScaffold() const;
    bool HasOpaqueDrawExecutionReady() const;
    bool TryGetImGuiVulkanInitData(void **outInstance,
                                   void **outPhysicalDevice,
                                   void **outDevice,
                                   uint32_t *outQueueFamily,
                                   void **outQueue,
                                   void **outRenderPass,
                                   uint32_t *outImageCount) const;
    void *GetActiveCommandBufferHandle() const;
    void QueueOverlayRenderCallback(OverlayRenderCallback callback, void *userData);
    const std::string &GetLastError() const { return m_lastError; }
    int GetExecutedOpaqueIndexedDrawCount() const { return m_executedOpaqueIndexedDraws; }

    struct OffscreenTargetMetadata
    {
      uint32_t width = 0;
      uint32_t height = 0;
      uint64_t generation = 0;
      bool readyForSampling = false;
      bool hasImGuiDescriptor = false;
    };

    bool EnsureOffscreenRenderTarget(const std::string &targetKey, uint32_t width, uint32_t height);
    bool TryGetOffscreenRenderTargetHandle(const std::string &targetKey,
                                           RenderTargetHandle *outHandle,
                                           bool needsYFlip = false);
    bool TryGetOffscreenRenderTargetMetadata(const std::string &targetKey,
                                             OffscreenTargetMetadata *outMetadata) const;
    void DestroyOffscreenRenderTarget(const std::string &targetKey);
    void DestroyAllOffscreenRenderTargets();

    struct TranslatedMaterialState
    {
      Vec4 baseColor = {1.0f, 1.0f, 1.0f, 1.0f};
      float roughness = 0.5f;
      float metallic = 0.0f;
      float uvScale = 1.0f;
      bool usesAlbedoMap = false;
      bool usesCustomShader = false;
    };

    struct OpaquePipelineKey
    {
      bool usesAlbedoMap = false;
      bool usesCustomShader = false;
      bool writesDepth = true;
      bool depthTestEnabled = true;
    };

    static TranslatedMaterialState TranslateMaterialState(const Material &material);
    static int ResolveIndexCount(const Mesh &mesh);
    static OpaquePipelineKey BuildOpaquePipelineKey(const TranslatedMaterialState &materialState);

  private:
    struct Context;

    bool Initialize(void *nativeWindowHandle);
    void Shutdown();
    bool RecreateSwapchain();
    void DestroySwapchain();
    bool CreateOpaqueRasterScaffold();
    void DestroyOpaqueRasterScaffold();
    bool CreateOpaquePipelineCreationScaffold();
    void DestroyOpaquePipelineCreationScaffold();
    bool CreateDepthResources();
    void DestroyDepthResources();
    bool EnsureReadbackBuffers();
    void DestroyReadbackBuffers();
    bool CreateOpaqueMaterialBindingScaffold();
    void DestroyOpaqueMaterialBindingScaffold();
    bool CreateOpaqueDrawIndexBuffer();
    void DestroyOpaqueDrawIndexBuffer();
    bool EnsureOpaqueMeshGpuBuffers(const Mesh &mesh);
    void DestroyOpaqueMeshGpuBuffers();
    bool GetOrCreateOpaquePipeline(const OpaquePipelineKey &key, void **outPipelineHandle);
    bool CreateOpaqueShaderPipelineScaffold();
    void DestroyOpaqueShaderPipelineScaffold();
    bool CreateOpaqueGraphicsPipelineScaffold();
    void DestroyOpaqueGraphicsPipelineScaffold();
    bool RecordFrameCommands(const RenderFrameConfig &frame);
    void ExecuteScreenSpaceReflectionPass();
    bool EnsureOffscreenRenderPass();
    void DestroyOffscreenRenderPass();
    bool CreateOffscreenRenderTargetResources(const std::string &targetKey,
                                              uint32_t width,
                                              uint32_t height,
                                              uint64_t previousGeneration);
    void DestroyOffscreenRenderTargetResources(const std::string &targetKey);
    bool BuildOffscreenResourceHandle(const std::string &targetKey,
                                      BackendResourceHandle *outHandle) const;
    bool TryRegisterOffscreenTargetImGuiDescriptor(const std::string &targetKey,
                                                   RenderTargetHandle *outHandle,
                                                   bool needsYFlip);

    struct PendingOpaqueDraw
    {
      const Mesh *mesh = nullptr;
      int indexCount = 0;
      Mat4 modelMatrix = Mat4::Identity();
      TranslatedMaterialState material;
      OpaquePipelineKey pipelineKey;
    };

    std::unique_ptr<Context> m_context;
    RenderFrameConfig m_activeFrame;
    RenderView m_activeView;
    std::vector<PendingOpaqueDraw> m_pendingOpaqueDraws;
    OverlayRenderCallback m_pendingOverlayRenderCallback = nullptr;
    void *m_pendingOverlayRenderUserData = nullptr;
    std::string m_lastError;
    RenderPassId m_activePassId = RenderPassId::OpaqueScene;
    int m_drawCalls = 0;
    int m_executedOpaqueIndexedDraws = 0;
    bool m_frameActive = false;
    bool m_passActive = false;
    SceneTextureCatalog m_sceneTextureCatalog;
    GiHistoryCatalog m_giHistoryCatalog;
    ScreenSpaceReflectionPassContract m_lastSsrPassContract;
    TemporalHistoryState m_lastTemporalHistoryState;
    bool m_hasTemporalHistoryState = false;
    bool m_hasSsrPassContract = false;
  };

} // namespace Monolith
