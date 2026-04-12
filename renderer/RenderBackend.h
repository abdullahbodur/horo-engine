#pragma once

#include <string>

#include "renderer/RenderTypes.h"

namespace Monolith {

enum class RenderBackendId {
  Auto,
  OpenGL,
  Vulkan,
};

struct RenderBackendCapabilities {
  bool supportsDebugDraw = false;
  bool supportsWireframeOverlay = false;
  bool supportsDebugLabels = false;
  bool supportsOffscreenTargets = false;
  bool supportsNativeTextureHandles = false;
  bool supportsReadback = false;
  bool supportsDepthReadback = false;
  bool supportsDebugHud = false;
  bool supportsComputePasses = false;
  bool supportsGpuTimestamps = false;
  bool supportsBindlessResources = false;
  bool supportsScreenSpaceReflections = false;
  bool supportsScreenSpaceGlobalIllumination = false;
  bool supportsTemporalGiResolve = false;
  bool supportsGiComposite = false;
  RenderFeatureQualityTier maxReflectionQuality = RenderFeatureQualityTier::Off;
  RenderFeatureQualityTier maxGlobalIlluminationQuality = RenderFeatureQualityTier::Off;
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
