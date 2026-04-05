#pragma once
#include "math/Vec4.h"

namespace Monolith {

// Thin wrapper around OpenGL state that should only be set once per frame
// rather than per draw call.
class RenderContext {
 public:
  static void Init();

  static void BeginFrame(const Vec4& clearColor = {0.1f, 0.1f, 0.15f, 1.0f});
  static void EndFrame();

  static void SetViewport(int x, int y, int w, int h);
  static void SetDepthTest(bool enabled);
  static void SetCullFace(bool enabled);
  static void SetWireframe(bool enabled);
  static void SetBlend(bool enabled);
};

}  // namespace Monolith
