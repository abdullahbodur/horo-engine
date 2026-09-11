#include "Horo/Runtime/Ui/UiCanvasSpace.h"

#include "Horo/Runtime/Ui/UiErrors.h"

#include <limits>
#include <numeric>
#include <utility>

namespace Horo::Runtime::Ui {
    namespace {
        template <typename T> [[nodiscard]] Result<T> Failure(const ErrorCodeDescriptor &descriptor) {
            return Result<T>::Failure(MakeError(descriptor));
        }

        [[nodiscard]] bool IsRenderMode(const UiRenderMode mode) noexcept {
            using enum UiRenderMode;
            return mode == ScreenSpaceOverlay || mode == ScreenSpaceCamera || mode == WorldSpace;
        }

        [[nodiscard]] bool IsScaleMode(const UiScaleMode mode) noexcept {
            using enum UiScaleMode;
            return mode == ScaleWithScreenSize || mode == ConstantPixelSize || mode == ConstantPhysicalSize;
        }

        [[nodiscard]] UiCanvasDeviceScale ReducedScale(std::uint32_t numerator, std::uint32_t denominator) noexcept {
            const std::uint32_t divisor = std::gcd(numerator, denominator);
            return {numerator / divisor, denominator / divisor};
        }

        [[nodiscard]] Result<std::int32_t> ResolveAxis(const std::uint32_t pixels, const UiCanvasDeviceScale scale) noexcept {
            constexpr std::uint64_t UnitsPerDip = 64;
            const std::uint64_t scaledPixels = static_cast<std::uint64_t>(pixels) * scale.logicalDips;
            if (scaledPixels > std::numeric_limits<std::uint64_t>::max() / UnitsPerDip)
                return Failure<std::int32_t>(UiErrors::CanvasSpaceOverflow);
            const std::uint64_t numerator = scaledPixels * UnitsPerDip;
            const std::uint64_t quotient = numerator / scale.pixelUnits;
            const std::uint64_t remainder = numerator % scale.pixelUnits;
            const std::uint64_t halfway = scale.pixelUnits / 2;
            const bool aboveHalf = remainder > halfway;
            const bool exactHalf = (scale.pixelUnits % 2 == 0) && remainder == halfway;
            const std::uint64_t rounded = quotient + static_cast<std::uint64_t>(aboveHalf || (exactHalf && (quotient % 2 != 0)));
            if (rounded > static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max()))
                return Failure<std::int32_t>(UiErrors::CanvasSpaceOverflow);
            return Result<std::int32_t>::Success(static_cast<std::int32_t>(rounded));
        }
    }  // namespace

    /** @copydoc UiCanvasReferenceResolution::IsValid */
    bool UiCanvasReferenceResolution::IsValid() const noexcept {
        return width > 0 && height > 0 && width <= MaximumUiCanvasReferenceDip && height <= MaximumUiCanvasReferenceDip;
    }

    /** @copydoc UiCanvasDescriptor::IsValid */
    bool UiCanvasDescriptor::IsValid() const noexcept {
        return id.IsValid() && rootElement.IsValid() && IsRenderMode(renderMode) && referenceResolution.IsValid() && IsScaleMode(scaleMode);
    }

    /** @copydoc UiCanvasPixelExtent::IsValid */
    bool UiCanvasPixelExtent::IsValid() const noexcept {
        return width > 0 && height > 0;
    }

    /** @copydoc UiCanvasDeviceScale::IsValid */
    bool UiCanvasDeviceScale::IsValid() const noexcept {
        return pixelUnits > 0 && logicalDips > 0;
    }

    /** @copydoc ResolveUiScreenCanvas */
    Result<UiResolvedScreenCanvas> ResolveUiScreenCanvas(const UiCanvasDescriptor &canvas, const UiCanvasPixelExtent viewport,
                                                         const UiCanvasDeviceScale deviceScale) {
        if (!canvas.IsValid() || !viewport.IsValid())
            return Failure<UiResolvedScreenCanvas>(UiErrors::CanvasSpaceInvalid);
        if (canvas.renderMode == UiRenderMode::WorldSpace)
            return Failure<UiResolvedScreenCanvas>(UiErrors::CanvasSpaceModeMismatch);

        using enum UiScaleMode;
        UiCanvasDeviceScale scale;
        switch (canvas.scaleMode) {
            case ScaleWithScreenSize: {
                const std::uint64_t widthCross = static_cast<std::uint64_t>(viewport.width) * canvas.referenceResolution.height;
                const std::uint64_t heightCross = static_cast<std::uint64_t>(viewport.height) * canvas.referenceResolution.width;
                scale = widthCross <= heightCross ? ReducedScale(viewport.width, canvas.referenceResolution.width)
                                                  : ReducedScale(viewport.height, canvas.referenceResolution.height);
                break;
            }
            case ConstantPixelSize:
                scale = {1, 1};
                break;
            case ConstantPhysicalSize:
                if (!deviceScale.IsValid())
                    return Failure<UiResolvedScreenCanvas>(UiErrors::CanvasSpaceInvalid);
                scale = ReducedScale(deviceScale.pixelUnits, deviceScale.logicalDips);
                break;
        }

        auto width = ResolveAxis(viewport.width, scale);
        if (width.HasError())
            return Result<UiResolvedScreenCanvas>::Failure(width.ErrorValue());
        auto height = ResolveAxis(viewport.height, scale);
        if (height.HasError())
            return Result<UiResolvedScreenCanvas>::Failure(height.ErrorValue());
        return Result<UiResolvedScreenCanvas>::Success({{std::move(width).Value(), std::move(height).Value()}, viewport, scale});
    }

    /** @copydoc ResolveUiWorldCanvas */
    Result<UiCanvasLogicalExtent> ResolveUiWorldCanvas(const UiCanvasDescriptor &canvas) {
        if (!canvas.IsValid())
            return Failure<UiCanvasLogicalExtent>(UiErrors::CanvasSpaceInvalid);
        if (canvas.renderMode != UiRenderMode::WorldSpace)
            return Failure<UiCanvasLogicalExtent>(UiErrors::CanvasSpaceModeMismatch);
        return Result<UiCanvasLogicalExtent>::Success({static_cast<std::int32_t>(canvas.referenceResolution.width * 64U),
                                                       static_cast<std::int32_t>(canvas.referenceResolution.height * 64U)});
    }
}  // namespace Horo::Runtime::Ui
