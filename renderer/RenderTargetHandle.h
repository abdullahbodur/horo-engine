#pragma once

#include <cstdint>

#include "renderer/RenderBackend.h"

namespace Monolith {

enum class RenderNativeHandleType {
  None,
  OpenGLTexture2D,
};

struct RenderTargetHandle {
  RenderBackendId backendId = RenderBackendId::Auto;
  RenderNativeHandleType nativeType = RenderNativeHandleType::None;
  uint64_t nativeHandle = 0;
  bool needsYFlip = false;

  bool IsValid() const { return nativeType != RenderNativeHandleType::None && nativeHandle != 0; }

  static RenderTargetHandle OpenGLTexture(uint32_t textureId, bool yFlip = false) {
    return {RenderBackendId::OpenGL,
            RenderNativeHandleType::OpenGLTexture2D,
            static_cast<uint64_t>(textureId),
            yFlip};
  }
};

}  // namespace Monolith
