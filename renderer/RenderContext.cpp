#include "renderer/RenderContext.h"

#include <glad/glad.h>

namespace Horo {

void RenderContext::Init() {
  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_LEQUAL);
  glEnable(GL_CULL_FACE);
  glCullFace(GL_BACK);
  glFrontFace(GL_CCW);
  glEnable(GL_MULTISAMPLE);
}

void RenderContext::BeginFrame(const Vec4& c) {
  glClearColor(c.x, c.y, c.z, c.w);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void RenderContext::EndFrame() {}

void RenderContext::SetViewport(int x, int y, int w, int h) {
  glViewport(x, y, w, h);
}

void RenderContext::SetDepthTest(bool enabled) {
  enabled ? glEnable(GL_DEPTH_TEST) : glDisable(GL_DEPTH_TEST);
}

void RenderContext::SetCullFace(bool enabled) {
  enabled ? glEnable(GL_CULL_FACE) : glDisable(GL_CULL_FACE);
}

void RenderContext::SetWireframe(bool enabled) {
  glPolygonMode(GL_FRONT_AND_BACK, enabled ? GL_LINE : GL_FILL);
}

void RenderContext::SetBlend(bool enabled) {
  if (enabled) {
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  } else {
    glDisable(GL_BLEND);
  }
}

}  // namespace Horo
