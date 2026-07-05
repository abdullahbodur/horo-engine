/** @file TextureCooker.cpp
 *  @brief Offline texture cooking: STB decode, mipmap generation, and
 *         GPU block-compression (BC1/BC3/BC4/BC5/BC7).
 */

// stb_dxt implementation — must come before the stb_dxt include.
#define STB_DXT_IMPLEMENTATION

#include "core/pipeline/TextureCooker.h"

#include <cstring>

#include <stb_image.h>
#include <stb_dxt.h>

// bc7enc is C; wrap the include in extern "C".
extern "C" {
#include "bc7enc.h"
}

#include <algorithm>
#include <memory>

#include "core/Logger.h"

namespace Horo::Pipeline {

// =========================================================================
//  Internal helpers
// =========================================================================

namespace {

/** @brief One-time bc7enc library initialisation.
 *
 *  bc7enc_compress_block_init() allocates internal tables and must be called
 *  exactly once before any bc7enc_compress_block() call. */
void EnsureBc7EncInit() {
    static bool s_initialised = false;
    if (!s_initialised) {
        bc7enc_compress_block_init();
        s_initialised = true;
    }
}

// ---- Mipmap generation (simple 2×2 box filter) --------------------------

/** @brief Downsamples a source RGBA8 image to half resolution.
 *
 *  Each output pixel is the average of a 2×2 source block.
 *  Handles odd-dimension images by replicating the last row/column.
 *
 *  @param src     Source RGBA8 data, row-major, `srcW * srcH * 4` bytes.
 *  @param srcW    Source width in pixels.
 *  @param srcH    Source height in pixels.
 *  @param dst     Destination buffer, must hold `((srcW+1)/2) * ((srcH+1)/2) * 4` bytes. */
void DownsampleBox2x2(const uint8_t* src,
                      uint32_t srcW, uint32_t srcH,
                      uint8_t* dst) {
    const uint32_t dstW = (srcW + 1) / 2;
    const uint32_t dstH = (srcH + 1) / 2;

    for (uint32_t dy = 0; dy < dstH; ++dy) {
        for (uint32_t dx = 0; dx < dstW; ++dx) {
            // 2×2 source pixel indices, clamped to avoid out-of-bounds.
            const uint32_t sx0 = dx * 2;
            const uint32_t sy0 = dy * 2;
            const uint32_t sx1 = (sx0 + 1 < srcW) ? sx0 + 1 : sx0;
            const uint32_t sy1 = (sy0 + 1 < srcH) ? sy0 + 1 : sy0;

            // Fetch four (possibly overlapping at edges) pixels.
            const size_t row0 = static_cast<size_t>(sy0) * srcW;
            const size_t row1 = static_cast<size_t>(sy1) * srcW;
            const uint8_t* p00 = src + (row0 + sx0) * 4;
            const uint8_t* p10 = src + (row0 + sx1) * 4;
            const uint8_t* p01 = src + (row1 + sx0) * 4;
            const uint8_t* p11 = src + (row1 + sx1) * 4;

            // Average each channel across the 2×2 block.
            uint8_t* d = dst + (static_cast<size_t>(dy) * dstW + dx) * 4;
            for (int c = 0; c < 4; ++c) {
                const uint32_t sum = static_cast<uint32_t>(p00[c]) +
                                     static_cast<uint32_t>(p10[c]) +
                                     static_cast<uint32_t>(p01[c]) +
                                     static_cast<uint32_t>(p11[c]);
                d[c] = static_cast<uint8_t>(sum / 4);
            }
        }
    }
}

// ---- Block compression dispatchers -------------------------------------

/** @brief Compresses one 4×4 RGBA block to BC1 (8 bytes, no alpha). */
void CompressBC1Block(uint8_t* dst, const uint8_t* srcRGBA) {
    stb_compress_dxt_block(dst, srcRGBA, 0, STB_DXT_HIGHQUAL);
}

/** @brief Compresses one 4×4 RGBA block to BC3 (16 bytes, interpolated alpha). */
void CompressBC3Block(uint8_t* dst, const uint8_t* srcRGBA) {
    stb_compress_dxt_block(dst, srcRGBA, 1, STB_DXT_HIGHQUAL);
}

/** @brief Compresses one 4×4 single-channel block to BC4 (8 bytes).
 *
 *  BC4 uses the same compression logic as BC1's colour channel, applied to
 *  a single scalar.  stb_dxt provides stb_compress_bc4_block for this. */
void CompressBC4Block(uint8_t* dst, const uint8_t* srcR) {
    stb_compress_bc4_block(dst, srcR);
}

/** @brief Compresses one 4×4 two-channel block to BC5 (16 bytes).
 *
 *  BC5 is two independent BC4 blocks — one for R, one for G. */
void CompressBC5Block(uint8_t* dst, const uint8_t* srcRG) {
    stb_compress_bc5_block(dst, srcRG);
}

/** @brief Compresses one 4×4 RGBA block to BC7 (16 bytes).
 *
 *  Uses bc7enc with default perceptual-weights settings. */
void CompressBC7Block(uint8_t* dst, const uint8_t* srcRGBA,
                      uint32_t uberLevel) {
    EnsureBc7EncInit();

    bc7enc_compress_block_params params;
    bc7enc_compress_block_params_init(&params);
    params.m_uber_level = uberLevel;

    bc7enc_compress_block(dst, srcRGBA, &params);
}

// ---- Whole-image compression routines -----------------------------------

/** @brief Signature for a per-block compression function. */
using BlockCompressor = void (*)(uint8_t* dst, const uint8_t* src);

/** @brief Compresses an entire image using a 4×4 block compressor.
 *
 *  Pads the source to the next block boundary when dimensions are not
 *  multiples of 4.  Padding replicates the last row/column.
 *
 *  @param src       Source pixel data (format depends on compressor).
 *  @param width     Image width in pixels.
 *  @param height    Image height in pixels.
 *  @param bpp       Bytes per source pixel (1 for BC4, 2 for BC5, 4 for BC1/3/7).
 *  @param blockBytes  Compressed bytes per block (8 for BC1/BC4, 16 for BC3/5/7).
 *  @param compress  Per-block compressor function.
 *  @param outData   Destination vector — resized to the compressed size. */
void CompressImage(const uint8_t* src,
                   uint32_t width, uint32_t height,
                   uint32_t bpp, uint32_t blockBytes,
                   BlockCompressor compress,
                   std::vector<uint8_t>& outData) {
    // Round dimensions up to block-aligned.
    const uint32_t blocksX = (width  + 3) / 4;
    const uint32_t blocksY = (height + 3) / 4;
    const uint32_t paddedW = blocksX * 4;
    const uint32_t paddedH = blocksY * 4;

    // Pad source if dimensions are not multiples of 4.
    std::vector<uint8_t> padded;
    const uint8_t* workSrc = src;
    if (paddedW != width || paddedH != height) {
        padded.resize(static_cast<size_t>(paddedW) * paddedH * bpp);
        for (uint32_t y = 0; y < paddedH; ++y) {
            const uint32_t sy = (y < height) ? y : height - 1;
            for (uint32_t x = 0; x < paddedW; ++x) {
                const uint32_t sx = (x < width) ? x : width - 1;
                const size_t dstOffset =
                    (static_cast<size_t>(y) * paddedW + x) * bpp;
                const size_t srcOffset =
                    (static_cast<size_t>(sy) * width + sx) * bpp;
                std::memcpy(padded.data() + dstOffset,
                            src + srcOffset, bpp);
            }
        }
        workSrc = padded.data();
    }

    outData.resize(static_cast<size_t>(blocksX) * blocksY * blockBytes);

    // Compress each 4×4 block.
    for (uint32_t by = 0; by < blocksY; ++by) {
        for (uint32_t bx = 0; bx < blocksX; ++bx) {
            const size_t srcOffset =
                (static_cast<size_t>(by) * 4 * paddedW + bx * 4) * bpp;
            const size_t dstOffset =
                (static_cast<size_t>(by) * blocksX + bx) * blockBytes;
            compress(outData.data() + dstOffset,
                     workSrc + srcOffset);
        }
    }
}

/** @brief Specialised version for BC7 that passes uberLevel. */
void CompressImageBC7(const uint8_t* srcRGBA,
                      uint32_t width, uint32_t height,
                      uint32_t uberLevel,
                      std::vector<uint8_t>& outData) {
    const uint32_t blockBytes = 16;
    const uint32_t bpp = 4;
    const uint32_t blocksX = (width  + 3) / 4;
    const uint32_t blocksY = (height + 3) / 4;
    const uint32_t paddedW = blocksX * 4;
    const uint32_t paddedH = blocksY * 4;

    std::vector<uint8_t> padded;
    const uint8_t* workSrc = srcRGBA;
    if (paddedW != width || paddedH != height) {
        padded.resize(static_cast<size_t>(paddedW) * paddedH * bpp);
        for (uint32_t y = 0; y < paddedH; ++y) {
            const uint32_t sy = (y < height) ? y : height - 1;
            for (uint32_t x = 0; x < paddedW; ++x) {
                const uint32_t sx = (x < width) ? x : width - 1;
                const size_t dstOffset =
                    (static_cast<size_t>(y) * paddedW + x) * bpp;
                const size_t srcOffset =
                    (static_cast<size_t>(sy) * width + sx) * bpp;
                std::memcpy(padded.data() + dstOffset,
                            srcRGBA + srcOffset, bpp);
            }
        }
        workSrc = padded.data();
    }

    outData.resize(static_cast<size_t>(blocksX) * blocksY * blockBytes);

    for (uint32_t by = 0; by < blocksY; ++by) {
        for (uint32_t bx = 0; bx < blocksX; ++bx) {
            const size_t srcOffset =
                (static_cast<size_t>(by) * 4 * paddedW + bx * 4) * bpp;
            const size_t dstOffset =
                (static_cast<size_t>(by) * blocksX + bx) * blockBytes;
            CompressBC7Block(outData.data() + dstOffset,
                             workSrc + srcOffset, uberLevel);
        }
    }
}

/** @brief Selects the appropriate block compressor for a format.
 *
 *  Returns nullptr for unsupported formats. */
BlockCompressor GetCompressor(CompressedFormat format,
                              uint32_t& outBpp,
                              uint32_t& outBlockBytes) {
    switch (format) {
        case CompressedFormat::BC1_RGB:
        case CompressedFormat::BC1_RGBA:
            outBpp = 4;
            outBlockBytes = 8;
            return &CompressBC1Block;

        case CompressedFormat::BC3_RGBA:
            outBpp = 4;
            outBlockBytes = 16;
            return &CompressBC3Block;

        case CompressedFormat::BC4_R:
            outBpp = 1;
            outBlockBytes = 8;
            return &CompressBC4Block;

        case CompressedFormat::BC5_RG:
            outBpp = 2;
            outBlockBytes = 16;
            return &CompressBC5Block;

        case CompressedFormat::BC7_RGBA:
            // BC7 uses a specialised path (needs uberLevel).
            outBpp = 4;
            outBlockBytes = 16;
            return nullptr; // caller must handle BC7 specially

        default:
            outBpp = 0;
            outBlockBytes = 0;
            return nullptr;
    }
}

/** @brief Extracts the R channel from RGBA data for BC4 compression. */
std::vector<uint8_t> ExtractRChannel(const uint8_t* rgba,
                                     uint32_t width, uint32_t height) {
    const size_t pixelCount = static_cast<size_t>(width) * height;
    std::vector<uint8_t> r(pixelCount);
    for (size_t i = 0; i < pixelCount; ++i) {
        r[i] = rgba[i * 4];  // R is first byte
    }
    return r;
}

/** @brief Extracts the RG channels from RGBA data for BC5 compression. */
std::vector<uint8_t> ExtractRGChannels(const uint8_t* rgba,
                                       uint32_t width, uint32_t height) {
    const size_t pixelCount = static_cast<size_t>(width) * height;
    std::vector<uint8_t> rg(pixelCount * 2);
    for (size_t i = 0; i < pixelCount; ++i) {
        rg[i * 2]     = rgba[i * 4];      // R
        rg[i * 2 + 1] = rgba[i * 4 + 1];  // G
    }
    return rg;
}

} // namespace

// =========================================================================
//  Public API
// =========================================================================

CookedTexture CookTextureFromFile(const std::string& path,
                                  const TextureCookSettings& settings) {
    CookedTexture result;

    stbi_set_flip_vertically_on_load(settings.flipY ? 1 : 0);
    int w = 0, h = 0, ch = 0;
    unsigned char* data = stbi_load(path.c_str(), &w, &h, &ch, 4);
    stbi_set_flip_vertically_on_load(0);

    if (!data) {
        result.error = std::string("Failed to load image: ") +
                       (stbi_failure_reason() ? stbi_failure_reason()
                                              : "unknown error");
        LogWarn("TextureCooker: {}", result.error);
        return result;
    }

    if (w <= 0 || h <= 0) {
        stbi_image_free(data);
        result.error = "Image has zero or negative dimensions";
        return result;
    }

    // Delegate to the RGBA entry point.
    result = CookTextureFromRGBA(data,
                                 static_cast<uint32_t>(w),
                                 static_cast<uint32_t>(h),
                                 settings);

    stbi_image_free(data);
    return result;
}

CookedTexture CookTextureFromRGBA(const uint8_t* rgba,
                                  uint32_t width,
                                  uint32_t height,
                                  const TextureCookSettings& settings) {
    CookedTexture result;
    result.format = settings.format;

    if (!rgba || width == 0 || height == 0) {
        result.error = "Invalid input: null data or zero dimensions";
        return result;
    }

    const uint32_t mipLevels = settings.generateMips
                                   ? ComputeMipLevels(width, height)
                                   : 1;

    // Validate the requested format.
    const auto& fmtInfo = GetCompressedFormatInfo(settings.format);
    (void)fmtInfo;

    // Total compressed data size across all mips.
    uint32_t totalSize = 0;
    uint32_t mipW = width;
    uint32_t mipH = height;
    for (uint32_t mip = 0; mip < mipLevels; ++mip) {
        totalSize += CompressedMipSize(settings.format, mipW, mipH);
        mipW = std::max(1u, mipW / 2);
        mipH = std::max(1u, mipH / 2);
    }

    result.data.reserve(totalSize);
    result.width  = width;
    result.height = height;
    result.mipLevels = mipLevels;

    // ---- Mip chain generation + compression ---------------------------

    // Source for the current mip level: starts with the original RGBA data.
    // We keep the previous level's RGBA data so we can downsample to the
    // next level.
    std::vector<uint8_t> currentRGBA;
    std::vector<uint8_t> nextRGBA;

    // Copy the input to currentRGBA (we need a mutable copy for BC4/BC5
    // channel extraction, and for downsampling).
    currentRGBA.assign(rgba, rgba + static_cast<size_t>(width) * height * 4);

    mipW = width;
    mipH = height;

    for (uint32_t mip = 0; mip < mipLevels; ++mip) {
        std::vector<uint8_t> mipData;

        // Special handling per format group.
        if (settings.format == CompressedFormat::BC7_RGBA) {
            CompressImageBC7(currentRGBA.data(), mipW, mipH,
                             settings.bc7UberLevel, mipData);
        } else if (settings.format == CompressedFormat::BC4_R) {
            auto rChan = ExtractRChannel(currentRGBA.data(), mipW, mipH);
            uint32_t bpp = 1, blockBytes = 8;
            CompressImage(rChan.data(), mipW, mipH,
                          bpp, blockBytes, CompressBC4Block, mipData);
        } else if (settings.format == CompressedFormat::BC5_RG) {
            auto rgChan = ExtractRGChannels(currentRGBA.data(), mipW, mipH);
            uint32_t bpp = 2, blockBytes = 16;
            CompressImage(rgChan.data(), mipW, mipH,
                          bpp, blockBytes, CompressBC5Block, mipData);
        } else {
            // BC1 / BC3 — uses the generic path.
            uint32_t bpp = 0, blockBytes = 0;
            auto compressor = GetCompressor(settings.format, bpp, blockBytes);
            if (!compressor) {
                result.error = "Unsupported compression format";
                result.data.clear();
                return result;
            }
            CompressImage(currentRGBA.data(), mipW, mipH,
                          bpp, blockBytes, compressor, mipData);
        }

        // Append compressed mip data.
        result.data.insert(result.data.end(),
                           mipData.begin(), mipData.end());

        // Generate next (smaller) mip level RGBA data.
        if (mip + 1 < mipLevels) {
            const uint32_t nextW = std::max(1u, mipW / 2);
            const uint32_t nextH = std::max(1u, mipH / 2);
            nextRGBA.resize(static_cast<size_t>(nextW) * nextH * 4);
            DownsampleBox2x2(currentRGBA.data(), mipW, mipH,
                             nextRGBA.data());
            currentRGBA.swap(nextRGBA);
            mipW = nextW;
            mipH = nextH;
        }
    }

    return result;
}

unsigned int CompressedFormatToGLInternalFormat(CompressedFormat format) {
    // OpenGL compressed texture internal-format enums.
    // These require the corresponding extension (e.g. GL_EXT_texture_compression_s3tc,
    // GL_ARB_texture_compression_bptc, etc.).
    switch (format) {
        case CompressedFormat::BC1_RGB:
            return 0x83F0;  // GL_COMPRESSED_RGB_S3TC_DXT1_EXT
        case CompressedFormat::BC1_RGBA:
            return 0x83F1;  // GL_COMPRESSED_RGBA_S3TC_DXT1_EXT
        case CompressedFormat::BC3_RGBA:
            return 0x83F3;  // GL_COMPRESSED_RGBA_S3TC_DXT5_EXT
        case CompressedFormat::BC4_R:
            return 0x8DBB;  // GL_COMPRESSED_RED_RGTC1
        case CompressedFormat::BC5_RG:
            return 0x8DBD;  // GL_COMPRESSED_RG_RGTC2
        case CompressedFormat::BC7_RGBA:
            return 0x8E8C;  // GL_COMPRESSED_RGBA_BPTC_UNORM
        default:
            return 0;
    }
}

} // namespace Horo::Pipeline
