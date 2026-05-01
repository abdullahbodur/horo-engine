#include "renderer/RenderBackendFactory.h"

#include <memory>

#if defined(HORO_RENDERER_OPENGL)
#include "renderer/OpenGLRenderBackend.h"
#endif
#include "renderer/VulkanRenderBackend.h"
#include "renderer/null/NullRenderBackend.h"

namespace Horo {
    RenderBackendCreateResult
    CreateRenderBackend(const RenderBackendSelection &selection) {
        const RenderBackendId resolvedBackend =
                ResolveRequestedRenderBackend(selection);
        using enum RenderBackendId;
        switch (resolvedBackend) {
            case OpenGL:
#if defined(HORO_RENDERER_OPENGL)
                return {std::make_unique<OpenGLRenderBackend>(), OpenGL, {}};
#else
                return {nullptr, OpenGL, "OpenGL backend not available in this build."};
#endif
            case Vulkan:
#if defined(HORO_HAS_VULKAN)
                {
                    std::unique_ptr<VulkanRenderBackend> backend =
                            std::make_unique<VulkanRenderBackend>(selection.nativeWindowHandle);
                    if (!backend->IsInitialized()) {
                        return {
                            nullptr, Vulkan,
                            backend->GetLastError().empty()
                                ? std::string("Vulkan backend initialization failed.")
                                : backend->GetLastError()
                        };
                    }
                    return {std::move(backend), Vulkan, {}};
                }
#else
                return {
                    nullptr, Vulkan,
                    "Vulkan backend support is not compiled in. Enable "
                    "HORO_ENGINE_ENABLE_VULKAN to build it."
                };
#endif
            case Null:
                return {std::make_unique<NullRenderBackend>(), Null, {}};
            case Auto:
                break;
        }

        return {
            nullptr, resolvedBackend,
            "Failed to resolve a supported render backend."
        };
    }
} // namespace Horo
