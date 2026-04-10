#pragma once

#include "renderer/Camera.h"
#include "renderer/RenderTypes.h"

namespace Monolith {

inline RenderView BuildRenderView(const Camera& camera) {
  return {camera.GetView(), camera.GetProjection(), camera.position};
}

}  // namespace Monolith
