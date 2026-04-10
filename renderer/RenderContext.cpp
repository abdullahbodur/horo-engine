#include "renderer/RenderContext.h"

namespace Monolith {

RenderFrameConfig RenderContext::MakeFrameConfig(std::vector<Light> lights,
                                                 std::string debugLabel,
                                                 const Vec4& clearColor,
                                                 bool clearColorBuffer,
                                                 bool clearDepthBuffer) {
  RenderFrameConfig frame;
  frame.lights = std::move(lights);
  frame.debugLabel = std::move(debugLabel);
  frame.clearColor = clearColor;
  frame.clearColorBuffer = clearColorBuffer;
  frame.clearDepthBuffer = clearDepthBuffer;
  return frame;
}

}  // namespace Monolith
