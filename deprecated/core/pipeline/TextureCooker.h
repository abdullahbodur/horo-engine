/** @file TextureCooker.h
 *  @brief Offline texture cooking pipeline: STB decode → GPU-compressed encode.
 *
 *  The TextureCooker converts source images (PNG, JPEG, TGA, BMP, etc.) into
 *  block-compressed GPU formats suitable for direct upload via
 *  glCompressedTexImage2D or equivalent.  It depends on no GUI, editor, or
 *  window-system libraries — only stb_image, stb_dxt, and bc7enc.
 *
 *  Typical workflow:
 *  @code
 *  Horo::Pipeline::TextureCookSettings settings;
 *  settings.format = Horo::Pipeline::CompressedFormat::BC7_RGBA;
 *  settings.generateMips = true;
 *
 *  auto result = Horo::Pipeline::CookTextureFromFile("hero.png", settings);
 *  if (result) {
 *      // result.data contains compressed mip chain
 *      // result.format, .width, .height, .mipLevels describe the output
 *      glCompressedTexImage2D(GL_TEXTURE_2D, level,
 *                             GL_COMPRESSED_RGBA_BPTC_UNORM, // BC7
 *                             mipWidth, mipHeight, 0,
 *                             mipSize, mipData);
 *  }
 *  @endcode
 */
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/pipeline/CompressedTextureFormat.h"

namespace Horo::Pipeline {

/** @brief Configuration for one texture cook operation. */
struct TextureCookSettings {
    /** Target GPU-compressed format.  Default: BC7_RGBA (highest quality). */
    CompressedFormat format = CompressedFormat::BC7_RGBA;

    /** When true, generate a full mip chain.  Default: true. */
    bool generateMips = true;

    /** When true, flip the image vertically on load (OpenGL convention).
     *  Default: true. */
    bool flipY = true;

    /** BC7 quality: 0 (fast) to 4 (exhaustive).  Ignored for non-BC7 formats.
     *  Default: 0 (production-fast). */
    uint32_t bc7UberLevel = 0;
};

/** @brief Result of a texture cook operation.
 *
 *  On success, `data` holds the full compressed mip chain packed back-to-back
 *  with no padding between levels.  Use CompressedMipSize() to compute the
 *  offset and size of each level. */
struct CookedTexture {
    /** GPU-compressed format of the output data. */
    CompressedFormat format = CompressedFormat::BC7_RGBA;

    /** Top-level width in pixels. */
    uint32_t width = 0;

    /** Top-level height in pixels. */
    uint32_t height = 0;

    /** Number of mip levels (1 when generateMips is false). */
    uint32_t mipLevels = 0;

    /** Packed compressed mip chain.  Mip 0 first, no inter-level padding. */
    std::vector<uint8_t> data;

    /** Error message when cooking failed.  Empty on success. */
    std::string error;

    /** @brief True when cooking succeeded (data is non-empty and error is empty). */
    explicit operator bool() const { return error.empty() && !data.empty(); }
};

/** @brief Cooks a source image file into a GPU-compressed texture.
 *
 *  Loads the file via stb_image, optionally generates mipmaps, then compresses
 *  each level into the requested format.
 *
 *  @param path     Filesystem path to the source image (PNG, JPEG, TGA, BMP, etc.).
 *  @param settings Compression and mip-generation parameters.
 *  @return CookedTexture with compressed mip chain, or error on failure. */
CookedTexture CookTextureFromFile(const std::string& path,
                                  const TextureCookSettings& settings);

/** @brief Cooks raw RGBA8 pixel data into a GPU-compressed texture.
 *
 *  The caller owns the pixel buffer.  Mipmaps are generated from this data
 *  when settings.generateMips is true.
 *
 *  @param rgba     Pointer to RGBA8 pixel data (R first in memory, row-major).
 *  @param width    Image width in pixels.
 *  @param height   Image height in pixels.
 *  @param settings Compression and mip-generation parameters.
 *  @return CookedTexture with compressed mip chain, or error on failure. */
CookedTexture CookTextureFromRGBA(const uint8_t* rgba,
                                  uint32_t width,
                                  uint32_t height,
                                  const TextureCookSettings& settings);

/** @brief Returns the OpenGL internal-format enum for a CompressedFormat.
 *
 *  Useful for mapping cooker output to glCompressedTexImage2D calls.
 *
 *  @param format  The compressed format produced by the cooker.
 *  @return GLenum suitable for glCompressedTexImage2D, or 0 if unknown. */
unsigned int CompressedFormatToGLInternalFormat(CompressedFormat format);

} // namespace Horo::Pipeline
