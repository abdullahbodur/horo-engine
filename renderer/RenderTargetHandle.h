#pragma once

#include <cstdint>

#include "renderer/RenderBackend.h"

namespace Monolith {
    enum class RenderNativeHandleType {
        None,
        OpenGLTexture2D,
        VulkanImGuiDescriptorSet,
    };

    struct RenderTargetHandle {
        RenderBackendId backendId = RenderBackendId::Auto;
        RenderNativeHandleType nativeType = RenderNativeHandleType::None;
        uint64_t nativeHandle = 0;
        uint64_t auxiliaryHandle = 0;
        uint32_t width = 0;
        uint32_t height = 0;
        uint64_t generation = 0;
        bool needsYFlip = false;

        bool IsValid() const {
            return nativeType != RenderNativeHandleType::None && nativeHandle != 0;
        }

        static RenderTargetHandle OpenGLTexture(uint32_t textureId,
                                                bool yFlip = false,
                                                uint32_t textureWidth = 0,
                                                uint32_t textureHeight = 0,
                                                uint64_t textureGeneration = 0) {
            return {
                RenderBackendId::OpenGL,
                RenderNativeHandleType::OpenGLTexture2D,
                static_cast<uint64_t>(textureId),
                0,
                textureWidth,
                textureHeight,
                textureGeneration,
                yFlip
            };
        }

        static RenderTargetHandle VulkanDescriptorSet(void *descriptorSet,
                                                      // NOSONAR: cpp:S5008 Vulkan/ImGui descriptor set is an opaque handle
                                                      bool yFlip = false,
                                                      uint32_t targetWidth = 0,
                                                      uint32_t targetHeight = 0,
                                                      uint64_t targetGeneration = 0) {
            return {
                RenderBackendId::Vulkan,
                RenderNativeHandleType::VulkanImGuiDescriptorSet,
                static_cast<uint64_t>(reinterpret_cast<uintptr_t>(descriptorSet)),
                0,
                targetWidth,
                targetHeight,
                targetGeneration,
                yFlip
            };
        }
    };
} // namespace Monolith
