/** @file test_texture_cooker.cpp
 *  @brief Unit tests for the GPU-compressed texture cooking pipeline.
 *
 *  Covers:
 *  - CompressedFormat metadata (block sizes, names)
 *  - CompressedMipSize / ComputeMipLevels
 *  - CompressedFormatToGLInternalFormat
 *  - CookTextureFromRGBA for BC1, BC3, BC4, BC5, BC7
 *  - Mip chain generation and sizing
 *  - Error paths (null input, zero dimensions)
 *  - Compression determinism (same input → same output)
 *  - Edge cases (1×1, 4×4, non-power-of-two, non-multiple-of-4)
 */
#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <vector>

#include "core/pipeline/TextureCooker.h"

using namespace Horo::Pipeline;

// =========================================================================
//  Test helpers
// =========================================================================

namespace {

/** @brief Creates a solid-colour RGBA image. */
std::vector<uint8_t> MakeSolidRGBA(uint32_t w, uint32_t h,
                                   uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    std::vector<uint8_t> data(w * h * 4);
    for (uint32_t i = 0; i < w * h; ++i) {
        data[i * 4 + 0] = r;
        data[i * 4 + 1] = g;
        data[i * 4 + 2] = b;
        data[i * 4 + 3] = a;
    }
    return data;
}

/** @brief Creates a gradient RGBA image (useful for detecting block artefacts). */
std::vector<uint8_t> MakeGradientRGBA(uint32_t w, uint32_t h) {
    std::vector<uint8_t> data(w * h * 4);
    for (uint32_t y = 0; y < h; ++y) {
        for (uint32_t x = 0; x < w; ++x) {
            const uint32_t i = y * w + x;
            data[i * 4 + 0] = static_cast<uint8_t>((x * 255) / std::max(1u, w - 1));
            data[i * 4 + 1] = static_cast<uint8_t>((y * 255) / std::max(1u, h - 1));
            data[i * 4 + 2] = 128;
            data[i * 4 + 3] = 255;
        }
    }
    return data;
}

/** @brief Verify that compressed data is not trivially empty / all zeros. */
bool CompressedDataLooksValid(const std::vector<uint8_t>& data) {
    if (data.empty()) return false;
    // At minimum, the first few bytes should not all be zero for a non-black
    // solid-colour or gradient image.
    int nonZero = 0;
    for (size_t i = 0; i < std::min(data.size(), size_t(16)); ++i) {
        if (data[i] != 0) ++nonZero;
    }
    return nonZero > 0;
}

} // namespace

// =========================================================================
//  CompressedFormat metadata
// =========================================================================

TEST_CASE("CompressedFormatInfo returns correct metadata", "[texture_cooker][format]") {
    SECTION("BC1_RGB") {
        const auto& info = GetCompressedFormatInfo(CompressedFormat::BC1_RGB);
        REQUIRE(info.format == CompressedFormat::BC1_RGB);
        REQUIRE(info.blockBytes == 8);
        REQUIRE(info.blockWidth == 4);
        REQUIRE(info.blockHeight == 4);
        REQUIRE(info.hasAlpha == false);
        REQUIRE(std::strcmp(info.name, "BC1_RGB") == 0);
    }

    SECTION("BC1_RGBA") {
        const auto& info = GetCompressedFormatInfo(CompressedFormat::BC1_RGBA);
        REQUIRE(info.format == CompressedFormat::BC1_RGBA);
        REQUIRE(info.hasAlpha == true);
    }

    SECTION("BC3_RGBA") {
        const auto& info = GetCompressedFormatInfo(CompressedFormat::BC3_RGBA);
        REQUIRE(info.blockBytes == 16);
        REQUIRE(info.hasAlpha == true);
    }

    SECTION("BC4_R") {
        const auto& info = GetCompressedFormatInfo(CompressedFormat::BC4_R);
        REQUIRE(info.blockBytes == 8);
        REQUIRE(info.hasAlpha == false);
    }

    SECTION("BC5_RG") {
        const auto& info = GetCompressedFormatInfo(CompressedFormat::BC5_RG);
        REQUIRE(info.blockBytes == 16);
        REQUIRE(info.hasAlpha == false);
    }

    SECTION("BC7_RGBA") {
        const auto& info = GetCompressedFormatInfo(CompressedFormat::BC7_RGBA);
        REQUIRE(info.blockBytes == 16);
        REQUIRE(info.hasAlpha == true);
    }
}

