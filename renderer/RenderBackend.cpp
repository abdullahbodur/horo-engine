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
      return {.supportsWireframeOverlay = true,
              .supportsDebugLabels = false,
              .supportsComputePasses = false,
              .supportsGpuTimestamps = false,
              .supportsBindlessResources = false};
    case RenderBackendId::Vulkan:
      return {};
  }

  return {};
}

bool IsRenderBackendSupported(RenderBackendId backendId) {
  switch (ResolveRequestedRenderBackend(RenderBackendSelection{backendId})) {
    case RenderBackendId::OpenGL:
      return true;
    case RenderBackendId::Vulkan:
      return false;
    case RenderBackendId::Auto:
      break;
  }

  return false;
}

}  // namespace Monolith
