#pragma once

/**
 * @file Texture.h
 * @brief Backend-neutral texture, view, and render-target descriptors.
 */

#include "Horo/Runtime/Render/RenderResource.h"

#include <cstdint>

namespace Horo::Render {
    /** @brief Texture dimensions supported by the baseline resident-resource contract. */
    enum class RenderTextureDimension : std::uint8_t {
        TwoD,
    };

    /** @brief Backend-neutral formats required by editor offscreen rendering. */
    enum class RenderTextureFormat : std::uint8_t {
        Rgba8Unorm,
        Depth24Stencil8,
        Depth32Float,
    };

    /** @brief Independent purposes permitted for one resident texture. */
    enum class RenderTextureUsage : std::uint8_t {
        None = 0,
        Sampled = 1U << 0U,
        RenderAttachment = 1U << 1U,
    };

    /** @brief Combines independent texture purposes without exposing backend flags. */
    [[nodiscard]] constexpr RenderTextureUsage operator|(const RenderTextureUsage left, const RenderTextureUsage right) noexcept {
        return static_cast<RenderTextureUsage>(static_cast<std::uint8_t>(left) | static_cast<std::uint8_t>(right));
    }

    /** @brief Reports whether every requested texture usage bit is present. */
    [[nodiscard]] constexpr bool HasTextureUsage(const RenderTextureUsage value, const RenderTextureUsage requested) noexcept {
        return (static_cast<std::uint8_t>(value) & static_cast<std::uint8_t>(requested)) == static_cast<std::uint8_t>(requested);
    }

    /** @brief Immutable structural policy for one resident texture. */
    struct RenderTextureDescriptor {
        RenderTextureDimension dimension{RenderTextureDimension::TwoD};
        FramebufferExtent extent;
        RenderTextureFormat format{RenderTextureFormat::Rgba8Unorm};
        std::uint32_t mipCount{1};
        std::uint32_t layerCount{1};
        std::uint32_t sampleCount{1};
        RenderTextureUsage usage{RenderTextureUsage::None};

        /** @brief Reports whether the baseline texture policy is structurally valid. */
        [[nodiscard]] constexpr bool IsValid() const noexcept {
            constexpr std::uint8_t validUsageBits =
                static_cast<std::uint8_t>(RenderTextureUsage::Sampled) | static_cast<std::uint8_t>(RenderTextureUsage::RenderAttachment);
            const std::uint8_t usageBits = static_cast<std::uint8_t>(usage);
            const bool formatValid = format == RenderTextureFormat::Rgba8Unorm || format == RenderTextureFormat::Depth24Stencil8 ||
                                     format == RenderTextureFormat::Depth32Float;
            return dimension == RenderTextureDimension::TwoD && extent.IsValid() && formatValid && mipCount == 1 && layerCount == 1 &&
                   sampleCount == 1 && usageBits != 0 && (usageBits & static_cast<std::uint8_t>(~validUsageBits)) == 0;
        }
    };

    /** @brief Selects the image plane exposed by a texture view. */
    enum class RenderTextureAspect : std::uint8_t {
        Color,
        Depth,
        DepthStencil,
    };

    /** @brief Immutable view over one exact resident texture generation. */
    struct RenderTextureViewDescriptor {
        RenderTextureHandle texture;
        RenderTextureFormat format{RenderTextureFormat::Rgba8Unorm};
        RenderTextureAspect aspect{RenderTextureAspect::Color};
        std::uint32_t baseMip{0};
        std::uint32_t mipCount{1};
        std::uint32_t baseLayer{0};
        std::uint32_t layerCount{1};

        /** @brief Reports whether ranges and typed policy values are structurally valid. */
        [[nodiscard]] constexpr bool IsValid() const noexcept {
            const bool formatValid = format == RenderTextureFormat::Rgba8Unorm || format == RenderTextureFormat::Depth24Stencil8 ||
                                     format == RenderTextureFormat::Depth32Float;
            const bool aspectValid =
                aspect == RenderTextureAspect::Color || aspect == RenderTextureAspect::Depth || aspect == RenderTextureAspect::DepthStencil;
            return texture.IsValid() && formatValid && aspectValid && mipCount > 0 && layerCount > 0;
        }
    };

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
