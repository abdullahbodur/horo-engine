#pragma once

/**
 * @file Texture.h
 * @brief Backend-neutral texture, view, and render-target descriptors.
 */

#include "Horo/Runtime/Render/RenderResourceDescriptors.h"

namespace Horo::Render {
    /** @brief Immutable framebuffer-compatible attachment set. */
    struct RenderTargetDescriptor {
        RenderTextureViewHandle colorAttachment;
        RenderTextureViewHandle depthAttachment;
        FramebufferExtent extent;
        std::uint32_t sampleCount{1};

        /** @brief Reports whether at least one baseline attachment and a valid extent are present. */
        [[nodiscard]] constexpr bool IsValid() const noexcept {
            return (colorAttachment.IsValid() || depthAttachment.IsValid()) && extent.IsValid() && sampleCount == 1;
        }
    };
}  // namespace Horo::Render
