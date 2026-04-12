#include "renderer/RenderBackend.h"

namespace Monolith {

const char* ToString(RenderBackendId backendId) {
  switch (backendId) {
    case RenderBackendId::Auto:
      return "Auto";
    case RenderBackendId::OpenGL:
      return "OpenGL";
    case RenderBackendId::Vulkan:
      return "Vulkan";
  }

  return "Unknown";
}

RenderBackendId ResolveRequestedRenderBackend(const RenderBackendSelection& selection) {
  return selection.requested == RenderBackendId::Auto ? RenderBackendId::OpenGL : selection.requested;
}

RenderBackendCapabilities GetDefaultRenderBackendCapabilities(RenderBackendId backendId) {
  switch (backendId) {
    case RenderBackendId::Auto:
      return GetDefaultRenderBackendCapabilities(RenderBackendId::OpenGL);
    case RenderBackendId::OpenGL:
      return {.supportsDebugDraw = true,
              .supportsWireframeOverlay = true,
              .supportsDebugLabels = false,
              .supportsOffscreenTargets = true,
              .supportsNativeTextureHandles = true,
               .supportsReadback = true,
               .supportsDepthReadback = true,
               .supportsDebugHud = true,
               .supportsComputePasses = false,
               .supportsGpuTimestamps = false,
               .supportsBindlessResources = false,
               .supportsSceneTextureAbstractions = false,
               .supportsGiHistoryResources = false};
    case RenderBackendId::Vulkan:
      return {.supportsDebugDraw = false,
              .supportsWireframeOverlay = false,
              .supportsDebugLabels = false,
              .supportsOffscreenTargets = true,
              .supportsNativeTextureHandles = true,
               .supportsReadback = false,
               .supportsDepthReadback = false,
               .supportsDebugHud = false,
               .supportsComputePasses = false,
               .supportsGpuTimestamps = false,
               .supportsBindlessResources = false,
               .supportsSceneTextureAbstractions = true,
               .supportsGiHistoryResources = true};
  }

  return {};
}

bool IsRenderBackendSupported(RenderBackendId backendId) {
  switch (ResolveRequestedRenderBackend(RenderBackendSelection{backendId})) {
    case RenderBackendId::OpenGL:
      return true;
    case RenderBackendId::Vulkan:
#if defined(MONOLITH_HAS_VULKAN)
      return true;
#else
      return false;
#endif
    case RenderBackendId::Auto:
      break;
  }

  return false;
}

}  // namespace Monolith
