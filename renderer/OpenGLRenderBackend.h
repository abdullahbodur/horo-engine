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

  int GetDrawCallCount() const override { return m_drawCalls; }

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
