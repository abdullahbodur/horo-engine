#pragma once

#include "renderer/Camera.h"
#include "renderer/RenderTypes.h"

namespace Horo {
    inline RenderView BuildRenderView(const Camera &camera) {
        return {camera.GetView(), camera.GetProjection(), camera.position};
    }
} // namespace Horo
