/** @file CompressedTextureFormat.cpp
 *  @brief Format metadata table and mip-size helpers for GPU-compressed textures.
 */
#include "core/pipeline/CompressedTextureFormat.h"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <iterator>

namespace Horo::Pipeline {

namespace {

/** @brief Static lookup table for every CompressedFormat value. */
constexpr CompressedFormatInfo kFormatTable[] = {
    { CompressedFormat::BC1_RGB,  8,  4, 4, false, "BC1_RGB"  },
    { CompressedFormat::BC1_RGBA, 8,  4, 4, true,  "BC1_RGBA" },
    { CompressedFormat::BC3_RGBA, 16, 4, 4, true,  "BC3_RGBA" },
    { CompressedFormat::BC4_R,    8,  4, 4, false, "BC4_R"    },
    { CompressedFormat::BC5_RG,   16, 4, 4, false, "BC5_RG"   },
    { CompressedFormat::BC7_RGBA, 16, 4, 4, true,  "BC7_RGBA" },
};

static_assert(std::size(kFormatTable) ==
              static_cast<size_t>(CompressedFormat::Count),
              "kFormatTable must have one entry per CompressedFormat");

} // namespace

const CompressedFormatInfo& GetCompressedFormatInfo(CompressedFormat format) {
    const auto idx = static_cast<size_t>(format);
    if (idx >= std::size(kFormatTable)) {
        // Safety net — never return a dangling reference.
        return kFormatTable[0];
    }
    return kFormatTable[idx];
}

uint32_t CompressedMipSize(CompressedFormat format,
                           uint32_t width,
                           uint32_t height) {
    if (width == 0 || height == 0) return 0;

    const auto& info = GetCompressedFormatInfo(format);

    // Round dimensions up to the next block boundary.
    const uint32_t blocksX = (width  + info.blockWidth  - 1) / info.blockWidth;
    const uint32_t blocksY = (height + info.blockHeight - 1) / info.blockHeight;

    return blocksX * blocksY * info.blockBytes;
}

uint32_t ComputeMipLevels(uint32_t width, uint32_t height) {
    const uint32_t maxDim = std::max(width, height);
    if (maxDim == 0) return 1;

    // Number of times we can halve until we reach 1.
    return 1 + static_cast<uint32_t>(std::bit_width(maxDim) - 1);
}

} // namespace Horo::Pipeline