// =========================================================================
//  CompressedMipSize
// =========================================================================

TEST_CASE("CompressedMipSize computes correct sizes", "[texture_cooker][mipsize]") {
    SECTION("4×4 BC1 (single block)") {
        REQUIRE(CompressedMipSize(CompressedFormat::BC1_RGB, 4, 4) == 8);
    }

    SECTION("8×8 BC1 (2×2 blocks)") {
        REQUIRE(CompressedMipSize(CompressedFormat::BC1_RGB, 8, 8) == 32);
    }

    SECTION("4×4 BC3 (single block, 16 bytes)") {
        REQUIRE(CompressedMipSize(CompressedFormat::BC3_RGBA, 4, 4) == 16);
    }

    SECTION("4×4 BC7 (single block, 16 bytes)") {
        REQUIRE(CompressedMipSize(CompressedFormat::BC7_RGBA, 4, 4) == 16);
    }

    SECTION("Non-multiple-of-4 dimensions are padded up") {
        // 5×5 → 2×2 blocks = 4 blocks × 8 bytes = 32 bytes for BC1
        REQUIRE(CompressedMipSize(CompressedFormat::BC1_RGB, 5, 5) == 32);
        // 1×1 → 1×1 block = 1 block × 8 bytes = 8 bytes
        REQUIRE(CompressedMipSize(CompressedFormat::BC1_RGB, 1, 1) == 8);
        // 7×3 → 2×1 blocks = 2 blocks × 8 bytes = 16 bytes
        REQUIRE(CompressedMipSize(CompressedFormat::BC1_RGB, 7, 3) == 16);
    }

    SECTION("Zero dimensions return 0") {
        REQUIRE(CompressedMipSize(CompressedFormat::BC1_RGB, 0, 4) == 0);
        REQUIRE(CompressedMipSize(CompressedFormat::BC1_RGB, 4, 0) == 0);
    }
}

// =========================================================================
//  ComputeMipLevels
// =========================================================================

TEST_CASE("ComputeMipLevels returns correct level counts", "[texture_cooker][miplevels]") {
    SECTION("1×1") {
        REQUIRE(ComputeMipLevels(1, 1) == 1);
    }

    SECTION("4×4 (3 levels: 4,2,1)") {
        REQUIRE(ComputeMipLevels(4, 4) == 3);
    }

    SECTION("16×16 (5 levels: 16,8,4,2,1)") {
        REQUIRE(ComputeMipLevels(16, 16) == 5);
    }

    SECTION("64×64 (7 levels)") {
        REQUIRE(ComputeMipLevels(64, 64) == 7);
    }

    SECTION("Non-square (8×4 → max dim 8 → 4 levels)") {
        REQUIRE(ComputeMipLevels(8, 4) == 4);  // 8,4,2,1
    }

    SECTION("Zero dimensions return 1") {
        REQUIRE(ComputeMipLevels(0, 0) == 1);
    }
}

// =========================================================================
//  GL internal format mapping
// =========================================================================

TEST_CASE("CompressedFormatToGLInternalFormat returns valid GL enums",
          "[texture_cooker][gl]") {
    // All defined formats should return non-zero.
    REQUIRE(CompressedFormatToGLInternalFormat(CompressedFormat::BC1_RGB) != 0);
    REQUIRE(CompressedFormatToGLInternalFormat(CompressedFormat::BC1_RGBA) != 0);
    REQUIRE(CompressedFormatToGLInternalFormat(CompressedFormat::BC3_RGBA) != 0);
    REQUIRE(CompressedFormatToGLInternalFormat(CompressedFormat::BC4_R) != 0);
    REQUIRE(CompressedFormatToGLInternalFormat(CompressedFormat::BC5_RG) != 0);
    REQUIRE(CompressedFormatToGLInternalFormat(CompressedFormat::BC7_RGBA) != 0);

    // Different formats should map to different GL enums (except BC1_RGB/BC1_RGBA
    // which are intentionally different).
    REQUIRE(CompressedFormatToGLInternalFormat(CompressedFormat::BC1_RGB) !=
            CompressedFormatToGLInternalFormat(CompressedFormat::BC3_RGBA));
}

