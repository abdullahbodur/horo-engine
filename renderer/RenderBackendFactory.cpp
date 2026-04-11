#include "renderer/RenderBackendFactory.h"

#include <memory>

#include "renderer/OpenGLRenderBackend.h"
#include "renderer/VulkanRenderBackend.h"

namespace Monolith {

RenderBackendCreateResult CreateRenderBackend(const RenderBackendSelection& selection) {
  const RenderBackendId resolvedBackend = ResolveRequestedRenderBackend(selection);
  switch (resolvedBackend) {
    case RenderBackendId::OpenGL:
      return {std::make_unique<OpenGLRenderBackend>(), RenderBackendId::OpenGL, {}};
    case RenderBackendId::Vulkan:
#if defined(MONOLITH_HAS_VULKAN)
    {
      std::unique_ptr<VulkanRenderBackend> backend =
          std::make_unique<VulkanRenderBackend>(selection.nativeWindowHandle);
      if (!backend->IsInitialized()) {
        return {nullptr,
                RenderBackendId::Vulkan,
                backend->GetLastError().empty()
                    ? std::string("Vulkan backend initialization failed.")
                    : backend->GetLastError()};
      }
      return {std::move(backend), RenderBackendId::Vulkan, {}};
    }
#else
      return {nullptr,
              RenderBackendId::Vulkan,
              "Vulkan backend support is not compiled in. Enable MONOLITH_ENGINE_ENABLE_VULKAN to build it."};
#endif
    case RenderBackendId::Auto:
      break;
  }

  return {nullptr, resolvedBackend, "Failed to resolve a supported render backend."};
}

}  // namespace Monolith
