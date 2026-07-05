#include "renderer/RenderBackend.h"

namespace Horo {
    const char *ToString(RenderBackendId backendId) {
        using enum RenderBackendId;
        switch (backendId) {
            case Auto:
                return "Auto";
            case OpenGL:
                return "OpenGL";
            case Vulkan:
                return "Vulkan";
            case Null:
                return "Null";
        }

        return "Unknown";
    }

    RenderBackendId
    ResolveRequestedRenderBackend(const RenderBackendSelection &selection) {
        return selection.requested == RenderBackendId::Auto
                   ? RenderBackendId::OpenGL
                   : selection.requested;
    }

    RenderBackendCapabilities
    GetDefaultRenderBackendCapabilities(RenderBackendId backendId) {
        switch (backendId) {
            case RenderBackendId::Auto:
                return GetDefaultRenderBackendCapabilities(RenderBackendId::OpenGL);
            case RenderBackendId::OpenGL:
                return {
                    .supportsDebugDraw = true,
                    .supportsWireframeOverlay = true,
                    .supportsDebugLabels = false,
                    .supportsOffscreenTargets = true,
                    .supportsNativeTextureHandles = true,
                    .supportsReadback = true,
                    .supportsDepthReadback = true,
                    .supportsDebugHud = true,
                    .supportsComputePasses = false,
                    .supportsGpuTimestamps = false,
                    .supportsBindlessResources = false
                };
            case RenderBackendId::Vulkan:
                return {
                    .supportsDebugDraw = false,
                    .supportsWireframeOverlay = false,
                    .supportsDebugLabels = false,
                    .supportsOffscreenTargets = true,
                    .supportsNativeTextureHandles = true,
                    .supportsReadback = false,
                    .supportsDepthReadback = false,
                    .supportsDebugHud = false,
                    .supportsComputePasses = false,
                    .supportsGpuTimestamps = false,
                    .supportsBindlessResources = false
                };
            case RenderBackendId::Null:
                return {};
        }

        return {};
    }

    bool IsRenderBackendSupported(RenderBackendId backendId) {
        using enum RenderBackendId;
        switch (ResolveRequestedRenderBackend(RenderBackendSelection{backendId})) {
            case OpenGL:
                return true;
            case Vulkan:
#if defined(HORO_HAS_VULKAN)
                return true;
#else
                return false;
#endif
            case Null:
                return true;
            case Auto:
                break;
        }

        return false;
    }
} // namespace Horo
