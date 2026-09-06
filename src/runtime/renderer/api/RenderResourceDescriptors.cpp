#include "Horo/Runtime/Render/RenderResourceDescriptors.h"

#include "Horo/Runtime/Render/RenderResourceDescriptorErrors.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Horo::Render {
    namespace {
        constexpr std::size_t MaximumInitialTextureSubresources = 1'024;

        [[nodiscard]] constexpr bool IsKnown(const RenderSamplerFilter value) noexcept {
            using enum RenderSamplerFilter;
            return value == Nearest || value == Linear;
        }

        [[nodiscard]] constexpr bool IsKnown(const RenderSamplerMipmapMode value) noexcept {
            using enum RenderSamplerMipmapMode;
            return value == Nearest || value == Linear;
        }

        [[nodiscard]] constexpr bool IsKnown(const RenderSamplerAddressMode value) noexcept {
            using enum RenderSamplerAddressMode;
            return value == Repeat || value == MirroredRepeat || value == ClampToEdge || value == ClampToBorder;
        }

        [[nodiscard]] constexpr bool IsKnown(const RenderCompareFunction value) noexcept {
            using enum RenderCompareFunction;
            return value == Never || value == Less || value == LessEqual || value == Equal || value == GreaterEqual || value == Greater ||
                   value == NotEqual || value == Always;
        }

        [[nodiscard]] constexpr std::size_t BytesPerTexel(const RenderTextureFormat format) noexcept {
            using enum RenderTextureFormat;
            switch (format) {
                case R8Unorm:
                    return 1;
                case Rg8Unorm:
                case R16Float:
                case Depth16Unorm:
                    return 2;
                case Rgba8Unorm:
                case Rgba8UnormSrgb:
                case Bgra8Unorm:
                case Bgra8UnormSrgb:
                case Rg16Float:
                case R32Float:
                case Depth24Stencil8:
                case Depth32Float:
                    return 4;
                case Rgba16Float:
                case Rg32Float:
                case Depth32FloatStencil8:
                    return 8;
                case Rgba32Float:
                    return 16;
            }
            return 0;
        }

        [[nodiscard]] constexpr bool CheckedMultiply(const std::size_t left, const std::size_t right, std::size_t &product) noexcept {
            if (left != 0 && right > std::numeric_limits<std::size_t>::max() / left)
                return false;
            product = left * right;
            return true;
        }

        [[nodiscard]] constexpr bool IsAspectCompatible(const RenderTextureFormat format, const RenderTextureAspect aspect) noexcept {
            using enum RenderTextureAspect;
            using enum RenderTextureFormat;
            if (format != Depth16Unorm && format != Depth24Stencil8 && format != Depth32Float && format != Depth32FloatStencil8)
                return aspect == Color;
            if (format == Depth24Stencil8)
                return aspect == Depth || aspect == Stencil || aspect == DepthStencil;
            if (format == Depth32FloatStencil8)
                return aspect == Depth || aspect == Stencil || aspect == DepthStencil;
            if (format == Depth16Unorm || format == Depth32Float)
                return aspect == Depth;
            return false;
        }

        [[nodiscard]] constexpr bool AreViewFormatsCompatible(const RenderTextureFormat texture, const RenderTextureFormat view) noexcept {
            if (texture == view)
                return true;
            using enum RenderTextureFormat;
            return (texture == Rgba8Unorm && view == Rgba8UnormSrgb) || (texture == Rgba8UnormSrgb && view == Rgba8Unorm) ||
                   (texture == Bgra8Unorm && view == Bgra8UnormSrgb) || (texture == Bgra8UnormSrgb && view == Bgra8Unorm);
        }

        [[nodiscard]] constexpr bool IsViewDimensionCompatible(const RenderTextureDescriptor &texture,
                                                               const RenderTextureViewDescriptor &view) noexcept {
            using enum RenderTextureDimension;
            if (texture.dimension == OneD)
                return view.dimension == RenderTextureViewDimension::OneD && view.layerCount == 1;
            if (texture.dimension == ThreeD)
                return view.dimension == RenderTextureViewDimension::ThreeD && view.baseLayer == 0 && view.layerCount == 1;
            if (view.dimension == RenderTextureViewDimension::TwoD)
                return view.layerCount == 1;
            if (view.dimension == RenderTextureViewDimension::TwoDArray)
                return true;
            if (view.dimension == RenderTextureViewDimension::Cube)
                return view.baseLayer % 6 == 0 && view.layerCount == 6;
            if (view.dimension == RenderTextureViewDimension::CubeArray)
                return view.baseLayer % 6 == 0 && view.layerCount % 6 == 0;
            return false;
        }

        [[nodiscard]] Result<void> InitialDataFailure() {
            return Result<void>::Failure(MakeError(RenderResourceDescriptorErrors::TextureInitialDataInvalid));
        }
    }  // namespace

    /** @copydoc RenderSamplerDescriptor::IsValid */
    bool RenderSamplerDescriptor::IsValid() const noexcept {
        return IsKnown(minFilter) && IsKnown(magFilter) && IsKnown(mipmapMode) && IsKnown(addressU) && IsKnown(addressV) &&
               IsKnown(addressW) && std::isfinite(minimumLod) && std::isfinite(maximumLod) && minimumLod >= 0.0F &&
               maximumLod >= minimumLod && std::isfinite(maximumAnisotropy) && maximumAnisotropy >= 1.0F && IsKnown(compare);
    }

    /** @copydoc ValidateRenderBufferDescriptor */
    Result<void> ValidateRenderBufferDescriptor(const RenderBufferDescriptor &descriptor) {
        if (!descriptor.IsValid())
            return Result<void>::Failure(MakeError(RenderResourceDescriptorErrors::BufferInvalid));
        return Result<void>::Success();
    }

    /** @copydoc ValidateRenderTextureDescriptor */
    Result<void> ValidateRenderTextureDescriptor(const RenderTextureDescriptor &descriptor) {
        if (!descriptor.IsValid())
            return Result<void>::Failure(MakeError(RenderResourceDescriptorErrors::TextureInvalid));
        return Result<void>::Success();
    }

    /** @copydoc ValidateRenderSamplerDescriptor */
    Result<void> ValidateRenderSamplerDescriptor(const RenderSamplerDescriptor &descriptor) {
        if (!descriptor.IsValid())
            return Result<void>::Failure(MakeError(RenderResourceDescriptorErrors::SamplerInvalid));
        return Result<void>::Success();
    }

    /** @copydoc ValidateRenderTextureViewDescriptor */
    Result<void> ValidateRenderTextureViewDescriptor(const RenderTextureViewDescriptor &descriptor) {
        if (!descriptor.IsValid())
            return Result<void>::Failure(MakeError(RenderResourceDescriptorErrors::TextureViewInvalid));
        return Result<void>::Success();
    }

    /** @copydoc ValidateRenderBufferInitialData */
    Result<void> ValidateRenderBufferInitialData(const RenderBufferDescriptor &descriptor, const RenderBufferInitialDataView initialData) {
        if (const Result<void> valid = ValidateRenderBufferDescriptor(descriptor); valid.HasError())
            return valid;
        if (!initialData.bytes.empty() && initialData.bytes.size() != descriptor.byteSize)
            return Result<void>::Failure(MakeError(RenderResourceDescriptorErrors::BufferInitialDataInvalid));
        return Result<void>::Success();
    }

    /** @copydoc ValidateRenderTextureInitialData */
    Result<void> ValidateRenderTextureInitialData(const RenderTextureDescriptor &descriptor,
                                                  const RenderTextureInitialDataView initialData) {
        if (const Result<void> valid = ValidateRenderTextureDescriptor(descriptor); valid.HasError())
            return valid;
        if (initialData.subresources.empty())
            return Result<void>::Success();
        std::size_t subresourceCount{};
        if (!CheckedMultiply(descriptor.mipCount, descriptor.layerCount, subresourceCount) ||
            subresourceCount > MaximumInitialTextureSubresources || initialData.subresources.size() != subresourceCount)
            return InitialDataFailure();

        const std::size_t bytesPerTexel = BytesPerTexel(descriptor.format);
        for (std::size_t index = 0; index < initialData.subresources.size(); ++index) {
            const auto &subresource = initialData.subresources[index];
            const std::uint32_t expectedLayer = static_cast<std::uint32_t>(index / descriptor.mipCount);
            const std::uint32_t expectedMip = static_cast<std::uint32_t>(index % descriptor.mipCount);
            if (subresource.arrayLayer != expectedLayer || subresource.mipLevel != expectedMip)
                return InitialDataFailure();

            const auto mipExtent = [expectedMip](const std::uint32_t extent) -> std::size_t {
                if (expectedMip >= std::numeric_limits<std::uint32_t>::digits)
                    return 1;
                return std::max<std::size_t>(1, extent >> expectedMip);
            };
            const std::size_t width = mipExtent(descriptor.extent.width);
            const std::size_t height = mipExtent(descriptor.extent.height);
            const std::size_t depth = mipExtent(descriptor.depth);
            std::size_t minimumRowPitch{};
            std::size_t minimumSlicePitch{};
            std::size_t requiredBytes{};
            if (!CheckedMultiply(width, bytesPerTexel, minimumRowPitch) ||
                !CheckedMultiply(subresource.rowPitchBytes, height, minimumSlicePitch) ||
                !CheckedMultiply(subresource.slicePitchBytes, depth, requiredBytes) || subresource.rowPitchBytes < minimumRowPitch ||
                subresource.slicePitchBytes < minimumSlicePitch || subresource.bytes.size() != requiredBytes)
                return InitialDataFailure();
        }
        return Result<void>::Success();
    }

    /** @copydoc ValidateRenderTextureViewCompatibility */
    Result<void> ValidateRenderTextureViewCompatibility(const RenderTextureDescriptor &texture, const RenderTextureViewDescriptor &view) {
        if (const Result<void> validTexture = ValidateRenderTextureDescriptor(texture); validTexture.HasError())
            return validTexture;
        if (const Result<void> validView = ValidateRenderTextureViewDescriptor(view); validView.HasError())
            return validView;
        const bool mipRangeValid = view.baseMip < texture.mipCount && view.mipCount <= texture.mipCount - view.baseMip;
        const bool layerRangeValid = view.baseLayer < texture.layerCount && view.layerCount <= texture.layerCount - view.baseLayer;
        if (!AreViewFormatsCompatible(texture.format, view.format) || !IsAspectCompatible(texture.format, view.aspect) ||
            !IsViewDimensionCompatible(texture, view) || !mipRangeValid || !layerRangeValid)
            return Result<void>::Failure(MakeError(RenderResourceDescriptorErrors::TextureViewIncompatible));
        return Result<void>::Success();
    }
}  // namespace Horo::Render
