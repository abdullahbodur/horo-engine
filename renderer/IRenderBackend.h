#pragma once

#include "renderer/RenderTypes.h"

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

  virtual int GetDrawCallCount() const = 0;
};

}  // namespace Monolith
