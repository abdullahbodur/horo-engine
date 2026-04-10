#include "renderer/RenderBackendFactory.h"

#include <memory>

#include "renderer/OpenGLRenderBackend.h"

namespace Monolith {

RenderBackendCreateResult CreateRenderBackend(const RenderBackendSelection& selection) {
  const RenderBackendId resolvedBackend = ResolveRequestedRenderBackend(selection);
  switch (resolvedBackend) {
    case RenderBackendId::OpenGL:
      return {std::make_unique<OpenGLRenderBackend>(), RenderBackendId::OpenGL, {}};
    case RenderBackendId::Vulkan:
      return {nullptr,
              RenderBackendId::Vulkan,
              "Vulkan backend is not integrated yet. Select OpenGL or Auto for now."};
    case RenderBackendId::Auto:
      break;
  }

  return {nullptr, resolvedBackend, "Failed to resolve a supported render backend."};
}

}  // namespace Monolith