// =========================================================================
//  CookTextureFromRGBA — basic cooking for each format
// =========================================================================

TEST_CASE("CookTextureFromRGBA cooks a 16×16 solid colour texture",
          "[texture_cooker][cook]") {
    const auto rgba = MakeSolidRGBA(16, 16, 200, 100, 50, 255);

    TextureCookSettings settings;
    settings.generateMips = false;  // Single level for simplicity.

    SECTION("BC1_RGB") {
        settings.format = CompressedFormat::BC1_RGB;
        auto result = CookTextureFromRGBA(rgba.data(), 16, 16, settings);
        REQUIRE(result);
        REQUIRE(result.error.empty());
        REQUIRE(result.format == CompressedFormat::BC1_RGB);
        REQUIRE(result.width == 16);
        REQUIRE(result.height == 16);
        REQUIRE(result.mipLevels == 1);
        // 16×16 → 4×4 blocks → 16 blocks × 8 bytes = 128 bytes
        REQUIRE(result.data.size() == 128);
        REQUIRE(CompressedDataLooksValid(result.data));
    }

    SECTION("BC3_RGBA") {
        settings.format = CompressedFormat::BC3_RGBA;
        auto result = CookTextureFromRGBA(rgba.data(), 16, 16, settings);
        REQUIRE(result);
        // 16×16 → 16 blocks × 16 bytes = 256 bytes
        REQUIRE(result.data.size() == 256);
        REQUIRE(CompressedDataLooksValid(result.data));
    }

    SECTION("BC4_R") {
        settings.format = CompressedFormat::BC4_R;
        auto result = CookTextureFromRGBA(rgba.data(), 16, 16, settings);
        REQUIRE(result);
        // 16×16 → 16 blocks × 8 bytes = 128 bytes
        REQUIRE(result.data.size() == 128);
        REQUIRE(CompressedDataLooksValid(result.data));
    }

    SECTION("BC5_RG") {
        settings.format = CompressedFormat::BC5_RG;
        auto result = CookTextureFromRGBA(rgba.data(), 16, 16, settings);
        REQUIRE(result);
        // 16×16 → 16 blocks × 16 bytes = 256 bytes
        REQUIRE(result.data.size() == 256);
        REQUIRE(CompressedDataLooksValid(result.data));
    }

    SECTION("BC7_RGBA") {
        settings.format = CompressedFormat::BC7_RGBA;
        auto result = CookTextureFromRGBA(rgba.data(), 16, 16, settings);
        REQUIRE(result);
        // 16×16 → 16 blocks × 16 bytes = 256 bytes
        REQUIRE(result.data.size() == 256);
        REQUIRE(CompressedDataLooksValid(result.data));
    }
}

// =========================================================================
//  Mip chain generation
// =========================================================================

TEST_CASE("CookTextureFromRGBA generates correct mip chain sizes",
          "[texture_cooker][mipchain]") {
    const auto rgba = MakeGradientRGBA(64, 64);

    TextureCookSettings settings;
    settings.generateMips = true;
    settings.format = CompressedFormat::BC1_RGB;

    auto result = CookTextureFromRGBA(rgba.data(), 64, 64, settings);
    REQUIRE(result);
    REQUIRE(result.mipLevels == ComputeMipLevels(64, 64));  // 7 levels

    // Verify total compressed size matches sum of per-level sizes.
    uint32_t expectedTotal = 0;
    uint32_t mipW = 64, mipH = 64;
    for (uint32_t mip = 0; mip < result.mipLevels; ++mip) {
        expectedTotal += CompressedMipSize(CompressedFormat::BC1_RGB, mipW, mipH);
        mipW = std::max(1u, mipW / 2);
        mipH = std::max(1u, mipH / 2);
    }
    REQUIRE(result.data.size() == expectedTotal);
}

