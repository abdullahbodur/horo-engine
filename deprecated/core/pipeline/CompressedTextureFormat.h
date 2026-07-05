/** @file CompressedTextureFormat.h
 *  @brief GPU-compressed texture format enumeration and block-level metadata.
 *
 *  Defines the block-compressed formats the texture pipeline can produce.
 *  Every format carries its block size (bytes) and block dimensions so that
 *  consumers can compute buffer sizes and pixel-to-block mappings without
 *  branching on every format.
 */
#pragma once

#include <cstdint>

namespace Horo::Pipeline {

/** @brief GPU-native block-compressed texture formats.
 *
 *  The pipeline cooks source images into one of these formats during offline
 *  processing.  Runtime uploads pass the compressed data straight to the GPU
 *  via glCompressedTexImage2D or equivalent without further decoding. */
enum class CompressedFormat : uint8_t {
    /** DXT1 / BC1 — RGB, 4 bits per pixel, 8 bytes per 4×4 block.
     *  One-bit alpha via the colour-key bit. */
    BC1_RGB = 0,

    /** DXT1 / BC1 with 1-bit punch-through alpha. */
    BC1_RGBA = 1,

    /** DXT5 / BC3 — RGBA, 8 bits per pixel, 16 bytes per 4×4 block.
     *  Interpolated alpha channel. */
    BC3_RGBA = 2,

    /** BC4 / ATI1 — single-channel (R), 4 bits per pixel, 8 bytes per 4×4 block.
     *  Ideal for roughness, metallic, or height maps. */
    BC4_R = 3,

    /** BC5 / ATI2 — two-channel (RG), 8 bits per pixel, 16 bytes per 4×4 block.
     *  Standard for tangent-space normal maps. */
    BC5_RG = 4,

    /** BC7 — RGBA, 8 bits per pixel, 16 bytes per 4×4 block.
     *  Highest quality block-compressed format for colour data.
     *  Requires OpenGL 4.2+ / D3D 11+. */
    BC7_RGBA = 5,

    /** Sentinel — must be last. */
    Count
};

/** @brief Block-level metadata for one compressed format. */
struct CompressedFormatInfo {
    CompressedFormat format;      /**< The format this info describes. */
    uint32_t        blockBytes;   /**< Bytes per compressed block. */
    uint32_t        blockWidth;   /**< Pixels per block (X). */
    uint32_t        blockHeight;  /**< Pixels per block (Y). */
    bool            hasAlpha;     /**< True when the format carries an alpha channel. */
    const char*     name;         /**< Human-readable name (e.g. "BC7_RGBA"). */
};

/** @brief Returns the metadata for a given compressed format. */
const CompressedFormatInfo& GetCompressedFormatInfo(CompressedFormat format);

/** @brief Computes the byte size of the compressed data for one mip level.
 *
 *  @param format       Target compression format.
 *  @param width        Mip-level width in pixels.
 *  @param height       Mip-level height in pixels.
 *  @return Total bytes for the compressed level, or 0 if width/height is 0. */
uint32_t CompressedMipSize(CompressedFormat format,
                           uint32_t width,
                           uint32_t height);

/** @brief Computes the number of mip levels for a power-of-two texture.
 *
 *  @param width   Top-level width.
 *  @param height  Top-level height.
 *  @return Number of mip levels (including the base level).  Minimum 1. */
uint32_t ComputeMipLevels(uint32_t width, uint32_t height);

} // namespace Horo::Pipeline
