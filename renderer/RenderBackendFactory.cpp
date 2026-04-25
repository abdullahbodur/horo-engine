#include "renderer/RenderBackendFactory.h"

#include <memory>

#include "renderer/OpenGLRenderBackend.h"
#include "renderer/VulkanRenderBackend.h"

namespace Monolith {
RenderBackendCreateResult
CreateRenderBackend(const RenderBackendSelection &selection) {
  const RenderBackendId resolvedBackend =
      ResolveRequestedRenderBackend(selection);
  using enum RenderBackendId;
  switch (resolvedBackend) {
  case OpenGL:
    return {std::make_unique<OpenGLRenderBackend>(), OpenGL, {}};
  case Vulkan:
#if defined(MONOLITH_HAS_VULKAN)
  {
    std::unique_ptr<VulkanRenderBackend> backend =
        std::make_unique<VulkanRenderBackend>(selection.nativeWindowHandle);
    if (!backend->IsInitialized()) {
      return {nullptr, Vulkan,
              backend->GetLastError().empty()
                  ? std::string("Vulkan backend initialization failed.")
                  : backend->GetLastError()};
    }
    return {std::move(backend), Vulkan, {}};
  }
#else
    return {nullptr, Vulkan,
            "Vulkan backend support is not compiled in. Enable "
            "MONOLITH_ENGINE_ENABLE_VULKAN to build it."};
#endif
  case Auto:
    break;
  }

  return {nullptr, resolvedBackend,
          "Failed to resolve a supported render backend."};
}
} // namespace Monolith