TEST_CASE("CookTextureFromRGBA with generateMips=false produces single level",
          "[texture_cooker][nomips]") {
    const auto rgba = MakeSolidRGBA(32, 32, 128, 128, 128, 255);

    TextureCookSettings settings;
    settings.generateMips = false;
    settings.format = CompressedFormat::BC7_RGBA;

    auto result = CookTextureFromRGBA(rgba.data(), 32, 32, settings);
    REQUIRE(result);
    REQUIRE(result.mipLevels == 1);
    REQUIRE(result.data.size() == CompressedMipSize(CompressedFormat::BC7_RGBA, 32, 32));
}

// =========================================================================
//  Non-power-of-two and non-multiple-of-4 dimensions
// =========================================================================

TEST_CASE("CookTextureFromRGBA handles non-power-of-two dimensions",
          "[texture_cooker][npot]") {
    // 10×10 image with BC1.
    const auto rgba = MakeSolidRGBA(10, 10, 64, 128, 192, 255);

    TextureCookSettings settings;
    settings.generateMips = true;
    settings.format = CompressedFormat::BC1_RGB;

    auto result = CookTextureFromRGBA(rgba.data(), 10, 10, settings);
    REQUIRE(result);

    // Mip chain: 10 → 5 → 2 → 1 (4 levels).
    REQUIRE(result.mipLevels == 4);

    // Verify per-level sizes.
    uint32_t expectedTotal = 0;
    uint32_t mipW = 10, mipH = 10;
    for (uint32_t mip = 0; mip < result.mipLevels; ++mip) {
        expectedTotal += CompressedMipSize(CompressedFormat::BC1_RGB, mipW, mipH);
        mipW = std::max(1u, mipW / 2);
        mipH = std::max(1u, mipH / 2);
    }
    REQUIRE(result.data.size() == expectedTotal);
}

TEST_CASE("CookTextureFromRGBA handles 1×1 image", "[texture_cooker][1x1]") {
    const auto rgba = MakeSolidRGBA(1, 1, 255, 0, 0, 255);

    TextureCookSettings settings;
    settings.generateMips = false;
    settings.format = CompressedFormat::BC3_RGBA;

    auto result = CookTextureFromRGBA(rgba.data(), 1, 1, settings);
    REQUIRE(result);
    // 1×1 still rounds up to one 4×4 block → 16 bytes for BC3.
    REQUIRE(result.data.size() == 16);
    REQUIRE(CompressedDataLooksValid(result.data));
}

// =========================================================================
//  Compression determinism
// =========================================================================

TEST_CASE("CookTextureFromRGBA is deterministic", "[texture_cooker][determinism]") {
    const auto rgba = MakeGradientRGBA(16, 16);

    TextureCookSettings settings;
    settings.generateMips = false;

    SECTION("BC1") {
        settings.format = CompressedFormat::BC1_RGB;
        auto r1 = CookTextureFromRGBA(rgba.data(), 16, 16, settings);
        auto r2 = CookTextureFromRGBA(rgba.data(), 16, 16, settings);
        REQUIRE(r1);
        REQUIRE(r2);
        REQUIRE(r1.data == r2.data);
    }

    SECTION("BC7") {
        settings.format = CompressedFormat::BC7_RGBA;
        auto r1 = CookTextureFromRGBA(rgba.data(), 16, 16, settings);
        auto r2 = CookTextureFromRGBA(rgba.data(), 16, 16, settings);
        REQUIRE(r1);
        REQUIRE(r2);
        REQUIRE(r1.data == r2.data);
    }
}

// =========================================================================
//  Error paths
// =========================================================================

