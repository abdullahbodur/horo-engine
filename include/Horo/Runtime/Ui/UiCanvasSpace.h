#pragma once

/**
 * @file UiCanvasSpace.h
 * @brief Backend-neutral Runtime UI canvas modes and deterministic screen-space resolution.
 */

#include "Horo/Runtime/Ui/UiIdentity.h"

#include <compare>
#include <cstdint>
#include <limits>

namespace Horo::Runtime::Ui {
    /** @brief Largest whole-DIP reference axis representable as signed 1/64-DIP layout geometry. */
    inline constexpr std::uint32_t MaximumUiCanvasReferenceDip = static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max() / 64);

    /** @brief Closed semantic canvas projection mode; native renderer types are deliberately absent. */
    enum class UiRenderMode : std::uint8_t {
        ScreenSpaceOverlay,
        ScreenSpaceCamera,
        WorldSpace,
    };

    /** @brief Closed screen scaling policy defined by the Runtime UI architecture. */
    enum class UiScaleMode : std::uint8_t {
        ScaleWithScreenSize,
        ConstantPixelSize,
        ConstantPhysicalSize,
    };

    /** @brief Positive authored reference resolution in whole logical DIPs. */
    struct UiCanvasReferenceResolution final {
        std::uint32_t width{1920};  /**< Positive horizontal reference DIPs. */
        std::uint32_t height{1080}; /**< Positive vertical reference DIPs. */

        /** @brief Checks that both axes fit the canonical 1/64-DIP domain. @return Whether the resolution is valid. */
        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] auto operator<=>(const UiCanvasReferenceResolution &) const noexcept = default;
    };

    /** @brief Authored canvas identity, root, projection mode, and screen scaling policy. */
    struct UiCanvasDescriptor final {
        UiCanvasId id;                                             /**< Stable identity of the canvas within the document. */
        UiElementId rootElement;                                   /**< Stable identity of the canvas root element. */
        UiRenderMode renderMode{UiRenderMode::ScreenSpaceOverlay}; /**< Semantic projection mode. */
        UiCanvasReferenceResolution referenceResolution{};         /**< Authored logical design resolution. */
        UiScaleMode scaleMode{UiScaleMode::ScaleWithScreenSize};   /**< Screen-space scaling policy. */

        /** @brief Checks identities, enum values, and reference bounds. @return Whether the descriptor is valid. */
        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] auto operator<=>(const UiCanvasDescriptor &) const noexcept = default;
    };

    /** @brief Positive physical pixel extent supplied by a viewport/output owner. */
    struct UiCanvasPixelExtent final {
        std::uint32_t width{};  /**< Physical viewport width in pixels. */
        std::uint32_t height{}; /**< Physical viewport height in pixels. */

        /** @brief Checks that both physical axes are non-zero. @return Whether the extent is valid. */
        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] auto operator<=>(const UiCanvasPixelExtent &) const noexcept = default;
    };

    /** @brief Reduced positive rational pixel scale supplied by the output-policy owner. */
    struct UiCanvasDeviceScale final {
        std::uint32_t pixelUnits{};  /**< Positive physical pixel units when evidence is present. */
        std::uint32_t logicalDips{}; /**< Positive logical DIP units when evidence is present. */

        /** @brief Checks positive scale evidence. @return Whether the evidence is valid. */
        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] auto operator<=>(const UiCanvasDeviceScale &) const noexcept = default;
    };

    /** @brief Resolved non-negative logical canvas extent in deterministic 1/64-DIP units. */
    struct UiCanvasLogicalExtent final {
        std::int32_t width{};  /**< Horizontal logical extent in 1/64 DIP. */
        std::int32_t height{}; /**< Vertical logical extent in 1/64 DIP. */

        [[nodiscard]] auto operator<=>(const UiCanvasLogicalExtent &) const noexcept = default;
    };

    /** @brief Allocation-free screen-space projection resolved from immutable caller evidence. */
    struct UiResolvedScreenCanvas final {
        UiCanvasLogicalExtent logicalExtent; /**< Complete logical viewport visible to layout. */
        UiCanvasPixelExtent pixelExtent;     /**< Exact physical viewport evidence consumed. */
        UiCanvasDeviceScale pixelsPerDip;    /**< Reduced uniform physical-pixels-per-DIP ratio. */

        [[nodiscard]] auto operator<=>(const UiResolvedScreenCanvas &) const noexcept = default;
    };

    /**
     * @brief Resolves a screen overlay/camera canvas without renderer, platform, or editor objects.
     * @details ScaleWithScreenSize uses the smaller width/height ratio, preserving authored aspect while exposing any additional
     *          logical extent. ConstantPhysicalSize consumes already-resolved scale evidence; this function does not own DPI,
     *          font-scale, safe-area, or pixel-snapping policy.
     * @param canvas Valid authored screen-space canvas descriptor.
     * @param viewport Exact positive physical viewport extent after any caller-owned inset policy.
     * @param deviceScale Caller-owned physical scale evidence required only by ConstantPhysicalSize and ignored otherwise.
     * @return Deterministic resolved metrics, or a typed malformed/mode/overflow error.
     */
    [[nodiscard]] Result<UiResolvedScreenCanvas> ResolveUiScreenCanvas(const UiCanvasDescriptor &canvas, UiCanvasPixelExtent viewport,
                                                                       UiCanvasDeviceScale deviceScale = {});

    /**
     * @brief Resolves the authored logical extent of a world-space canvas for headless layout.
     * @details Camera projection and device pixels remain Renderer-owned and are intentionally not fabricated here.
     * @param canvas Valid authored world-space canvas descriptor.
     * @return Exact 1/64-DIP logical extent, or a typed malformed/mode error.
     */
    [[nodiscard]] Result<UiCanvasLogicalExtent> ResolveUiWorldCanvas(const UiCanvasDescriptor &canvas);
}  // namespace Horo::Runtime::Ui
