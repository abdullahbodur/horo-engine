#pragma once

#include <memory>
#include <string>
#include <vector>

#include "renderer/IRenderBackend.h"

namespace Monolith {

class VulkanRenderBackend : public IRenderBackend {
 public:
  explicit VulkanRenderBackend(void* nativeWindowHandle);
  ~VulkanRenderBackend() override;

  VulkanRenderBackend(const VulkanRenderBackend&) = delete;
  VulkanRenderBackend& operator=(const VulkanRenderBackend&) = delete;
  VulkanRenderBackend(VulkanRenderBackend&&) = delete;
  VulkanRenderBackend& operator=(VulkanRenderBackend&&) = delete;

  void BeginFrame(const RenderFrameConfig& frame) override;
  void EndFrame() override;
  void BeginPass(const RenderPassConfig& pass) override;
  void EndPass() override;

  void DrawMesh(const MeshDrawCommand& command) override;
  void DrawSkinnedMesh(const SkinnedMeshDrawCommand& command) override;
  void DrawWireframe(const WireframeDrawCommand& command) override;

  RenderBackendId GetBackendId() const override { return RenderBackendId::Vulkan; }
  RenderBackendCapabilities GetCapabilities() const override;
  int GetDrawCallCount() const override { return m_drawCalls; }

  bool IsInitialized() const;
  const std::string& GetLastError() const { return m_lastError; }

  struct TranslatedMaterialState {
    Vec4 baseColor = {1.0f, 1.0f, 1.0f, 1.0f};
    float roughness = 0.5f;
    float metallic = 0.0f;
    float uvScale = 1.0f;
    bool usesAlbedoMap = false;
    bool usesCustomShader = false;
  };

  static TranslatedMaterialState TranslateMaterialState(const Material& material);
  static int ResolveIndexCount(const Mesh& mesh);

 private:
  struct Context;

  bool Initialize(void* nativeWindowHandle);
  void Shutdown();
  bool RecreateSwapchain();
  void DestroySwapchain();
  bool RecordFrameCommands(const RenderFrameConfig& frame);

  struct PendingOpaqueDraw {
    int indexCount = 0;
    Mat4 modelMatrix = Mat4::Identity();
    TranslatedMaterialState material;
  };

  std::unique_ptr<Context> m_context;
  RenderFrameConfig m_activeFrame;
  RenderView m_activeView;
  std::vector<PendingOpaqueDraw> m_pendingOpaqueDraws;
  std::string m_lastError;
  RenderPassId m_activePassId = RenderPassId::OpaqueScene;
  int m_drawCalls = 0;
  bool m_frameActive = false;
  bool m_passActive = false;
};

}  // namespace Monolith
