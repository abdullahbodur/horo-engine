#pragma once

/**
 * @file AssetPreview.h
 * @brief Rendering-neutral asset-card preview contribution contract.
 */

#include "Horo/Assets/AssetId.h"
#include "Horo/Foundation/CancellationToken.h"
#include "Horo/Foundation/Result.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace Horo::Assets {
    /** @brief Host fallback selected when an importer does not provide a usable preview image. */
    enum class AssetPreviewFallback : std::uint8_t {
        Automatic,
        Mesh,
        Image,
        Audio,
        Generic,
    };

    /** @brief Borrowed bounded input passed to one asset preview provider invocation. */
    struct AssetPreviewInput {
        std::span<const std::uint8_t> editorPayload; /**< Immutable imported editor payload. */
        std::string_view absoluteAssetPath;          /**< Absolute source path for diagnostics only. */
        AssetTypeId assetType;                       /**< Stable type produced by the importer. */
        std::uint32_t width{128};                    /**< Requested output width in pixels. */
        std::uint32_t height{128};                   /**< Requested output height in pixels. */
    };

    /** @brief Owned tightly packed RGBA8 image returned by a preview provider. */
    struct AssetPreviewImage {
        std::uint32_t width{};
        std::uint32_t height{};
        std::vector<std::uint8_t> pixels;

        /** @brief Reports whether dimensions and tightly packed pixel storage agree. */
        [[nodiscard]] bool IsValid() const noexcept {
            return width > 0 && height > 0 && pixels.size() == static_cast<std::size_t>(width) * height * 4U;
        }
    };

    /**
     * @brief Module-supplied strategy that creates one editor asset-card preview.
     *
     * The provider owns preview interpretation and pixels but never receives an
     * ImGui or renderer callback. The host owns scheduling, validation, texture
     * upload, caching, fallback presentation, and resource destruction.
     */
    class IAssetPreviewProvider {
    public:
        virtual ~IAssetPreviewProvider() = default;

        /**
         * @brief Produces a bounded RGBA8 image for an imported editor payload.
         * @param input Borrowed payload, absolute diagnostic path, type, and requested dimensions.
         * @param cancellation Host-owned cooperative cancellation token.
         * @return Owned preview image or a typed failure that selects the host fallback.
         */
        [[nodiscard]] virtual Result<AssetPreviewImage> GeneratePreview(const AssetPreviewInput &input,
                                                                        const CancellationToken &cancellation) const = 0;
    };
}  // namespace Horo::Assets
