#pragma once

#include <string>

namespace Monolith {

enum class RenderBackendId {
  Auto,
  OpenGL,
  Vulkan,
};

struct RenderBackendCapabilities {
  bool supportsWireframeOverlay = false;
  bool supportsDebugLabels = false;
  bool supportsComputePasses = false;
  bool supportsGpuTimestamps = false;
  bool supportsBindlessResources = false;
};

struct RenderBackendSelection {
  RenderBackendId requested = RenderBackendId::Auto;
  void* nativeWindowHandle = nullptr;
};

struct RenderBackendInitResult {
  bool ok = false;
  RenderBackendId requested = RenderBackendId::Auto;
  RenderBackendId selected = RenderBackendId::Auto;
  RenderBackendCapabilities capabilities;
  std::string error;
};

const char* ToString(RenderBackendId backendId);
RenderBackendId ResolveRequestedRenderBackend(const RenderBackendSelection& selection);
RenderBackendCapabilities GetDefaultRenderBackendCapabilities(RenderBackendId backendId);
bool IsRenderBackendSupported(RenderBackendId backendId);

}  // namespace Monolith
