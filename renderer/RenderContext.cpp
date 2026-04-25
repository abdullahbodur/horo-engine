#include "renderer/RenderContext.h"

#include "renderer/Renderer.h"

namespace Monolith {
void RenderContext::Init() {
  // Nothing to initialize; RenderContext delegates directly to Renderer.
}

void RenderContext::BeginFrame(const Vec4 &clearColor) {
  Renderer::BeginFrame(
      MakeFrameConfig({}, "compat-render-context-frame", clearColor));
}

void RenderContext::EndFrame() {
  if (Renderer::IsFrameActive())
    Renderer::EndFrame();
}

RenderFrameConfig RenderContext::MakeFrameConfig(std::vector<Light> lights,
                                                 std::string debugLabel,
                                                 const Vec4 &clearColor,
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
} // namespace Monolith
