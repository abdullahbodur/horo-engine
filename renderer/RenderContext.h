#pragma once

#include <string>
#include <vector>

#include "math/Vec4.h"
#include "renderer/Light.h"
#include "renderer/RenderTypes.h"

namespace Monolith {
// Compatibility helper for assembling frame-level output configuration.
// Graphics API state is owned by the active render backend, not by this type.
class RenderContext {
public:
  // Legacy compatibility shim for downstream starter apps. No explicit global
  // graphics initialization remains here; the active backend owns the frame
  // state.
  static void Init();

  static void BeginFrame(const Vec4 &clearColor = {0.1f, 0.1f, 0.15f, 1.0f});

  static void EndFrame();

  static RenderFrameConfig
  MakeFrameConfig(std::vector<Light> lights = {}, std::string debugLabel = {},
                  const Vec4 &clearColor = {0.1f, 0.1f, 0.15f, 1.0f},
                  bool clearColorBuffer = true, bool clearDepthBuffer = true);
};
} // namespace Monolith