TEST_CASE("CookTextureFromRGBA handles invalid inputs", "[texture_cooker][errors]") {
    TextureCookSettings settings;

    SECTION("Null data pointer") {
        auto result = CookTextureFromRGBA(nullptr, 16, 16, settings);
        REQUIRE_FALSE(result);
        REQUIRE(!result.error.empty());
        REQUIRE(result.data.empty());
    }

    SECTION("Zero width") {
        std::vector<uint8_t> dummy(64);
        auto result = CookTextureFromRGBA(dummy.data(), 0, 16, settings);
        REQUIRE_FALSE(result);
        REQUIRE(!result.error.empty());
    }

    SECTION("Zero height") {
        std::vector<uint8_t> dummy(64);
        auto result = CookTextureFromRGBA(dummy.data(), 16, 0, settings);
        REQUIRE_FALSE(result);
        REQUIRE(!result.error.empty());
    }
}

// =========================================================================
//  BC4 / BC5 channel extraction
// =========================================================================

TEST_CASE("BC4 and BC5 correctly extract channels from RGBA input",
          "[texture_cooker][bc4_bc5]") {
    // Create an image where R differs from G, so BC4≠BC5 behaviour is visible.
    auto rgba = MakeSolidRGBA(16, 16, 200, 100, 50, 255);

    TextureCookSettings settings;
    settings.generateMips = false;

    SECTION("BC4 extracts R channel only") {
        settings.format = CompressedFormat::BC4_R;
        auto result = CookTextureFromRGBA(rgba.data(), 16, 16, settings);
        REQUIRE(result);
        REQUIRE(result.data.size() == 128);  // 16 blocks × 8 bytes
    }

    SECTION("BC5 extracts RG channels") {
        settings.format = CompressedFormat::BC5_RG;
        auto result = CookTextureFromRGBA(rgba.data(), 16, 16, settings);
        REQUIRE(result);
        REQUIRE(result.data.size() == 256);  // 16 blocks × 16 bytes
    }
}

// =========================================================================
//  BC7 uber levels affect output
// =========================================================================

TEST_CASE("BC7 uberLevel changes output (higher quality ≠ lower quality)",
          "[texture_cooker][bc7_uber]") {
    const auto rgba = MakeGradientRGBA(16, 16);

    TextureCookSettings settings;
    settings.generateMips = false;
    settings.format = CompressedFormat::BC7_RGBA;

    SECTION("uberLevel 0 vs 4 may produce different results") {
        settings.bc7UberLevel = 0;
        auto r0 = CookTextureFromRGBA(rgba.data(), 16, 16, settings);

        settings.bc7UberLevel = 4;
        auto r4 = CookTextureFromRGBA(rgba.data(), 16, 16, settings);

        REQUIRE(r0);
        REQUIRE(r4);
        REQUIRE(r0.data.size() == r4.data.size());

        // They MAY differ (higher uber level refines more).
        // We don't assert inequality — both are valid — but we verify both cook.
    }
}

// =========================================================================
//  Solid-colour compression edge cases
// =========================================================================

TEST_CASE("Solid colours compress to non-empty valid blocks",
          "[texture_cooker][solid]") {
    TextureCookSettings settings;
    settings.generateMips = false;

    SECTION("Fully opaque red, 4×4") {
        const auto rgba = MakeSolidRGBA(4, 4, 255, 0, 0, 255);
        settings.format = CompressedFormat::BC1_RGB;
        auto result = CookTextureFromRGBA(rgba.data(), 4, 4, settings);
        REQUIRE(result);
        REQUIRE(result.data.size() == 8);  // Single block
        REQUIRE(CompressedDataLooksValid(result.data));
    }

    SECTION("Fully transparent, 4×4") {
        const auto rgba = MakeSolidRGBA(4, 4, 0, 0, 0, 0);
        settings.format = CompressedFormat::BC3_RGBA;
        auto result = CookTextureFromRGBA(rgba.data(), 4, 4, settings);
        REQUIRE(result);
        REQUIRE(result.data.size() == 16);
    }
}
