/** @file test_release_pipeline.cpp
 *  @brief End-to-end tests for the full Horo Engine release pipeline:
 *         Raw Asset → Cook → Pack → Extract → Verify.
 *
 *  Phase 3.2: End-to-End Release Test
 *
 *  Covers the complete pipeline across multiple asset types:
 *  - Texture pipeline: raw RGBA → Cook (BC7 compressed) → Pack → Extract → verify
 *  - Shader pipeline: raw GLSL → Cook (SPIR-V) → Pack → Extract → verify
 *  - Multi-asset archives with mixed cooked content
 *  - CRC32 always-on + SHA-256 opt-in through the full pipeline
 *  - Tampered archive detection via hash verification
 *  - Compression round-trips
 *  - Encryption API surface (SetEncryptionEnabled / SetEncryptionKey)
 *    NOTE: AES-CTR encryption is a Phase 1.2 TODO — the Packager currently
 *    accepts encryption flags but stores data as plaintext.  The encryption-
 *    flag tests exercise the API surface and verify the pipeline does not
 *    regress when encryption is enabled.  Wrong-key detection tests will
 *    be added when the encryption implementation lands.
 *  - Lifecycle: repeated write/open cycles
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "core/archive/HashVerifier.h"
#include "core/archive/HoroFormat.h"
#include "core/archive/Packager.h"
#include "core/crypto/AESContext.h"
#include "core/ProjectPath.h"
#include "core/pipeline/AesCtrProvider.h"
#include "core/pipeline/ArchiveBuilder.h"
#include "core/pipeline/ArchiveFormat.h"
#include "core/pipeline/ArtifactManifest.h"
#include "core/pipeline/ReleasePipeline.h"
#include "core/pipeline/TextureCooker.h"

#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <unordered_map>
#include <vector>

using namespace Horo::Archive;
using namespace Horo::Build;
using namespace Horo::Crypto;
using namespace Horo::Pipeline;

namespace fs = std::filesystem;

// ==========================================================================
//  Test helpers
// ==========================================================================

namespace {

/** @brief Return a unique temporary directory path for a test case. */
fs::path MakeTempDir(const std::string& test_name) {
    const auto base = fs::temp_directory_path() / "horo_e2e_test";
    const auto dir = base / test_name;
    std::error_code ec;
    fs::create_directories(dir, ec);
    fs::remove_all(dir, ec);  // Ensure clean state
    fs::create_directories(dir, ec);
    return dir;
}

/** @brief Remove a temporary directory and all its contents. */
void CleanupTempDir(const fs::path& dir) {
    std::error_code ec;
    fs::remove_all(dir, ec);
}

/** @brief Create a solid-colour RGBA test image. */
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

/** @brief Asset data provider backed by an in-memory map. */
class InMemoryAssetProvider {
public:
    void Add(const std::string& path, std::vector<uint8_t> data) {
        m_data[path] = std::move(data);
    }

    bool operator()(const std::string& path, std::vector<uint8_t>& out) const {
        auto it = m_data.find(path);
        if (it == m_data.end()) return false;
        out = it->second;
        return true;
    }

private:
    std::unordered_map<std::string, std::vector<uint8_t>> m_data;
};

/** @brief A fixed 256-bit AES key for encryption API surface tests. */
constexpr std::array<uint8_t, 32> kTestKey256 = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
    0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F,
};

/** @brief Minimal valid GLSL shaders for testing (when glslang is available). */
#if defined(HORO_HAS_GLSLANG)
constexpr const char* kMinimalVertexShader = R"(#version 450
vec2 positions[3] = vec2[](
    vec2(0.0, -0.5), vec2(0.5, 0.5), vec2(-0.5, 0.5));
void main() {
    gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);
}
)";

constexpr const char* kMinimalFragmentShader = R"(#version 450
layout(location = 0) out vec4 outColor;
void main() {
    outColor = vec4(1.0, 0.0, 0.0, 1.0);
}
)";
#endif

/** @brief Reads a complete binary file into memory for archive layout checks. */
std::vector<uint8_t> ReadBinaryFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    REQUIRE(in.good());
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(in),
                                std::istreambuf_iterator<char>());
}

/** @brief Copies a packed POD value from a byte buffer at a checked offset. */
template <typename T>
T ReadPodAt(const std::vector<uint8_t>& bytes, size_t offset) {
    REQUIRE(offset + sizeof(T) <= bytes.size());
    T value{};
    std::memcpy(&value, bytes.data() + offset, sizeof(T));
    return value;
}

} // anonymous namespace

// ==========================================================================
//  Section 1: Texture Pipeline — Raw RGBA → Cook → Pack → Extract → Verify
// ==========================================================================

TEST_CASE("E2E: texture pipeline — raw RGBA → cook BC7 → pack → extract → verify",
          "[e2e][texture][pipeline]") {
    const auto tmp = MakeTempDir("e2e_texture");
    const auto archive_path = tmp / "texture_bundle.horo";

    // ── Step 1: Raw Asset (create RGBA image) ──────────────────────────
    const auto rgba = MakeSolidRGBA(64, 64, 255, 128, 64, 255);

    // ── Step 2: Cook (compress to BC7) ─────────────────────────────────
    TextureCookSettings cookSettings;
    cookSettings.format = CompressedFormat::BC7_RGBA;
    cookSettings.generateMips = false;
    cookSettings.bc7UberLevel = 0;

    auto cooked = CookTextureFromRGBA(rgba.data(), 64, 64, cookSettings);
    REQUIRE(cooked);
    REQUIRE_FALSE(cooked.data.empty());
    REQUIRE(cooked.format == CompressedFormat::BC7_RGBA);
    REQUIRE(cooked.width == 64);
    REQUIRE(cooked.height == 64);

    // ── Step 3: Pack into .horo archive ────────────────────────────────
    InMemoryAssetProvider provider;
    provider.Add("textures/hero.dds", cooked.data);

    Packager packer;
    packer.SetCompressionLevel(1);
    REQUIRE(packer.AddAsset("textures/hero.dds") == PackResult::Ok);
    REQUIRE(packer.Write(archive_path.string(), provider) == PackResult::Ok);
    REQUIRE(fs::exists(archive_path));

    // ── Step 4: Run (extract and verify) ───────────────────────────────
    Packager reader;
    REQUIRE(reader.Open(archive_path.string()) == PackResult::Ok);
    REQUIRE(reader.IsOpen());

    std::vector<std::string> paths;
    REQUIRE(reader.ListAssets(paths) == PackResult::Ok);
    REQUIRE(paths.size() == 1);
    REQUIRE(paths[0] == "textures/hero.dds");

    std::vector<uint8_t> extracted;
    REQUIRE(reader.Extract("textures/hero.dds", extracted) == PackResult::Ok);
    REQUIRE(extracted == cooked.data);

    CleanupTempDir(tmp);
}

TEST_CASE("E2E: texture pipeline — RGBA → cook BC7 with mips → pack → verify",
          "[e2e][texture][pipeline][mips]") {
    const auto tmp = MakeTempDir("e2e_texture_mips");
    const auto archive_path = tmp / "mip_bundle.horo";

    const auto rgba = MakeSolidRGBA(128, 128, 0, 200, 100, 255);

    TextureCookSettings cookSettings;
    cookSettings.format = CompressedFormat::BC7_RGBA;
    cookSettings.generateMips = true;
    cookSettings.bc7UberLevel = 0;

    auto cooked = CookTextureFromRGBA(rgba.data(), 128, 128, cookSettings);
    REQUIRE(cooked);
    REQUIRE(cooked.mipLevels > 1);
    REQUIRE_FALSE(cooked.data.empty());

    InMemoryAssetProvider provider;
    provider.Add("textures/mipped.dds", cooked.data);

    Packager packer;
    packer.SetCompressionLevel(0);
    REQUIRE(packer.AddAsset("textures/mipped.dds") == PackResult::Ok);
    REQUIRE(packer.Write(archive_path.string(), provider) == PackResult::Ok);

    Packager reader;
    REQUIRE(reader.Open(archive_path.string()) == PackResult::Ok);

    std::vector<uint8_t> extracted;
    REQUIRE(reader.Extract("textures/mipped.dds", extracted) == PackResult::Ok);
    REQUIRE(extracted == cooked.data);

    CleanupTempDir(tmp);
}

// ==========================================================================
//  Section 2: Shader Pipeline — Raw GLSL → Cook → Pack → Extract → Verify
// ==========================================================================

#if defined(HORO_HAS_GLSLANG)
#include "core/pipeline/ShaderCooker.h"

TEST_CASE("E2E: shader pipeline — GLSL → cook SPIR-V → pack → extract → verify",
          "[e2e][shader][pipeline]") {
    const auto tmp = MakeTempDir("e2e_shader");
    const auto archive_path = tmp / "shader_bundle.horo";

    // ── Cook GLSL → SPIR-V ────────────────────────────────────────────
    ShaderCookSettings shaderSettings;
    auto cookedVert = CookShaderFromSource(kMinimalVertexShader,
                                           ShaderStage::Vertex,
                                           "test.vert",
                                           shaderSettings);
    REQUIRE(cookedVert);
    REQUIRE_FALSE(cookedVert.spirv.empty());

    auto cookedFrag = CookShaderFromSource(kMinimalFragmentShader,
                                           ShaderStage::Fragment,
                                           "test.frag",
                                           shaderSettings);
    REQUIRE(cookedFrag);
    REQUIRE_FALSE(cookedFrag.spirv.empty());

    // Verify SPIR-V magic number (0x07230203)
    REQUIRE(cookedVert.spirv[0] == 0x07230203u);
    REQUIRE(cookedFrag.spirv[0] == 0x07230203u);

    // Convert SPIR-V to byte vectors for packing
    auto vertBytes = std::vector<uint8_t>(
        reinterpret_cast<const uint8_t*>(cookedVert.spirv.data()),
        reinterpret_cast<const uint8_t*>(cookedVert.spirv.data()) +
            cookedVert.spirv.size() * sizeof(uint32_t));

    auto fragBytes = std::vector<uint8_t>(
        reinterpret_cast<const uint8_t*>(cookedFrag.spirv.data()),
        reinterpret_cast<const uint8_t*>(cookedFrag.spirv.data()) +
            cookedFrag.spirv.size() * sizeof(uint32_t));

    // ── Pack ──────────────────────────────────────────────────────────
    InMemoryAssetProvider provider;
    provider.Add("shaders/test.vert.spv", vertBytes);
    provider.Add("shaders/test.frag.spv", fragBytes);

    Packager packer;
    packer.SetCompressionLevel(1);
    REQUIRE(packer.AddAsset("shaders/test.vert.spv") == PackResult::Ok);
    REQUIRE(packer.AddAsset("shaders/test.frag.spv") == PackResult::Ok);
    REQUIRE(packer.Write(archive_path.string(), provider) == PackResult::Ok);
    REQUIRE(fs::exists(archive_path));

    // ── Extract and verify ────────────────────────────────────────────
    Packager reader;
    REQUIRE(reader.Open(archive_path.string()) == PackResult::Ok);

    std::vector<std::string> paths;
    REQUIRE(reader.ListAssets(paths) == PackResult::Ok);
    REQUIRE(paths.size() == 2);

    std::vector<uint8_t> outVert, outFrag;
    REQUIRE(reader.Extract("shaders/test.vert.spv", outVert) == PackResult::Ok);
    REQUIRE(reader.Extract("shaders/test.frag.spv", outFrag) == PackResult::Ok);
    REQUIRE(outVert == vertBytes);
    REQUIRE(outFrag == fragBytes);

    CleanupTempDir(tmp);
}
#else
TEST_CASE("E2E: shader pipeline — skipped (no glslang)",
          "[e2e][shader][pipeline][skip]") {
    SUCCEED("Shader cooker not available — texture E2E tests cover the pipeline");
}
#endif // HORO_HAS_GLSLANG

// ==========================================================================
//  Section 3: Encryption API Surface
//
//  NOTE (Phase 1.2): AES-CTR encryption is a known TODO in Packager::Write().
//  The SetEncryptionEnabled / SetEncryptionKey API is accepted but data is
//  stored as plaintext.  These tests verify the pipeline does not crash or
//  regress when encryption flags are enabled, and that extract still works
//  with matching keys.  Full wrong-key detection tests will be added when
//  encryption is implemented.
// ==========================================================================

TEST_CASE("E2E: encryption API surface — pack+extract with encryption enabled",
          "[e2e][encrypt][api][pipeline]") {
    const auto tmp = MakeTempDir("e2e_enc_api");
    const auto archive_path = tmp / "enc_api.horo";

    const auto rgba = MakeSolidRGBA(32, 32, 128, 64, 255, 255);
    TextureCookSettings settings;
    settings.format = CompressedFormat::BC7_RGBA;
    settings.generateMips = false;

    auto cooked = CookTextureFromRGBA(rgba.data(), 32, 32, settings);
    REQUIRE(cooked);

    InMemoryAssetProvider provider;
    provider.Add("sprites/icon.dds", cooked.data);

    // Pack with encryption flags enabled (stored plaintext pending Phase 1.2)
    Packager packer;
    packer.SetCompressionLevel(0);
    packer.SetEncryptionEnabled(true);
    packer.SetEncryptionKey(kTestKey256);
    REQUIRE(packer.AddAsset("sprites/icon.dds") == PackResult::Ok);
    REQUIRE(packer.Write(archive_path.string(), provider) == PackResult::Ok);
    REQUIRE(fs::exists(archive_path));

    // Reader with same key — should succeed (data is plaintext)
    Packager reader;
    reader.SetEncryptionEnabled(true);
    reader.SetEncryptionKey(kTestKey256);
    REQUIRE(reader.Open(archive_path.string()) == PackResult::Ok);
    REQUIRE(reader.IsOpen());

    std::vector<uint8_t> extracted;
    REQUIRE(reader.Extract("sprites/icon.dds", extracted) == PackResult::Ok);
    REQUIRE(extracted == cooked.data);

    CleanupTempDir(tmp);
}

TEST_CASE("E2E: encryption API surface — toggle on/off is safe",
          "[e2e][encrypt][api][pipeline]") {
    const auto tmp = MakeTempDir("e2e_enc_toggle");
    const auto archive_path = tmp / "enc_toggle.horo";

    const auto rgba = MakeSolidRGBA(16, 16, 200, 100, 50, 255);
    TextureCookSettings settings;
    settings.format = CompressedFormat::BC7_RGBA;
    settings.generateMips = false;

    auto cooked = CookTextureFromRGBA(rgba.data(), 16, 16, settings);
    REQUIRE(cooked);

    InMemoryAssetProvider provider;
    provider.Add("toggle.dds", cooked.data);

    // Toggle encryption on, then off — should not crash
    Packager packer;
    packer.SetEncryptionEnabled(true);
    packer.SetEncryptionEnabled(false);
    packer.SetCompressionLevel(0);
    REQUIRE(packer.AddAsset("toggle.dds") == PackResult::Ok);
    REQUIRE(packer.Write(archive_path.string(), provider) == PackResult::Ok);

    // Extract without encryption — should work
    Packager reader;
    REQUIRE(reader.Open(archive_path.string()) == PackResult::Ok);

    std::vector<uint8_t> extracted;
    REQUIRE(reader.Extract("toggle.dds", extracted) == PackResult::Ok);
    REQUIRE(extracted == cooked.data);

    CleanupTempDir(tmp);
}

TEST_CASE("Pipeline crypto provider — AES-CTR validates keys and round-trips partial blocks",
          "[pipeline][crypto][aes-ctr]") {
    AesCtrProvider provider;
    std::array<uint8_t, 8> shortKey{};
    REQUIRE_FALSE(provider.SetKey(shortKey.data(),
                                  static_cast<uint32_t>(shortKey.size())));
    REQUIRE_FALSE(provider.HasKey());

    std::array<uint8_t, kAes128KeySize> key{};
    for (size_t i = 0; i < key.size(); ++i)
        key[i] = static_cast<uint8_t>(i * 17u + 3u);

    REQUIRE(provider.SetKey(key.data(), static_cast<uint32_t>(key.size())));
    REQUIRE(provider.HasKey());
    REQUIRE(std::string(provider.Name()).find("AES-128-CTR") != std::string::npos);

    std::array<uint8_t, 8> kcv{};
    REQUIRE(provider.GenerateKcv(kcv.data()));
    REQUIRE(std::any_of(kcv.begin(), kcv.end(), [](uint8_t b) { return b != 0; }));

    const std::vector<uint8_t> plaintext = {
        0x10, 0x11, 0x12, 0x13, 0x20, 0x21, 0x22,
        0x23, 0x30, 0x31, 0x32, 0x33, 0x40, 0x41,
        0x42, 0x43, 0x50, 0x51, 0x52};
    std::vector<uint8_t> buffer(Horo::Build::kAesBlockSize + plaintext.size(), 0);
    std::copy(plaintext.begin(), plaintext.end(), buffer.begin() + Horo::Build::kAesBlockSize);

    uint32_t encryptedSize = 0;
    REQUIRE(provider.Encrypt(std::span<uint8_t>(buffer), encryptedSize));
    REQUIRE(encryptedSize == buffer.size());
    REQUIRE_FALSE(std::equal(plaintext.begin(), plaintext.end(),
                             buffer.begin() + Horo::Build::kAesBlockSize));

    uint32_t decryptedSize = 0;
    REQUIRE(provider.Decrypt(std::span<uint8_t>(buffer), decryptedSize));
    REQUIRE(decryptedSize == plaintext.size());
    REQUIRE(std::equal(plaintext.begin(), plaintext.end(),
                       buffer.begin() + Horo::Build::kAesBlockSize));

    std::vector<uint8_t> tooSmall(Horo::Build::kAesBlockSize - 1, 0);
    REQUIRE_FALSE(provider.Encrypt(std::span<uint8_t>(tooSmall), encryptedSize));
    REQUIRE_FALSE(provider.Decrypt(std::span<uint8_t>(tooSmall), decryptedSize));
}

TEST_CASE("Pipeline ArchiveBuilder — writes deterministic plaintext archive layout",
          "[pipeline][archive][layout]") {
    const auto tmp = MakeTempDir("pipeline_archive_plaintext_layout");
    const auto archivePath = tmp / "assets.horo";

    ArchiveBuilder builder;
    builder.AddAsset("stale.bin", std::string_view("ignored"));
    REQUIRE(builder.AssetCount() == 1);
    builder.Clear();
    REQUIRE(builder.AssetCount() == 0);

    const std::vector<uint8_t> payloadA = {0x01, 0x02, 0x03, 0x04, 0x05};
    const std::string payloadB = "level-data";
    builder.AddAsset("textures/a.bin", std::span<const uint8_t>(payloadA));
    builder.AddAsset("scenes/main.scene", std::string_view(payloadB));
    REQUIRE(builder.AssetCount() == 2);

    ArchiveBuilder moved(std::move(builder));
    REQUIRE(moved.AssetCount() == 2);
    ArchiveBuilder assigned;
    assigned = std::move(moved);
    REQUIRE(assigned.AssetCount() == 2);

    std::vector<std::pair<uint64_t, uint64_t>> progress;
    assigned.SetProgressCallback([&progress](uint64_t written, uint64_t total) {
        progress.emplace_back(written, total);
    });

    REQUIRE(assigned.WriteToFile(archivePath));
    REQUIRE(fs::exists(archivePath));

    const auto bytes = ReadBinaryFile(archivePath);
    REQUIRE(bytes.size() == kArchiveHeaderSize + sizeof(uint32_t) +
                                2 * sizeof(TocEntry) + payloadA.size() +
                                payloadB.size());

    const auto header = ReadPodAt<Horo::Build::ArchiveHeader>(bytes, 0);
    REQUIRE(std::string(header.magic, header.magic + 4) == "HORO");
    REQUIRE(header.version == 1);
    REQUIRE(header.flags == 0);
    REQUIRE(std::all_of(std::begin(header.kcv), std::end(header.kcv),
                        [](uint8_t b) { return b == 0; }));
    REQUIRE(header.reserved == 0);
    REQUIRE(header.tocOffset == kArchiveHeaderSize);
    REQUIRE(header.tocSize == sizeof(uint32_t) + 2 * sizeof(TocEntry));
    REQUIRE(header.dataOffset == header.tocOffset + header.tocSize);

    const uint32_t entryCount = ReadPodAt<uint32_t>(bytes, header.tocOffset);
    REQUIRE(entryCount == 2);
    const auto tocA = ReadPodAt<TocEntry>(bytes, header.tocOffset + sizeof(uint32_t));
    const auto tocB = ReadPodAt<TocEntry>(bytes, header.tocOffset + sizeof(uint32_t) +
                                                   sizeof(TocEntry));

    REQUIRE(tocA.pathHash == Fnv1a64("textures/a.bin"));
    REQUIRE(tocA.originalSize == payloadA.size());
    REQUIRE(tocA.storedSize == payloadA.size());
    REQUIRE(tocA.contentHash == Fnv1a64(payloadA.data(), payloadA.size()));
    REQUIRE(tocA.offset == header.dataOffset);

    const auto* payloadBBytes = reinterpret_cast<const uint8_t*>(payloadB.data());
    REQUIRE(tocB.pathHash == Fnv1a64("scenes/main.scene"));
    REQUIRE(tocB.originalSize == payloadB.size());
    REQUIRE(tocB.storedSize == payloadB.size());
    REQUIRE(tocB.contentHash == Fnv1a64(payloadBBytes, payloadB.size()));
    REQUIRE(tocB.offset == header.dataOffset + payloadA.size());

    REQUIRE(std::equal(payloadA.begin(), payloadA.end(), bytes.begin() + tocA.offset));
    REQUIRE(std::equal(payloadB.begin(), payloadB.end(), bytes.begin() + tocB.offset));
    REQUIRE(progress.size() == 2);
    REQUIRE(progress.back().first == payloadA.size() + payloadB.size());
    REQUIRE(progress.back().second == payloadA.size() + payloadB.size());

    CleanupTempDir(tmp);
}

TEST_CASE("Pipeline ArchiveBuilder — encrypts chunks with AES provider metadata",
          "[pipeline][archive][crypto]") {
    const auto tmp = MakeTempDir("pipeline_archive_encrypted_layout");
    const auto archivePath = tmp / "encrypted_assets.horo";

    AesCtrProvider provider;
    std::array<uint8_t, kAes128KeySize> key{};
    for (size_t i = 0; i < key.size(); ++i)
        key[i] = static_cast<uint8_t>(0xA0u + i);
    REQUIRE(provider.SetKey(key.data(), static_cast<uint32_t>(key.size())));

    const std::vector<uint8_t> payload = {
        0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x02, 0x03, 0x04,
        0x05, 0x06, 0x07, 0x08, 0x09};

    ArchiveBuilder builder;
    builder.SetCryptoProvider(&provider);
    builder.SetEncryptionEnabled(true);
    builder.AddAsset("secure/data.bin", std::span<const uint8_t>(payload));

    REQUIRE(builder.WriteToFile(archivePath));

    const auto bytes = ReadBinaryFile(archivePath);
    const auto header = ReadPodAt<Horo::Build::ArchiveHeader>(bytes, 0);
    REQUIRE(HasFlag(static_cast<ArchiveFlag>(header.flags), ArchiveFlag::kEncrypted));
    REQUIRE(std::any_of(std::begin(header.kcv), std::end(header.kcv),
                        [](uint8_t b) { return b != 0; }));

    const uint32_t entryCount = ReadPodAt<uint32_t>(bytes, header.tocOffset);
    REQUIRE(entryCount == 1);
    const auto toc = ReadPodAt<TocEntry>(bytes, header.tocOffset + sizeof(uint32_t));
    REQUIRE(toc.pathHash == Fnv1a64("secure/data.bin"));
    REQUIRE(toc.originalSize == payload.size());
    REQUIRE(toc.storedSize == payload.size() + Horo::Build::kAesBlockSize);
    REQUIRE(toc.contentHash == Fnv1a64(payload.data(), payload.size()));
    REQUIRE(toc.offset == header.dataOffset);

    REQUIRE(toc.offset + toc.storedSize <= bytes.size());
    std::vector<uint8_t> encrypted(bytes.begin() + toc.offset,
                                   bytes.begin() + toc.offset + toc.storedSize);
    REQUIRE_FALSE(std::equal(payload.begin(), payload.end(),
                             encrypted.begin() + Horo::Build::kAesBlockSize));

    uint32_t plainSize = 0;
    REQUIRE(provider.Decrypt(std::span<uint8_t>(encrypted), plainSize));
    REQUIRE(plainSize == payload.size());
    REQUIRE(std::equal(payload.begin(), payload.end(),
                       encrypted.begin() + Horo::Build::kAesBlockSize));

    CleanupTempDir(tmp);
}

// ==========================================================================
//  Section 4: Hash verification through full pipeline (CRC32 + SHA-256)
// ==========================================================================

TEST_CASE("E2E: pipeline with SHA-256 — cook → pack (SHA256) → extract → verify",
          "[e2e][hash][sha256][pipeline]") {
    const auto tmp = MakeTempDir("e2e_sha256");
    const auto archive_path = tmp / "sha256_bundle.horo";

    const auto rgba = MakeSolidRGBA(64, 64, 50, 100, 200, 255);
    TextureCookSettings settings;
    settings.format = CompressedFormat::BC7_RGBA;
    settings.generateMips = false;

    auto cooked = CookTextureFromRGBA(rgba.data(), 64, 64, settings);
    REQUIRE(cooked);

    InMemoryAssetProvider provider;
    provider.Add("hash_test.dds", cooked.data);

    Packager packer;
    packer.SetCompressionLevel(0);
    packer.SetSHA256Enabled(true);
    REQUIRE(packer.AddAsset("hash_test.dds") == PackResult::Ok);
    REQUIRE(packer.Write(archive_path.string(), provider) == PackResult::Ok);

    // Extract — CRC32 + SHA-256 both verified
    Packager reader;
    REQUIRE(reader.Open(archive_path.string()) == PackResult::Ok);

    std::vector<uint8_t> extracted;
    REQUIRE(reader.Extract("hash_test.dds", extracted) == PackResult::Ok);
    REQUIRE(extracted == cooked.data);

    // Verify CRC32 of extracted data matches
    uint32_t expected_crc = ComputeCRC32(cooked.data.data(), cooked.data.size());
    uint32_t extracted_crc = ComputeCRC32(extracted.data(), extracted.size());
    REQUIRE(expected_crc == extracted_crc);

    CleanupTempDir(tmp);
}

TEST_CASE("E2E: pipeline with SHA-256 — tampered archive detected",
          "[e2e][hash][sha256][pipeline][corrupt]") {
    const auto tmp = MakeTempDir("e2e_sha256_corrupt");
    const auto archive_path = tmp / "corrupt_bundle.horo";

    const auto rgba = MakeSolidRGBA(32, 32, 75, 125, 200, 255);
    TextureCookSettings settings;
    settings.format = CompressedFormat::BC7_RGBA;
    settings.generateMips = false;

    auto cooked = CookTextureFromRGBA(rgba.data(), 32, 32, settings);
    REQUIRE(cooked);

    InMemoryAssetProvider provider;
    provider.Add("corrupt_me.dds", cooked.data);

    Packager packer;
    packer.SetCompressionLevel(0);
    packer.SetSHA256Enabled(true);
    REQUIRE(packer.AddAsset("corrupt_me.dds") == PackResult::Ok);
    REQUIRE(packer.Write(archive_path.string(), provider) == PackResult::Ok);

    // Tamper with a data byte after header
    {
        std::fstream file(archive_path, std::ios::binary | std::ios::in |
                                      std::ios::out);
        file.seekp(40, std::ios::beg);
        char corrupt_byte;
        file.read(&corrupt_byte, 1);
        file.seekp(40, std::ios::beg);
        corrupt_byte ^= 0xFF;
        file.write(&corrupt_byte, 1);
    }

    // Extract should detect hash mismatch
    Packager reader;
    REQUIRE(reader.Open(archive_path.string()) == PackResult::Ok);

    std::vector<uint8_t> extracted;
    REQUIRE(reader.Extract("corrupt_me.dds", extracted) == PackResult::HashMismatch);

    CleanupTempDir(tmp);
}

// ==========================================================================
//  Section 5: Multi-asset archive through full pipeline
// ==========================================================================

TEST_CASE("E2E: multi-asset archive — mix of 5 cooked texture formats → pack → extract all",
          "[e2e][multi_asset][pipeline]") {
    const auto tmp = MakeTempDir("e2e_multi");
    const auto archive_path = tmp / "multi_bundle.horo";

    std::unordered_map<std::string, std::vector<uint8_t>> cooked_assets;

    // BC1 RGB solid red 16×16
    {
        auto rgba = MakeSolidRGBA(16, 16, 255, 0, 0, 255);
        TextureCookSettings s;
        s.format = CompressedFormat::BC1_RGB;
        s.generateMips = false;
        auto c = CookTextureFromRGBA(rgba.data(), 16, 16, s);
        REQUIRE(c);
        cooked_assets["textures/red.dds"] = c.data;
    }

    // BC3 RGBA green 32×32
    {
        auto rgba = MakeSolidRGBA(32, 32, 0, 255, 0, 200);
        TextureCookSettings s;
        s.format = CompressedFormat::BC3_RGBA;
        s.generateMips = false;
        auto c = CookTextureFromRGBA(rgba.data(), 32, 32, s);
        REQUIRE(c);
        cooked_assets["textures/green.dds"] = c.data;
    }

    // BC7 RGBA blue 64×64
    {
        auto rgba = MakeSolidRGBA(64, 64, 0, 0, 255, 128);
        TextureCookSettings s;
        s.format = CompressedFormat::BC7_RGBA;
        s.generateMips = false;
        auto c = CookTextureFromRGBA(rgba.data(), 64, 64, s);
        REQUIRE(c);
        cooked_assets["textures/blue.dds"] = c.data;
    }

    // BC4 single-channel 8×8
    {
        auto rgba = MakeSolidRGBA(8, 8, 128, 128, 128, 255);
        TextureCookSettings s;
        s.format = CompressedFormat::BC4_R;
        s.generateMips = false;
        auto c = CookTextureFromRGBA(rgba.data(), 8, 8, s);
        REQUIRE(c);
        cooked_assets["textures/height.dds"] = c.data;
    }

    // BC5 two-channel 16×16
    {
        auto rgba = MakeSolidRGBA(16, 16, 200, 100, 128, 255);
        TextureCookSettings s;
        s.format = CompressedFormat::BC5_RG;
        s.generateMips = false;
        auto c = CookTextureFromRGBA(rgba.data(), 16, 16, s);
        REQUIRE(c);
        cooked_assets["textures/normal.dds"] = c.data;
    }

    // ── Pack all assets ────────────────────────────────────────────────
    InMemoryAssetProvider provider;
    for (const auto& [path, data] : cooked_assets) {
        provider.Add(path, data);
    }

    Packager packer;
    packer.SetCompressionLevel(1);
    packer.SetSHA256Enabled(true);

    for (const auto& [path, data] : cooked_assets) {
        REQUIRE(packer.AddAsset(path) == PackResult::Ok);
    }

    REQUIRE(packer.Write(archive_path.string(), provider) == PackResult::Ok);

    // ── Extract all and verify ─────────────────────────────────────────
    Packager reader;
    REQUIRE(reader.Open(archive_path.string()) == PackResult::Ok);

    std::vector<std::string> paths;
    REQUIRE(reader.ListAssets(paths) == PackResult::Ok);
    REQUIRE(paths.size() == cooked_assets.size());

    for (const auto& [path, expected] : cooked_assets) {
        std::vector<uint8_t> extracted;
        INFO("Verifying asset: " << path);
        REQUIRE(reader.Extract(path, extracted) == PackResult::Ok);
        REQUIRE(extracted == expected);
    }

    // ExtractAll to disk
    const auto out_dir = tmp / "extracted";
    REQUIRE(reader.ExtractAll(out_dir.string()) == PackResult::Ok);
    REQUIRE(fs::exists(out_dir / "textures/red.dds"));
    REQUIRE(fs::exists(out_dir / "textures/green.dds"));
    REQUIRE(fs::exists(out_dir / "textures/blue.dds"));
    REQUIRE(fs::exists(out_dir / "textures/height.dds"));
    REQUIRE(fs::exists(out_dir / "textures/normal.dds"));

    CleanupTempDir(tmp);
}

// ==========================================================================
//  Section 6: Plain pipeline (no encryption, no SHA-256)
// ==========================================================================

TEST_CASE("E2E: plain pipeline — cook → pack → extract → verify",
          "[e2e][plain][pipeline]") {
    const auto tmp = MakeTempDir("e2e_plain");
    const auto archive_path = tmp / "plain.horo";

    const auto rgba = MakeSolidRGBA(48, 48, 100, 200, 50, 255);
    TextureCookSettings settings;
    settings.format = CompressedFormat::BC7_RGBA;
    settings.generateMips = true;

    auto cooked = CookTextureFromRGBA(rgba.data(), 48, 48, settings);
    REQUIRE(cooked);

    InMemoryAssetProvider provider;
    provider.Add("plain_texture.dds", cooked.data);

    Packager packer;
    packer.SetCompressionLevel(1);
    REQUIRE(packer.AddAsset("plain_texture.dds") == PackResult::Ok);
    REQUIRE(packer.Write(archive_path.string(), provider) == PackResult::Ok);

    Packager reader;
    REQUIRE(reader.Open(archive_path.string()) == PackResult::Ok);

    std::vector<uint8_t> extracted;
    REQUIRE(reader.Extract("plain_texture.dds", extracted) == PackResult::Ok);
    REQUIRE(extracted == cooked.data);

    CleanupTempDir(tmp);
}

// ==========================================================================
//  Section 7: Max compression pipeline
// ==========================================================================

TEST_CASE("E2E: compression pipeline — cook → max-compress → extract → verify",
          "[e2e][compression][pipeline]") {
    const auto tmp = MakeTempDir("e2e_comp_max");
    const auto archive_path = tmp / "comp_max.horo";

    // Cook a highly compressible texture (solid colour)
    const auto rgba = MakeSolidRGBA(128, 128, 127, 127, 127, 255);

    TextureCookSettings settings;
    settings.format = CompressedFormat::BC7_RGBA;
    settings.generateMips = false;

    auto cooked = CookTextureFromRGBA(rgba.data(), 128, 128, settings);
    REQUIRE(cooked);

    InMemoryAssetProvider provider;
    provider.Add("comp_max.dds", cooked.data);

    Packager packer;
    packer.SetCompressionLevel(12);  // Max LZ4 HC
    packer.SetSHA256Enabled(true);
    REQUIRE(packer.AddAsset("comp_max.dds") == PackResult::Ok);
    REQUIRE(packer.Write(archive_path.string(), provider) == PackResult::Ok);

    // Verify file is reasonably sized (compression helped)
    const auto file_size = fs::file_size(archive_path);
    REQUIRE(file_size > 0);
    REQUIRE(file_size < cooked.data.size() + 300);

    // Extract and verify round-trip
    Packager reader;
    REQUIRE(reader.Open(archive_path.string()) == PackResult::Ok);

    std::vector<uint8_t> extracted;
    REQUIRE(reader.Extract("comp_max.dds", extracted) == PackResult::Ok);
    REQUIRE(extracted == cooked.data);

    CleanupTempDir(tmp);
}

// ==========================================================================
//  Section 8: Stress — 50 cooked textures through full pipeline
// ==========================================================================

TEST_CASE("E2E: stress — 50 cooked textures → pack → extract all",
          "[e2e][stress][pipeline]") {
    const auto tmp = MakeTempDir("e2e_stress");
    const auto archive_path = tmp / "stress.horo";

    constexpr int kAssetCount = 50;
    std::unordered_map<std::string, std::vector<uint8_t>> originals;

    for (int i = 0; i < kAssetCount; ++i) {
        const uint32_t dim = static_cast<uint32_t>(16 + (i % 16) * 4);  // 16 to 76
        const auto rgba = MakeSolidRGBA(dim, dim,
            static_cast<uint8_t>((i * 17) % 256),
            static_cast<uint8_t>((i * 31) % 256),
            static_cast<uint8_t>((i * 53) % 256),
            255);

        TextureCookSettings s;
        s.format = CompressedFormat::BC7_RGBA;
        s.generateMips = false;
        s.bc7UberLevel = 0;

        auto c = CookTextureFromRGBA(rgba.data(), dim, dim, s);
        REQUIRE(c);

        const std::string path = "textures/tex_" + std::to_string(i) + ".dds";
        originals[path] = c.data;
    }

    InMemoryAssetProvider provider;
    for (const auto& [path, data] : originals) {
        provider.Add(path, data);
    }

    Packager packer;
    packer.SetCompressionLevel(1);

    for (const auto& [path, data] : originals) {
        REQUIRE(packer.AddAsset(path) == PackResult::Ok);
    }

    REQUIRE(packer.Write(archive_path.string(), provider) == PackResult::Ok);

    // Extract all and verify
    Packager reader;
    REQUIRE(reader.Open(archive_path.string()) == PackResult::Ok);

    for (const auto& [path, expected] : originals) {
        std::vector<uint8_t> extracted;
        REQUIRE(reader.Extract(path, extracted) == PackResult::Ok);
        REQUIRE(extracted == expected);
    }

    CleanupTempDir(tmp);
}

// ==========================================================================
//  Section 9: Pipeline lifecycle — repeated write/open cycles
// ==========================================================================

TEST_CASE("E2E: repeated pack → open → extract cycles don't leak state",
          "[e2e][lifecycle][pipeline]") {
    const auto tmp = MakeTempDir("e2e_cycles");
    const auto path1 = tmp / "cycle1.horo";
    const auto path2 = tmp / "cycle2.horo";

    const auto rgba = MakeSolidRGBA(32, 32, 150, 75, 200, 255);
    TextureCookSettings settings;
    settings.format = CompressedFormat::BC7_RGBA;
    settings.generateMips = false;

    auto cooked = CookTextureFromRGBA(rgba.data(), 32, 32, settings);
    REQUIRE(cooked);

    InMemoryAssetProvider provider;
    provider.Add("cycle.dds", cooked.data);

    // Cycle 1
    {
        Packager p;
        p.AddAsset("cycle.dds");
        REQUIRE(p.Write(path1.string(), provider) == PackResult::Ok);
    }

    // Cycle 2
    {
        Packager p;
        p.AddAsset("cycle.dds");
        REQUIRE(p.Write(path2.string(), provider) == PackResult::Ok);
    }

    // Verify both archives independently
    for (const auto& path : {path1, path2}) {
        Packager reader;
        REQUIRE(reader.Open(path.string()) == PackResult::Ok);

        std::vector<uint8_t> extracted;
        REQUIRE(reader.Extract("cycle.dds", extracted) == PackResult::Ok);
        REQUIRE(extracted == cooked.data);
    }

    CleanupTempDir(tmp);
}


// ==========================================================================
//  Section 11: Package Command Generation — Path Handling
// ==========================================================================

TEST_CASE("ReleasePipeline: BuildProjectShellCommand produces forward-slash paths",
          "[pipeline][command_gen][paths]") {
    const std::filesystem::path projectRoot = "/tmp/test-project";
    const std::filesystem::path sdkPrefix = "/opt/horo-sdk";

    const std::string cmd = BuildProjectShellCommand(
        projectRoot, sdkPrefix, BuildConfig::Release);

    // On all platforms, the generated command must NOT contain backslashes
    REQUIRE(cmd.find('\\') == std::string::npos);

    // Must contain the project root and SDK prefix
    REQUIRE(cmd.find("/tmp/test-project") != std::string::npos);
    REQUIRE(cmd.find("/opt/horo-sdk") != std::string::npos);

    // Must contain cmake --fresh invocation
    REQUIRE(cmd.find("cmake --fresh") != std::string::npos);
    REQUIRE(cmd.find("-DCMAKE_PREFIX_PATH=") != std::string::npos);
    REQUIRE(cmd.find("-DCMAKE_BUILD_TYPE=") != std::string::npos);
}

TEST_CASE("ReleasePipeline: BuildProjectShellCommand with different configs",
          "[pipeline][command_gen][paths]") {
    const std::filesystem::path projectRoot = "/tmp/test-proj";
    const std::filesystem::path sdkPrefix = "/opt/sdk";

    SECTION("Debug config") {
        const std::string cmd = BuildProjectShellCommand(
            projectRoot, sdkPrefix, BuildConfig::Debug);
        REQUIRE(cmd.find("-DCMAKE_BUILD_TYPE=Debug") != std::string::npos);
    }

    SECTION("Release config") {
        const std::string cmd = BuildProjectShellCommand(
            projectRoot, sdkPrefix, BuildConfig::Release);
        REQUIRE(cmd.find("-DCMAKE_BUILD_TYPE=Release") != std::string::npos);
    }

    SECTION("MinSizeRel config") {
        const std::string cmd = BuildProjectShellCommand(
            projectRoot, sdkPrefix, BuildConfig::MinSizeRel);
        REQUIRE(cmd.find("-DCMAKE_BUILD_TYPE=MinSizeRel") != std::string::npos);
    }
}

// ==========================================================================
//  Section 12: Package Command Generation — With/Without Output Path
// ==========================================================================

TEST_CASE("ReleasePipeline: BuildPackageShellCommand without output path — build only",
          "[pipeline][command_gen][package]") {
    const std::filesystem::path projectRoot = "/tmp/test-project";
    const std::filesystem::path sdkPrefix = "/opt/horo-sdk";

    const std::string cmd = BuildPackageShellCommand(
        projectRoot, sdkPrefix, BuildConfig::Release,
        std::filesystem::path{}, std::filesystem::path{});

    // Must contain the build command
    REQUIRE(cmd.find("cmake --fresh") != std::string::npos);

    // Must NOT contain packaging commands
    REQUIRE(cmd.find("cmake -E copy_directory") == std::string::npos);
    REQUIRE(cmd.find("horopak pack") == std::string::npos);
}

TEST_CASE("ReleasePipeline: BuildPackageShellCommand with output path — full package",
          "[pipeline][command_gen][package]") {
    const std::filesystem::path projectRoot = "/tmp/test-project";
    const std::filesystem::path sdkPrefix = "/opt/horo-sdk";
    const std::filesystem::path outputPath = "/tmp/output/v1.0.0_macos_arm64";

    const std::string cmd = BuildPackageShellCommand(
        projectRoot, sdkPrefix, BuildConfig::Release, outputPath, outputPath / "assets.horo");

    // Must contain the build command first
    REQUIRE(cmd.find("cmake --fresh") != std::string::npos);

    // Must contain packaging commands
    REQUIRE(cmd.find("cmake -E rm -rf") != std::string::npos);
    REQUIRE(cmd.find("cmake -E make_directory") != std::string::npos);
    REQUIRE(cmd.find("cmake -E copy_directory") != std::string::npos);
    REQUIRE(cmd.find("pack --project-root") != std::string::npos);

    // Must reference the output path
    REQUIRE(cmd.find("/tmp/output/v1.0.0_macos_arm64") != std::string::npos);

    // Must contain compression flag
    REQUIRE(cmd.find("--compression 9") != std::string::npos);

    // Verify command ordering: build → rm output → mkdir output → copy → archive
    const auto build_pos = cmd.find("cmake --fresh");
    const auto rm_pos = cmd.find("cmake -E rm -rf");
    const auto mkdir_pos = cmd.find("cmake -E make_directory");
    const auto copy_pos = cmd.find("cmake -E copy_directory");
    const auto pak_pos = cmd.find("horopak pack");

    REQUIRE(build_pos < rm_pos);
    REQUIRE(rm_pos < mkdir_pos);
    REQUIRE(mkdir_pos < copy_pos);
    REQUIRE(copy_pos < pak_pos);
}

// ==========================================================================
//  Section 13: Command Plan Generation
// ==========================================================================

TEST_CASE("ReleasePipeline: CreateBuildCommandPlan produces correct executable",
          "[pipeline][command_plan]") {
    const std::filesystem::path projectRoot = "/tmp/test-project";

    const auto plan = CreateBuildCommandPlan(projectRoot, BuildConfig::Release);

#if defined(_WIN32)
    REQUIRE(plan.executable == "cmd");
    REQUIRE(plan.args.size() == 2);
    REQUIRE(plan.args[0] == "/C");
#else
    REQUIRE(plan.executable == "/bin/sh");
    REQUIRE(plan.args.size() == 2);
    REQUIRE(plan.args[0] == "-c");
#endif

    REQUIRE(plan.workingDirectory == "/tmp/test-project");
    REQUIRE(plan.debugString.find("cmake --fresh") != std::string::npos);
}

TEST_CASE("ReleasePipeline: CreateBuildCommandPlan from draft includes packaging",
          "[pipeline][command_plan]") {
    BuildPipelineDraft draft;
    draft.outputRoot = "/tmp/output";
    draft.versionTag = "v1.0.0";

    BuildJob job;
    job.os = BuildTargetOS::MacOS;
    job.arch = BuildArch::Arm64;
    job.config = BuildConfig::Release;

    const std::filesystem::path projectRoot = "/tmp/test-project";

    const auto plan = CreateBuildCommandPlan(draft, job, projectRoot);

    REQUIRE(plan.debugString.find("cmake --fresh") != std::string::npos);
    REQUIRE(plan.debugString.find("Info.plist") != std::string::npos);
    REQUIRE(plan.debugString.find("Contents/Resources/assets.horo") != std::string::npos);
}

TEST_CASE("ReleasePipeline: CreateBuildCommandPlan separates raw args from redacted debugString",
          "[pipeline][command_plan][redaction]") {
    BuildPipelineDraft draft;
    draft.outputRoot = "/tmp/output";
    draft.versionTag = "v1.0.0";

    BuildJob job;
    job.os = BuildTargetOS::Windows;
    job.arch = BuildArch::x86_64;
    job.config = BuildConfig::Release;

    const std::filesystem::path projectRoot = "/tmp/test-project";

    const auto plan = CreateBuildCommandPlan(draft, job, projectRoot);

    // args[1] is the raw shell command — must contain the unredacted flag
    REQUIRE(plan.args.size() >= 2);
    REQUIRE(plan.args[1].find("--password") != std::string::npos);

    // debugString is redacted — must NOT leak the env var reference
    // adjacent to --password flag
    REQUIRE(plan.debugString.find("--password ***") != std::string::npos);

    // The env var name may legitimately appear in shell conditionals
    // (e.g. [ -n "${HORO_RELEASE_ARCHIVE_PASSWORD:-}" ]) which is NOT a secret.
    // The test only asserts the flag VALUE is redacted.

    // args[1] and debugString should differ when sensitive content is present
    // (debugString is redacted, args[1] is raw)
    REQUIRE(plan.args[1] != plan.debugString);
}

TEST_CASE("ReleasePipeline: BuildCommandForJob returns redacted command for display",
          "[pipeline][command_plan][redaction]") {
    BuildPipelineDraft draft;
    draft.outputRoot = "/tmp/output";
    draft.versionTag = "v1.0.0";

    BuildJob job;
    job.os = BuildTargetOS::Windows;
    job.arch = BuildArch::x86_64;
    job.config = BuildConfig::Release;

    const std::filesystem::path projectRoot = "/tmp/test-project";

    // BuildCommandForJob returns debugString — must be redacted
    const std::string cmd = BuildCommandForJob(draft, job, projectRoot);

    // Must contain the --password flag with redacted value
    REQUIRE(cmd.find("--password ***") != std::string::npos);

    // Must NOT contain the env var reference as the password VALUE
    // (the env var name may appear in shell conditionals elsewhere)
    const auto pwPos = cmd.find("--password");
    REQUIRE(pwPos != std::string::npos);
    // Value starts after "--password " (11 chars)
    const auto afterPw = cmd.substr(pwPos + 11);
    // Value ends at next space, quote, semicolon, or end of string
    const auto valEnd = afterPw.find_first_of(" \"';");
    const std::string pwValue =
        (valEnd == std::string::npos) ? afterPw : afterPw.substr(0, valEnd);
    REQUIRE(pwValue.find("***") != std::string::npos);
}

// ==========================================================================
//  Section 13.5: Toolchain command generation
// ==========================================================================

TEST_CASE("ReleasePipeline: command planner constructs cross-toolchain shell commands",
          "[pipeline][command_plan][toolchain]") {
    BuildPipelineDraft draft;
    draft.outputRoot = "/tmp/output";
    draft.versionTag = "v1.0.0";

    BuildJob job;
#if defined(_WIN32)
    job.os = BuildTargetOS::Linux;
#else
    job.os = BuildTargetOS::Windows;
#endif
    job.arch = BuildArch::x86_64;
    job.config = BuildConfig::Release;

    const std::filesystem::path projectRoot = "/tmp/test-project";

    ToolchainSettingsStore store;
    ToolchainConfig tc;
    tc.name = "Cross compiler";
    tc.targetTriple = ReleaseTargetTriple{job.os, job.arch};
    tc.enabled = true;
    tc.cmakeToolchainFilePath = "/opt/mingw/toolchain.cmake";
    tc.cmakeGenerator = "Ninja";
    tc.compilerPath = "/usr/bin/x86_64-w64-mingw32-g++";
    tc.sysrootPath = "/usr/x86_64-w64-mingw32/sys-root";
    tc.cmakePath = "/usr/local/bin/cmake";
    // Force validation pass
    tc.lastValidationResult.status = ToolchainValidationResult::Status::Valid;
    store.AddToolchain(tc);

    const auto plan = CreateBuildCommandPlan(draft, job, projectRoot, &store);

    // Should use the custom cmake executable
#if defined(_WIN32)
    REQUIRE(plan.debugString.find("\"/usr/local/bin/cmake\" --fresh") !=
            std::string::npos);

    // Should inject toolchain properties
    REQUIRE(plan.debugString.find("-G \"Ninja\"") != std::string::npos);
    REQUIRE(plan.debugString.find(
                "-DCMAKE_TOOLCHAIN_FILE=\"/opt/mingw/toolchain.cmake\"") !=
            std::string::npos);
    REQUIRE(plan.debugString.find(
                "-DCMAKE_SYSROOT=\"/usr/x86_64-w64-mingw32/sys-root\"") !=
            std::string::npos);
    REQUIRE(plan.debugString.find(
                "-DCMAKE_C_COMPILER=\"/usr/bin/x86_64-w64-mingw32-g++\"") !=
            std::string::npos);
    REQUIRE(plan.debugString.find(
                "-DCMAKE_CXX_COMPILER=\"/usr/bin/x86_64-w64-mingw32-g++\"") !=
            std::string::npos);
#else
    REQUIRE(plan.debugString.find("'/usr/local/bin/cmake' --fresh") != std::string::npos);

    // Should inject toolchain properties
    REQUIRE(plan.debugString.find("-G 'Ninja'") != std::string::npos);
    REQUIRE(plan.debugString.find("-DCMAKE_TOOLCHAIN_FILE='/opt/mingw/toolchain.cmake'") != std::string::npos);
    REQUIRE(plan.debugString.find("-DCMAKE_SYSROOT='/usr/x86_64-w64-mingw32/sys-root'") != std::string::npos);
    REQUIRE(plan.debugString.find("-DCMAKE_C_COMPILER='/usr/bin/x86_64-w64-mingw32-g++'") != std::string::npos);
    REQUIRE(plan.debugString.find("-DCMAKE_CXX_COMPILER='/usr/bin/x86_64-w64-mingw32-g++'") != std::string::npos);
#endif
}

TEST_CASE("ReleasePipeline: command planner fails gracefully on invalid/missing toolchains",
          "[pipeline][command_plan][toolchain]") {
    BuildPipelineDraft draft;
    draft.outputRoot = "/tmp/output";
    draft.versionTag = "v1.0.0";

    BuildJob job;
#if defined(_WIN32)
    job.os = BuildTargetOS::MacOS; // Unbuildable locally by default on Windows
#elif defined(__APPLE__)
    job.os = BuildTargetOS::Windows; // Unbuildable locally by default on macOS
#else
    job.os = BuildTargetOS::Windows; // Unbuildable locally by default on Linux
#endif
    job.arch = BuildArch::x86_64;
    job.config = BuildConfig::Release;

    const std::filesystem::path projectRoot = "/tmp/test-project";

    ToolchainSettingsStore store; // Empty store

    const std::string expectedError =
        std::string("Build blocked: ") + GetBuildTargetOSLabel(job.os) +
        " x86_64 target cannot be built locally. Reason: No cross-compilation toolchain configured for this target.";

    // Expect an exception because the capability evaluates to disabled
    REQUIRE_THROWS_WITH(
        CreateBuildCommandPlan(draft, job, projectRoot, &store),
        Catch::Matchers::Equals(expectedError));
}

// ==========================================================================
//  Section 14: ResolveJobOutputPath
// ==========================================================================

TEST_CASE("ReleasePipeline: ResolveJobOutputPath uses draft outputRoot",
          "[pipeline][output_path]") {
    BuildPipelineDraft draft;
    draft.outputRoot = "/custom/output";
    draft.versionTag = "v2.0.0";

    BuildJob job;
    job.os = BuildTargetOS::Windows;
    job.arch = BuildArch::x86_64;
    job.config = BuildConfig::Release;

    const std::string path = ResolveJobOutputPath(
        draft, job, std::filesystem::path{"/tmp/project"});

    REQUIRE(path.find("/custom/output") != std::string::npos);
    REQUIRE(path.find("v2.0.0") != std::string::npos);
    REQUIRE(path.find("windows") != std::string::npos);
    REQUIRE(path.find("x86_64") != std::string::npos);
}

TEST_CASE("ReleasePipeline: ResolveJobOutputPath falls back to default",
          "[pipeline][output_path]") {
    BuildPipelineDraft draft;

    BuildJob job;
    job.os = BuildTargetOS::Linux;
    job.arch = BuildArch::x86_64;

    const std::filesystem::path projectRoot = "/tmp/my-project";
    const std::string path = ResolveJobOutputPath(draft, job, projectRoot);

    REQUIRE(path.find("linux") != std::string::npos);
}

TEST_CASE("ReleasePipeline: ResolveJobOutputPath — all three platforms distinct",
          "[pipeline][output_path]") {
    BuildPipelineDraft draft;
    draft.outputRoot = "/out";
    draft.versionTag = "v1.0.0";

    const std::filesystem::path projectRoot = "/tmp/proj";

    const std::string winPath = ResolveJobOutputPath(
        draft, MakePendingJob(BuildTargetOS::Windows, BuildArch::x86_64,
                              BuildConfig::Release), projectRoot);
    const std::string macPath = ResolveJobOutputPath(
        draft, MakePendingJob(BuildTargetOS::MacOS, BuildArch::Arm64,
                              BuildConfig::Release), projectRoot);
    const std::string linuxPath = ResolveJobOutputPath(
        draft, MakePendingJob(BuildTargetOS::Linux, BuildArch::x86_64,
                              BuildConfig::Release), projectRoot);

    REQUIRE(winPath != macPath);
    REQUIRE(macPath != linuxPath);
    REQUIRE(winPath != linuxPath);
    REQUIRE(winPath.find("windows") != std::string::npos);
    REQUIRE(macPath.find("macos") != std::string::npos);
    REQUIRE(linuxPath.find("linux") != std::string::npos);
}

// ==========================================================================
//  Section 15: Temp Directory / Staging File Cleanup
// ==========================================================================

TEST_CASE("ReleasePipeline: Packager Write leaves no temp staging file on success",
          "[pipeline][temp_cleanup]") {
    const auto tmp = MakeTempDir("e2e_temp_cleanup");
    const auto archive_path = tmp / "temp_test.horo";

    const auto rgba = MakeSolidRGBA(32, 32, 100, 100, 100, 255);
    TextureCookSettings settings;
    settings.format = CompressedFormat::BC7_RGBA;
    settings.generateMips = false;

    auto cooked = CookTextureFromRGBA(rgba.data(), 32, 32, settings);
    REQUIRE(cooked);

    InMemoryAssetProvider provider;
    provider.Add("test.dds", cooked.data);

    Packager packer;
    packer.SetCompressionLevel(0);
    REQUIRE(packer.AddAsset("test.dds") == PackResult::Ok);
    REQUIRE(packer.Write(archive_path.string(), provider) == PackResult::Ok);

    REQUIRE(fs::exists(archive_path));

    const auto temp_path = fs::path(archive_path.string() + ".horo.tmp");
    REQUIRE_FALSE(fs::exists(temp_path));

    CleanupTempDir(tmp);
}

TEST_CASE("ReleasePipeline: Packager Write + Open cycle leaves no temp residue",
          "[pipeline][temp_cleanup]") {
    const auto tmp = MakeTempDir("e2e_temp_residue");
    const auto archive_path = tmp / "residue_test.horo";

    const auto rgba = MakeSolidRGBA(16, 16, 200, 100, 50, 255);
    TextureCookSettings settings;
    settings.format = CompressedFormat::BC7_RGBA;
    settings.generateMips = false;

    auto cooked = CookTextureFromRGBA(rgba.data(), 16, 16, settings);
    REQUIRE(cooked);

    InMemoryAssetProvider provider;
    provider.Add("asset.dds", cooked.data);

    // Write
    {
        Packager p;
        p.SetCompressionLevel(1);
        REQUIRE(p.AddAsset("asset.dds") == PackResult::Ok);
        REQUIRE(p.Write(archive_path.string(), provider) == PackResult::Ok);
    }

    // Read
    {
        Packager reader;
        REQUIRE(reader.Open(archive_path.string()) == PackResult::Ok);
        std::vector<uint8_t> out;
        REQUIRE(reader.Extract("asset.dds", out) == PackResult::Ok);
        REQUIRE(out == cooked.data);
    }

    // Overwrite
    {
        Packager p;
        p.SetCompressionLevel(1);
        REQUIRE(p.AddAsset("asset.dds") == PackResult::Ok);
        REQUIRE(p.Write(archive_path.string(), provider) == PackResult::Ok);
    }

    const auto temp_path = fs::path(archive_path.string() + ".horo.tmp");
    REQUIRE_FALSE(fs::exists(temp_path));

    // Still valid
    {
        Packager reader;
        REQUIRE(reader.Open(archive_path.string()) == PackResult::Ok);
        std::vector<uint8_t> out;
        REQUIRE(reader.Extract("asset.dds", out) == PackResult::Ok);
        REQUIRE(out == cooked.data);
    }

    CleanupTempDir(tmp);
}

TEST_CASE("ReleasePipeline: ExtractAll creates no temp files in output dir",
          "[pipeline][temp_cleanup]") {
    const auto tmp = MakeTempDir("e2e_extract_clean");
    const auto archive_path = tmp / "extract_test.horo";
    const auto out_dir = tmp / "extracted_output";

    const auto rgba = MakeSolidRGBA(32, 32, 50, 100, 200, 255);
    TextureCookSettings settings;
    settings.format = CompressedFormat::BC7_RGBA;
    settings.generateMips = false;

    auto cooked = CookTextureFromRGBA(rgba.data(), 32, 32, settings);
    REQUIRE(cooked);

    InMemoryAssetProvider provider;
    provider.Add("sprites/hero.dds", cooked.data);

    {
        Packager p;
        p.SetCompressionLevel(0);
        REQUIRE(p.AddAsset("sprites/hero.dds") == PackResult::Ok);
        REQUIRE(p.Write(archive_path.string(), provider) == PackResult::Ok);
    }

    {
        Packager reader;
        REQUIRE(reader.Open(archive_path.string()) == PackResult::Ok);
        REQUIRE(reader.ExtractAll(out_dir.string()) == PackResult::Ok);
    }

    REQUIRE(fs::exists(out_dir / "sprites/hero.dds"));

    bool found_temp = false;
    for (const auto& entry : fs::recursive_directory_iterator(out_dir)) {
        if (entry.path().extension() == ".tmp" ||
            entry.path().filename().string().find(".horo.tmp") != std::string::npos) {
            found_temp = true;
            break;
        }
    }
    REQUIRE_FALSE(found_temp);

    CleanupTempDir(tmp);
}

// ==========================================================================
//  Section 16: RebuildJobsForSelection and PlatformSelection
// ==========================================================================

TEST_CASE("ReleasePipeline: RebuildJobsForSelection restores correct job count",
          "[pipeline][jobs]") {
    BuildPipelineDraft draft;
    draft.versionTag = "v1.0.0";

    PlatformSelection sel;
    sel.windowsSelected = true;
    sel.macOSSelected = true;
    sel.linuxSelected = true;

    RebuildJobsForSelection(draft, sel);
    REQUIRE(draft.jobs.size() == 3);

    bool has_windows = false, has_macos = false, has_linux = false;
    for (const auto& j : draft.jobs) {
        if (j.os == BuildTargetOS::Windows) has_windows = true;
        if (j.os == BuildTargetOS::MacOS) has_macos = true;
        if (j.os == BuildTargetOS::Linux) has_linux = true;
    }
    REQUIRE(has_windows);
    REQUIRE(has_macos);
    REQUIRE(has_linux);
}

TEST_CASE("ReleasePipeline: RebuildJobsForSelection — single platform",
          "[pipeline][jobs]") {
    BuildPipelineDraft draft;

    PlatformSelection sel;
    sel.windowsSelected = true;

    RebuildJobsForSelection(draft, sel);
    REQUIRE(draft.jobs.size() == 1);
    REQUIRE(draft.jobs[0].os == BuildTargetOS::Windows);
}

TEST_CASE("ReleasePipeline: RebuildJobsForSelection — no selection falls back to host",
          "[pipeline][jobs]") {
    BuildPipelineDraft draft;
    PlatformSelection sel;

    RebuildJobsForSelection(draft, sel);
    REQUIRE(draft.jobs.size() == 1);

#if defined(_WIN32)
    REQUIRE(draft.jobs[0].os == BuildTargetOS::Windows);
#elif defined(__APPLE__)
    REQUIRE(draft.jobs[0].os == BuildTargetOS::MacOS);
#else
    REQUIRE(draft.jobs[0].os == BuildTargetOS::Linux);
#endif
}

TEST_CASE("ReleasePipeline: GetPlatformSelection mirrors job configuration",
          "[pipeline][jobs]") {
    BuildPipelineDraft draft;
    draft.jobs.push_back(
        MakePendingJob(BuildTargetOS::Windows, BuildArch::x86_64, BuildConfig::Release));
    draft.jobs.push_back(
        MakePendingJob(BuildTargetOS::MacOS, BuildArch::Arm64, BuildConfig::Release));

    const auto sel = GetPlatformSelection(draft);
    REQUIRE(sel.windowsSelected);
    REQUIRE(sel.macOSSelected);
    REQUIRE_FALSE(sel.linuxSelected);
}

// ==========================================================================
//  Section 17: Progress and Status Helpers
// ==========================================================================

TEST_CASE("ReleasePipeline: ComputeTotalProgress all pending returns 0",
          "[pipeline][progress]") {
    BuildPipelineDraft draft;
    draft.jobs.push_back(
        MakePendingJob(BuildTargetOS::Windows, BuildArch::x86_64, BuildConfig::Release));
    draft.jobs.push_back(
        MakePendingJob(BuildTargetOS::MacOS, BuildArch::Arm64, BuildConfig::Release));
    REQUIRE(ComputeTotalProgress(draft) == 0);
}

TEST_CASE("ReleasePipeline: ComputeTotalProgress all success returns 100",
          "[pipeline][progress]") {
    BuildPipelineDraft draft;
    auto j1 = MakePendingJob(BuildTargetOS::Windows, BuildArch::x86_64, BuildConfig::Release);
    j1.status = BuildJobStatus::Success;
    auto j2 = MakePendingJob(BuildTargetOS::MacOS, BuildArch::Arm64, BuildConfig::Release);
    j2.status = BuildJobStatus::Success;
    draft.jobs.push_back(j1);
    draft.jobs.push_back(j2);
    REQUIRE(ComputeTotalProgress(draft) == 100);
}

TEST_CASE("ReleasePipeline: ComputeTotalProgress mixed statuses",
          "[pipeline][progress]") {
    BuildPipelineDraft draft;
    auto j1 = MakePendingJob(BuildTargetOS::Windows, BuildArch::x86_64, BuildConfig::Release);
    j1.status = BuildJobStatus::Success;
    auto j2 = MakePendingJob(BuildTargetOS::MacOS, BuildArch::Arm64, BuildConfig::Release);
    j2.status = BuildJobStatus::Failed;
    auto j3 = MakePendingJob(BuildTargetOS::Linux, BuildArch::x86_64, BuildConfig::Release);
    j3.status = BuildJobStatus::Building;
    draft.jobs.push_back(j1);
    draft.jobs.push_back(j2);
    draft.jobs.push_back(j3);
    REQUIRE(ComputeTotalProgress(draft) == 66);
}

TEST_CASE("ReleasePipeline: IsAnyBuildRunning detects active job",
          "[pipeline][progress]") {
    BuildPipelineDraft draft;
    auto j1 = MakePendingJob(BuildTargetOS::Windows, BuildArch::x86_64, BuildConfig::Release);
    j1.status = BuildJobStatus::Building;
    draft.jobs.push_back(j1);
    REQUIRE(IsAnyBuildRunning(draft));
}

TEST_CASE("ReleasePipeline: IsAnyBuildRunning false when all idle",
          "[pipeline][progress]") {
    BuildPipelineDraft draft;
    auto j1 = MakePendingJob(BuildTargetOS::Windows, BuildArch::x86_64, BuildConfig::Release);
    j1.status = BuildJobStatus::Success;
    draft.jobs.push_back(j1);
    REQUIRE_FALSE(IsAnyBuildRunning(draft));
}

// ==========================================================================
//  Section 18: Timestamp and Label Functions
// ==========================================================================

TEST_CASE("ReleasePipeline: DefaultVersionTag returns non-empty string",
          "[pipeline][config]") {
    REQUIRE_FALSE(DefaultVersionTag().empty());
}

TEST_CASE("ReleasePipeline: CurrentTimestamp returns ISO-8601 format",
          "[pipeline][config]") {
    const std::string ts = CurrentTimestamp();
    REQUIRE_FALSE(ts.empty());
    REQUIRE(ts.size() >= 20);
    REQUIRE(ts.find('T') != std::string::npos);
    REQUIRE(ts.find('Z') != std::string::npos);
}

TEST_CASE("ReleasePipeline: FormatRecentRunTimestamp handles valid input",
          "[pipeline][config]") {
    const std::string formatted = FormatRecentRunTimestamp("2026-05-25T14:00:00Z");
    REQUIRE_FALSE(formatted.empty());
}

TEST_CASE("ReleasePipeline: FormatRecentRunTimestamp handles short input",
          "[pipeline][config]") {
    const std::string formatted = FormatRecentRunTimestamp("short");
    REQUIRE(formatted == "short");
}

TEST_CASE("ReleasePipeline: GetBuildTargetOSLabel covers all values",
          "[pipeline][labels]") {
    REQUIRE(std::string(GetBuildTargetOSLabel(BuildTargetOS::Windows)) == "Windows");
    REQUIRE(std::string(GetBuildTargetOSLabel(BuildTargetOS::MacOS)) == "macOS");
    REQUIRE(std::string(GetBuildTargetOSLabel(BuildTargetOS::Linux)) == "Linux");
}

TEST_CASE("ReleasePipeline: GetBuildConfigLabel covers all values",
          "[pipeline][labels]") {
    REQUIRE(std::string(GetBuildConfigLabel(BuildConfig::Debug)) == "Debug");
    REQUIRE(std::string(GetBuildConfigLabel(BuildConfig::Release)) == "Release");
    REQUIRE(std::string(GetBuildConfigLabel(BuildConfig::MinSizeRel)) == "MinSizeRel");
}

TEST_CASE("ReleasePipeline: GetBuildArchLabel covers all values",
          "[pipeline][labels]") {
    REQUIRE(std::string(GetBuildArchLabel(BuildArch::x86_64)) == "x86_64");
    REQUIRE(std::string(GetBuildArchLabel(BuildArch::Arm64)) == "arm64");
}

TEST_CASE("ReleasePipeline: GetBuildJobStatusLabel covers all values",
          "[pipeline][labels]") {
    REQUIRE(std::string(GetBuildJobStatusLabel(BuildJobStatus::Pending)) == "Pending");
    REQUIRE(std::string(GetBuildJobStatusLabel(BuildJobStatus::Building)) == "Building");
    REQUIRE(std::string(GetBuildJobStatusLabel(BuildJobStatus::Success)) == "Success");
    REQUIRE(std::string(GetBuildJobStatusLabel(BuildJobStatus::Failed)) == "Failed");
    REQUIRE(std::string(GetBuildJobStatusLabel(BuildJobStatus::Cancelled)) == "Cancelled");
}

TEST_CASE("ReleasePipeline: GetBuildPipelineStateLabel covers all states",
          "[pipeline][labels]") {
    using enum BuildPipelineState;
    REQUIRE(std::string(GetBuildPipelineStateLabel(Idle)) == "Idle");
    REQUIRE(std::string(GetBuildPipelineStateLabel(Configuring)) == "Configuring");
    REQUIRE(std::string(GetBuildPipelineStateLabel(Building)) == "Building");
    REQUIRE(std::string(GetBuildPipelineStateLabel(Packaging)) == "Packaging");
    REQUIRE(std::string(GetBuildPipelineStateLabel(Downloading)) == "Downloading");
    REQUIRE(std::string(GetBuildPipelineStateLabel(Done)) == "Done");
    REQUIRE(std::string(GetBuildPipelineStateLabel(Error)) == "Error");
}

TEST_CASE("ReleasePipeline: IsTerminalBuildPipelineState identifies terminal states",
          "[pipeline][state]") {
    using enum BuildPipelineState;
    REQUIRE(IsTerminalBuildPipelineState(Done));
    REQUIRE(IsTerminalBuildPipelineState(Error));
    REQUIRE_FALSE(IsTerminalBuildPipelineState(Idle));
    REQUIRE_FALSE(IsTerminalBuildPipelineState(Configuring));
    REQUIRE_FALSE(IsTerminalBuildPipelineState(Building));
    REQUIRE_FALSE(IsTerminalBuildPipelineState(Packaging));
    REQUIRE_FALSE(IsTerminalBuildPipelineState(Downloading));
}

TEST_CASE("ReleasePipeline: CanTransitionBuildPipelineState — valid forward transitions",
          "[pipeline][state]") {
    using enum BuildPipelineState;
    // Idle → Configuring
    REQUIRE(CanTransitionBuildPipelineState(Idle, Configuring));
    // Configuring → Building
    REQUIRE(CanTransitionBuildPipelineState(Configuring, Building));
    // Building → Packaging
    REQUIRE(CanTransitionBuildPipelineState(Building, Packaging));
    // Building → Error
    REQUIRE(CanTransitionBuildPipelineState(Building, Error));
    // Packaging → Downloading
    REQUIRE(CanTransitionBuildPipelineState(Packaging, Downloading));
    // Packaging → Error
    REQUIRE(CanTransitionBuildPipelineState(Packaging, Error));
    // Downloading → Done
    REQUIRE(CanTransitionBuildPipelineState(Downloading, Done));
}

TEST_CASE("ReleasePipeline: CanTransitionBuildPipelineState — reset from anywhere",
          "[pipeline][state]") {
    using enum BuildPipelineState;
    // Any state can reset to Idle or Configuring.
    REQUIRE(CanTransitionBuildPipelineState(Idle, Idle));
    REQUIRE(CanTransitionBuildPipelineState(Building, Idle));
    REQUIRE(CanTransitionBuildPipelineState(Done, Idle));
    REQUIRE(CanTransitionBuildPipelineState(Error, Idle));
    REQUIRE(CanTransitionBuildPipelineState(Idle, Configuring));
    REQUIRE(CanTransitionBuildPipelineState(Building, Configuring));
    REQUIRE(CanTransitionBuildPipelineState(Done, Configuring));
    REQUIRE(CanTransitionBuildPipelineState(Error, Configuring));
    // Reset from Configuring back to Idle.
    REQUIRE(CanTransitionBuildPipelineState(Configuring, Idle));
}

TEST_CASE("ReleasePipeline: CanTransitionBuildPipelineState — invalid transitions",
          "[pipeline][state]") {
    using enum BuildPipelineState;
    // Cannot skip steps without explicit allowance.
    REQUIRE_FALSE(CanTransitionBuildPipelineState(Idle, Building));
    REQUIRE_FALSE(CanTransitionBuildPipelineState(Idle, Done));
    REQUIRE_FALSE(CanTransitionBuildPipelineState(Configuring, Done));
    // Building → Done is valid for local builds (packaging inline).
    // Building → Downloading is explicitly allowed for CI builds.
    REQUIRE_FALSE(CanTransitionBuildPipelineState(Done, Building));       // Terminal forward
    REQUIRE_FALSE(CanTransitionBuildPipelineState(Error, Building));      // Terminal forward
}



// ==========================================================================
//  Section 19: Build History Persistence
// ==========================================================================

TEST_CASE("ReleasePipeline: BuildHistory round-trip through JSON",
          "[pipeline][history]") {
    const auto tmp = MakeTempDir("e2e_history");
    const auto history_path = tmp / "build_history.json";

    BuildHistoryEntry entry;
    entry.versionTag = "v2.1.0";
    entry.timestamp = "2026-05-25T14:00:00Z";
    entry.allSucceeded = true;

    BuildJob job;
    job.os = BuildTargetOS::MacOS;
    job.arch = BuildArch::Arm64;
    job.config = BuildConfig::Release;
    job.status = BuildJobStatus::Success;
    job.exitCode = 0;
    job.outputPath = "/tmp/out/v2.1.0_macos_arm64";
    job.timestamp = "2026-05-25T14:00:00Z";
    entry.jobs.push_back(job);

    std::vector<BuildHistoryEntry> entries = { entry };

    WriteHistoryJson(history_path, entries);
    REQUIRE(fs::exists(history_path));

    const auto restored = ReadHistoryJson(history_path);
    REQUIRE(restored.size() == 1);
    REQUIRE(restored[0].versionTag == "v2.1.0");
    REQUIRE(restored[0].allSucceeded);
    REQUIRE(restored[0].jobs.size() == 1);
    REQUIRE(restored[0].jobs[0].os == BuildTargetOS::MacOS);
    REQUIRE(restored[0].jobs[0].arch == BuildArch::Arm64);
    REQUIRE(restored[0].jobs[0].config == BuildConfig::Release);
    REQUIRE(restored[0].jobs[0].status == BuildJobStatus::Success);

    CleanupTempDir(tmp);
}

TEST_CASE("ReleasePipeline: ReadHistoryJson on non-existent file returns empty",
          "[pipeline][history]") {
    const auto entries = ReadHistoryJson("/tmp/horo_nonexistent_history.json");
    REQUIRE(entries.empty());
}

// ==========================================================================
//  Section 20: Command Wrappers
// ==========================================================================

TEST_CASE("ReleasePipeline: BuildCommandForJob returns command string",
          "[pipeline][command_gen]") {
    BuildPipelineDraft draft;
    draft.outputRoot = "/tmp/out";
    draft.versionTag = "v1.0.0";

    BuildJob job;
    job.os = BuildTargetOS::MacOS;
    job.arch = BuildArch::Arm64;
    job.config = BuildConfig::Release;

    const std::filesystem::path projectRoot = "/tmp/proj";
    const std::string cmd = BuildCommandForJob(draft, job, projectRoot);

    REQUIRE(cmd.find("cmake --fresh") != std::string::npos);
}

TEST_CASE("ReleasePipeline: SignCommandForJob returns empty when signing disabled",
          "[pipeline][signing]") {
    BuildPipelineDraft draft;
    draft.signing.enabled = false;

    BuildJob job;
    job.os = BuildTargetOS::MacOS;

    REQUIRE(SignCommandForJob(draft, job).empty());
}

TEST_CASE("ReleasePipeline: SignCommandForJob returns empty for Linux",
          "[pipeline][signing]") {
    BuildPipelineDraft draft;
    draft.signing.enabled = true;

    BuildJob job;
    job.os = BuildTargetOS::Linux;

    REQUIRE(SignCommandForJob(draft, job).empty());
}

TEST_CASE("ReleasePipeline: ResolveBuildSdkPrefix returns non-empty path",
          "[pipeline][sdk]") {
    Horo::ProjectPath::SetSdkRoot("/tmp/fake-sdk");
    const auto prefix = ResolveBuildSdkPrefix();
    REQUIRE_FALSE(prefix.empty());
}

TEST_CASE("ReleasePipeline: CurrentBuildConfig returns Release for empty draft",
          "[pipeline][config]") {
    BuildPipelineDraft draft;
    REQUIRE(CurrentBuildConfig(draft) == BuildConfig::Release);
}

TEST_CASE("ReleasePipeline: CurrentBuildConfig returns first job config",
          "[pipeline][config]") {
    BuildPipelineDraft draft;
    draft.jobs.push_back(
        MakePendingJob(BuildTargetOS::Windows, BuildArch::x86_64, BuildConfig::Debug));
    REQUIRE(CurrentBuildConfig(draft) == BuildConfig::Debug);
}

// ==========================================================================
//  Section 21: Windows/POSIX Path Separator Normalisation
// ==========================================================================

TEST_CASE("ReleasePipeline: BuildProjectShellCommand normalises backslashes to forward slashes",
          "[pipeline][command_gen][paths][cross_platform]") {
    // Simulate Windows-style paths with backslashes.
    // generic_string() (used by QuoteShellArg) converts backslashes to forward slashes.
    const std::filesystem::path projectRoot("C:\\Users\\dev\\MyGame");
    const std::filesystem::path sdkPrefix("C:\\Program Files\\HoroSDK");

    const std::string cmd = BuildProjectShellCommand(
        projectRoot, sdkPrefix, BuildConfig::Release);

    // No backslashes in generated command on Windows. On POSIX, backslashes are valid chars.
#if defined(_WIN32)
    REQUIRE(cmd.find('\\') == std::string::npos);
    REQUIRE(cmd.find("C:/Users/dev/MyGame") != std::string::npos);
    REQUIRE(cmd.find("C:/Program Files/HoroSDK") != std::string::npos);
#else
    REQUIRE(cmd.find("C:\\Users\\dev\\MyGame") != std::string::npos);
    REQUIRE(cmd.find("C:\\Program Files\\HoroSDK") != std::string::npos);
#endif
    REQUIRE(cmd.find("cmake --fresh") != std::string::npos);
}

TEST_CASE("ReleasePipeline: BuildProjectShellCommand handles mixed separators",
          "[pipeline][command_gen][paths][cross_platform]") {
    // Mixed forward/backward slashes — should normalise to all forward
    const std::filesystem::path projectRoot("D:/dev\\GameProject");
    const std::filesystem::path sdkPrefix("E:/tools\\sdk");

    const std::string cmd = BuildProjectShellCommand(
        projectRoot, sdkPrefix, BuildConfig::Debug);

#if defined(_WIN32)
    REQUIRE(cmd.find('\\') == std::string::npos);
    REQUIRE(cmd.find("D:/dev/GameProject") != std::string::npos);
    REQUIRE(cmd.find("E:/tools/sdk") != std::string::npos);
#else
    REQUIRE(cmd.find("D:/dev\\GameProject") != std::string::npos);
    REQUIRE(cmd.find("E:/tools\\sdk") != std::string::npos);
#endif
}

TEST_CASE("ReleasePipeline: BuildPackageShellCommand normalises backslash outputPath",
          "[pipeline][command_gen][paths][cross_platform]") {
    const std::filesystem::path projectRoot("C:\\Projects\\game");
    const std::filesystem::path sdkPrefix("C:\\HoroSDK");
    const std::filesystem::path outputPath("D:\\output\\v1.0.0_windows_x86_64");

    const std::string cmd = BuildPackageShellCommand(
        projectRoot, sdkPrefix, BuildConfig::Release, outputPath, outputPath / "assets.horo");

#if defined(_WIN32)
    REQUIRE(cmd.find('\\') == std::string::npos);
    REQUIRE(cmd.find("D:/output/v1.0.0_windows_x86_64") != std::string::npos);
#else
    REQUIRE(cmd.find("D:\\output\\v1.0.0_windows_x86_64") != std::string::npos);
#endif
    REQUIRE(cmd.find("cmake --fresh") != std::string::npos);
    REQUIRE(cmd.find("cmake -E copy_directory") != std::string::npos);
    REQUIRE(cmd.find("pack --project-root") != std::string::npos);
}

TEST_CASE("ReleasePipeline: BuildProjectShellCommand — leading/trailing spaces in paths don't break output",
          "[pipeline][command_gen][paths][robustness]") {
    // Paths with leading/trailing spaces: filesystem::path preserves them,
    // but QuoteShellArg wraps in quotes, so the command is still valid.
    const std::filesystem::path projectRoot("/tmp/ my game /");
    const std::filesystem::path sdkPrefix("/opt/ horo sdk /");

    const std::string cmd = BuildProjectShellCommand(
        projectRoot, sdkPrefix, BuildConfig::Release);

    // Should not crash, should contain the quoted paths
    REQUIRE(cmd.find("cmake --fresh") != std::string::npos);
    // The path string will have trailing spaces preserved — verify the command is non-empty
    REQUIRE_FALSE(cmd.empty());
}

// ==========================================================================
//  Section 22: Extended Temp Directory Cleanup
// ==========================================================================

TEST_CASE("ReleasePipeline: MakeTempDir creates and isolates test directories",
          "[pipeline][temp_cleanup][helpers]") {
    const auto dir1 = MakeTempDir("isolation_a");
    const auto dir2 = MakeTempDir("isolation_b");

    REQUIRE(fs::exists(dir1));
    REQUIRE(fs::exists(dir2));
    REQUIRE(dir1 != dir2);
    REQUIRE(dir1.filename() == "isolation_a");
    REQUIRE(dir2.filename() == "isolation_b");

    // Should be subdirectories of the temp base
    REQUIRE(dir1.parent_path().filename() == "horo_e2e_test");

    CleanupTempDir(dir1);
    CleanupTempDir(dir2);

    REQUIRE_FALSE(fs::exists(dir1));
    REQUIRE_FALSE(fs::exists(dir2));
}

TEST_CASE("ReleasePipeline: MakeTempDir is idempotent — no stale state",
          "[pipeline][temp_cleanup][helpers]") {
    // Create, write a file, then re-create — the helper should clean and re-create
    const auto dir = MakeTempDir("idempotent_check");

    // Write a sentinel file
    const auto sentinel = dir / "sentinel.txt";
    {
        std::ofstream f(sentinel);
        f << "marker";
    }
    REQUIRE(fs::exists(sentinel));

    // Call MakeTempDir again — it removes_all, so sentinel should be gone
    const auto dir2 = MakeTempDir("idempotent_check");
    REQUIRE(dir == dir2);
    REQUIRE_FALSE(fs::exists(sentinel));

    CleanupTempDir(dir);
    REQUIRE_FALSE(fs::exists(dir));
}

TEST_CASE("ReleasePipeline: repeated packer Write cycles leave no temp residue",
          "[pipeline][temp_cleanup][packer]") {
    const auto tmp = MakeTempDir("e2e_cycle_cleanup");
    const auto archive_path = tmp / "cycle_cleanup.horo";

    const auto rgba = MakeSolidRGBA(16, 16, 100, 200, 50, 255);
    TextureCookSettings settings;
    settings.format = CompressedFormat::BC7_RGBA;
    settings.generateMips = false;

    auto cooked = CookTextureFromRGBA(rgba.data(), 16, 16, settings);
    REQUIRE(cooked);

    InMemoryAssetProvider provider;
    provider.Add("asset.dds", cooked.data);

    // Three write cycles with the same packer, clearing between each
    for (int cycle = 0; cycle < 3; ++cycle) {
        Packager p;
        p.SetCompressionLevel(0);
        REQUIRE(p.AddAsset("asset.dds") == PackResult::Ok);
        REQUIRE(p.Write(archive_path.string(), provider) == PackResult::Ok);

        // Verify no .tmp file remains
        const auto temp_path = fs::path(archive_path.string() + ".horo.tmp");
        REQUIRE_FALSE(fs::exists(temp_path));

        // Overwrite next cycle
    }

    CleanupTempDir(tmp);
    REQUIRE_FALSE(fs::exists(tmp));
}

TEST_CASE("ReleasePipeline: CleanupTempDir removes nested content",
          "[pipeline][temp_cleanup][helpers]") {
    const auto tmp = MakeTempDir("nested_cleanup");
    const auto nested = tmp / "a" / "b" / "c";
    fs::create_directories(nested);

    const auto file = nested / "data.txt";
    {
        std::ofstream f(file);
        f << "nested";
    }
    REQUIRE(fs::exists(file));

    CleanupTempDir(tmp);
    REQUIRE_FALSE(fs::exists(tmp));
    REQUIRE_FALSE(fs::exists(nested));
}


// ==========================================================================
//  Section 24: Extended Package Command Edge Cases
// ==========================================================================

TEST_CASE("ReleasePipeline: BuildPackageShellCommand with empty output path returns build-only",
          "[pipeline][command_gen][package][edge]") {
    const std::filesystem::path projectRoot("/tmp/my-project");
    const std::filesystem::path sdkPrefix("/opt/horo-sdk");
    const std::filesystem::path emptyOutput;

    const std::string cmd = BuildPackageShellCommand(
        projectRoot, sdkPrefix, BuildConfig::Release, emptyOutput, emptyOutput / "assets.horo");

    // Should be just the build command, no packaging
    REQUIRE(cmd.find("cmake --fresh") != std::string::npos);
    REQUIRE(cmd.find("cmake -E copy_directory") == std::string::npos);
    REQUIRE(cmd.find("horopak pack") == std::string::npos);

    // Verify it matches BuildProjectShellCommand output (same args)
    const std::string buildOnly = BuildProjectShellCommand(
        projectRoot, sdkPrefix, BuildConfig::Release);
    REQUIRE(cmd == buildOnly);
}

TEST_CASE("ReleasePipeline: BuildPackageShellCommand with special characters in paths",
          "[pipeline][command_gen][package][robustness]") {
    // Paths with spaces and special characters: the shell quoting
    // in QuoteShellArg must produce a valid shell command.
    const std::filesystem::path projectRoot("/tmp/My Game (v2)");
    const std::filesystem::path sdkPrefix("/opt/Horo SDK!");
    const std::filesystem::path outputPath("/tmp/output/My Game v1.0");

    const std::string cmd = BuildPackageShellCommand(
        projectRoot, sdkPrefix, BuildConfig::Release, outputPath, outputPath / "assets.horo");

    REQUIRE(cmd.find("cmake --fresh") != std::string::npos);
    REQUIRE(cmd.find("cmake -E copy_directory") != std::string::npos);
    REQUIRE_FALSE(cmd.empty());
}

TEST_CASE("ReleasePipeline: BuildCommandForJob — with explicit project root",
          "[pipeline][command_gen][integration]") {
    BuildPipelineDraft draft;
    draft.outputRoot = "/tmp/custom_out";
    draft.versionTag = "v3.0.0";

    BuildJob job;
    job.os = BuildTargetOS::MacOS;
    job.arch = BuildArch::Arm64;
    job.config = BuildConfig::Release;

    const std::filesystem::path projectRoot("/tmp/custom-project");
    const std::string cmd = BuildCommandForJob(draft, job, projectRoot);

    REQUIRE(cmd.find("cmake --fresh") != std::string::npos);
    REQUIRE(cmd.find("/tmp/custom-project") != std::string::npos);
    REQUIRE_FALSE(cmd.empty());
}

TEST_CASE("ReleasePipeline: BuildCommandForJob — Linux stages generated game release locally",
          "[pipeline][command_gen][linux]") {
    BuildPipelineDraft draft;
    draft.buildName = "TestGame";
    draft.gameVersion = "1.2.3";

    BuildJob job;
    job.os = BuildTargetOS::Linux;
    job.arch = BuildArch::x86_64;
    job.config = BuildConfig::Release;

    const std::filesystem::path projectRoot("/tmp/linux-game");
    const std::string cmd = BuildCommandForJob(draft, job, projectRoot);

    REQUIRE(cmd.find("package-linux.sh") == std::string::npos);
    REQUIRE(cmd.find("cmake -E copy_directory") != std::string::npos);
    REQUIRE(cmd.find("pack --project-root") != std::string::npos);
    REQUIRE(cmd.find("linux_x86_64") != std::string::npos);
    REQUIRE(cmd.find("assets.horo") != std::string::npos);
}

TEST_CASE("ReleasePipeline: macOS job packages generated game assets into app resources",
          "[pipeline][command_gen][package][macos]") {
    BuildPipelineDraft draft;
    draft.buildName = "MyHoroGame";
    draft.outputRoot = "/tmp/release-out";
    draft.versionTag = "v1.0.0";

    BuildJob job;
    job.os = BuildTargetOS::MacOS;
    job.arch = BuildArch::Arm64;
    job.config = BuildConfig::Release;

    const std::filesystem::path projectRoot("/tmp/MyHoroGame");
    const std::string cmd = BuildCommandForJob(draft, job, projectRoot);

    REQUIRE(cmd.find("scripts/package-macos.sh") == std::string::npos);
    REQUIRE(cmd.find("Info.plist") != std::string::npos);
    REQUIRE(cmd.find("Contents/MacOS/MyHoroGame") != std::string::npos);
    REQUIRE(cmd.find("MyHoroGame.app/Contents/Resources/assets") != std::string::npos);
    REQUIRE(cmd.find("MyHoroGame.app/Contents/Resources/assets.horo") != std::string::npos);
    REQUIRE(cmd.find("MyHoroGame.app/Contents/MacOS/shaders") != std::string::npos);
    REQUIRE(cmd.find("MyHoroGame.app/Contents/Resources/shaders") != std::string::npos);
    REQUIRE(cmd.find("pack --project-root") != std::string::npos);
}

TEST_CASE("ReleasePipeline: SignCommandForJob returns non-empty for macOS with notarization",
          "[pipeline][signing][macos]") {
    BuildPipelineDraft draft;
    draft.signing.enabled = true;
    draft.signing.notarize = true;
    draft.signing.teamId = "ABC123XYZ";

    BuildJob job;
    job.os = BuildTargetOS::MacOS;
    job.outputPath = "/tmp/MyGame.app";

    const std::string cmd = SignCommandForJob(draft, job);
    REQUIRE_FALSE(cmd.empty());
    REQUIRE(cmd.find("codesign") != std::string::npos);
    REQUIRE(cmd.find("ABC123XYZ") != std::string::npos);
}

TEST_CASE("ReleasePipeline: SignCommandForJob returns non-empty for Windows",
          "[pipeline][signing][windows]") {
    BuildPipelineDraft draft;
    draft.signing.enabled = true;
    draft.signing.certificatePath = "C:\\certs\\mycert.pfx";
    draft.signing.certificatePassword = Horo::Core::SecureString("test123");

    BuildJob job;
    job.os = BuildTargetOS::Windows;
    job.outputPath = "C:\\output\\MyGame.exe";

    const std::string cmd = SignCommandForJob(draft, job);
    REQUIRE_FALSE(cmd.empty());
    REQUIRE(cmd.find("signtool") != std::string::npos);
    REQUIRE(cmd.find("mycert.pfx") != std::string::npos);
}

TEST_CASE("ReleasePipeline: ResolveDefaultOutputRoot returns non-empty",
          "[pipeline][output_path]") {
    const std::string root = ResolveDefaultOutputRoot("/tmp/game-project");
    REQUIRE_FALSE(root.empty());
    REQUIRE(root.find("build/release") != std::string::npos);
}

// -- SemVer wrapper tests -------------------------------------------------

TEST_CASE("SemVer: IsZero returns true for default-constructed value",
          "[pipeline][semver]") {
    SemVer v;
    REQUIRE(v.IsZero());
    REQUIRE(v.major == 0);
    REQUIRE(v.minor == 0);
    REQUIRE(v.patch == 0);
}

TEST_CASE("SemVer: IsZero returns false for non-zero version",
          "[pipeline][semver]") {
    SemVer v{1, 2, 3};
    REQUIRE_FALSE(v.IsZero());
}

TEST_CASE("SemVer: ParseSemVer canonical form",
          "[pipeline][semver]") {
    SemVer v = ParseSemVer("1.2.3");
    REQUIRE_FALSE(v.IsZero());
    REQUIRE(v.major == 1);
    REQUIRE(v.minor == 2);
    REQUIRE(v.patch == 3);
}

TEST_CASE("SemVer: ParseSemVer with leading v",
          "[pipeline][semver]") {
    SemVer v = ParseSemVer("v4.5.6");
    REQUIRE_FALSE(v.IsZero());
    REQUIRE(v.major == 4);
    REQUIRE(v.minor == 5);
    REQUIRE(v.patch == 6);
}

TEST_CASE("SemVer: ParseSemVer returns zero on invalid input",
          "[pipeline][semver]") {
    SemVer v = ParseSemVer("not-a-version");
    REQUIRE(v.IsZero());
}

TEST_CASE("SemVer: ParseSemVer returns zero on empty string",
          "[pipeline][semver]") {
    SemVer v = ParseSemVer("");
    REQUIRE(v.IsZero());
}

TEST_CASE("SemVer: IsValidSemVer returns true for valid versions",
          "[pipeline][semver]") {
    REQUIRE(IsValidSemVer("1.2.3"));
    REQUIRE(IsValidSemVer("0.0.0"));
    REQUIRE(IsValidSemVer("v1.0.0"));
    REQUIRE(IsValidSemVer("10.20.30"));
}

TEST_CASE("SemVer: IsValidSemVer returns false for invalid inputs",
          "[pipeline][semver]") {
    REQUIRE_FALSE(IsValidSemVer(""));
    REQUIRE_FALSE(IsValidSemVer("not-semver"));
    REQUIRE_FALSE(IsValidSemVer("1.2"));
    REQUIRE_FALSE(IsValidSemVer("1.2.3.4"));
}

TEST_CASE("SemVer: BumpPatch increments patch",
          "[pipeline][semver]") {
    SemVer v{1, 2, 3};
    BumpPatch(v);
    REQUIRE(v.major == 1);
    REQUIRE(v.minor == 2);
    REQUIRE(v.patch == 4);
}

TEST_CASE("SemVer: BumpPatch works from zero patch",
          "[pipeline][semver]") {
    SemVer v{0, 1, 0};
    BumpPatch(v);
    REQUIRE(v.patch == 1);
}

TEST_CASE("SemVer: SemVerToString produces canonical form",
          "[pipeline][semver]") {
    REQUIRE(SemVerToString({1, 2, 3}) == "1.2.3");
    REQUIRE(SemVerToString({0, 0, 0}) == "0.0.0");
    REQUIRE(SemVerToString({10, 20, 30}) == "10.20.30");
}

TEST_CASE("SemVer: parse-bump-to-string round-trip",
          "[pipeline][semver]") {
    SemVer v = ParseSemVer("3.7.2");
    REQUIRE_FALSE(v.IsZero());
    BumpPatch(v);
    REQUIRE(SemVerToString(v) == "3.7.3");
}

// ==========================================================================
//  Section 25: Artifact Manifest and Checksum Flow (CLI Path)
//
//  Validates the library components that compose the CLI artifact flow:
//  archive → checksum → manifest generation → serialisation → verification.
//  These tests exercise the same code paths that horo-engine project release
//  invokes: BuildArtifactManifest, WriteArtifactManifest,
//  BuildManifestShellCommand, and the underlying ArtifactManifest serialisation.
// ==========================================================================

TEST_CASE("E2E: manifest — GenerateManifest populates all sections",
          "[e2e][manifest][cli_flow]") {
    ManifestArtifactInfo artifact;
    artifact.name = "TestGame";
    artifact.version = "2.5.0";
    artifact.buildNumber = "42";
    artifact.releaseChannel = "stable";

    ManifestContentsInfo contents;
    contents.assetCount = 12;
    contents.archivePath = "assets.horo";
    contents.archiveSizeBytes = 1048576;
    contents.archiveSha256 = "abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789";

    const auto manifest = GenerateManifest(artifact, "Release", contents, "Windows", "x86_64");

    // Top-level fields
    REQUIRE(manifest.manifestVersion == ArtifactManifest::kManifestVersion);
    REQUIRE_FALSE(manifest.engineVersion.empty());

    // Artifact section
    REQUIRE(manifest.artifact.name == "TestGame");
    REQUIRE(manifest.artifact.version == "2.5.0");
    REQUIRE(manifest.artifact.buildNumber == "42");
    REQUIRE(manifest.artifact.releaseChannel == "stable");

    // Build section
    REQUIRE(manifest.build.config == "Release");
    REQUIRE_FALSE(manifest.build.platform.empty());
    REQUIRE_FALSE(manifest.build.arch.empty());
    REQUIRE_FALSE(manifest.build.compiler.empty());
    REQUIRE_FALSE(manifest.build.timestamp.empty());
    REQUIRE(manifest.build.timestamp.find('T') != std::string::npos);
    REQUIRE(manifest.build.timestamp.find('Z') != std::string::npos);

    // Contents section
    REQUIRE(manifest.contents.assetCount == 12);
    REQUIRE(manifest.contents.archivePath == "assets.horo");
    REQUIRE(manifest.contents.archiveSizeBytes == 1048576);
    REQUIRE(manifest.contents.archiveSha256.size() == 64);
}

TEST_CASE("E2E: manifest — SerializeManifest produces valid JSON",
          "[e2e][manifest][cli_flow]") {
    ManifestArtifactInfo artifact;
    artifact.name = "JsonTest";
    artifact.version = "1.0.0";

    ManifestContentsInfo contents;
    contents.archivePath = "data.horo";

    const auto manifest = GenerateManifest(artifact, "Debug", contents, "Windows", "x86_64");
    const std::string json = SerializeManifest(manifest);

    REQUIRE_FALSE(json.empty());

    // Must be valid JSON with expected top-level keys
    REQUIRE(json.find("\"manifest_version\"") != std::string::npos);
    REQUIRE(json.find("\"engine_version\"") != std::string::npos);
    REQUIRE(json.find("\"artifact\"") != std::string::npos);
    REQUIRE(json.find("\"build\"") != std::string::npos);
    REQUIRE(json.find("\"contents\"") != std::string::npos);

    // Artifact fields must appear in JSON
    REQUIRE(json.find("\"JsonTest\"") != std::string::npos);
    REQUIRE(json.find("\"1.0.0\"") != std::string::npos);
}

TEST_CASE("E2E: manifest — SerializeManifest → DeserializeManifest round-trip",
          "[e2e][manifest][cli_flow]") {
    ManifestArtifactInfo artifact;
    artifact.name = "RoundTrip";
    artifact.version = "3.1.4";
    artifact.buildNumber = "7";
    artifact.releaseChannel = "beta";

    ManifestContentsInfo contents;
    contents.assetCount = 256;
    contents.archivePath = "assets.horo";
    contents.archiveSizeBytes = 2097152;
    contents.archiveSha256 = "deadbeef" + std::string(56, '0');

    const auto original = GenerateManifest(artifact, "MinSizeRel", contents, "Windows", "x86_64");
    const std::string json = SerializeManifest(original);

    ArtifactManifest parsed;
    REQUIRE(DeserializeManifest(json, parsed));

    // Verify round-trip fidelity
    REQUIRE(parsed.manifestVersion == original.manifestVersion);
    REQUIRE(parsed.engineVersion == original.engineVersion);
    REQUIRE(parsed.artifact.name == original.artifact.name);
    REQUIRE(parsed.artifact.version == original.artifact.version);
    REQUIRE(parsed.artifact.buildNumber == original.artifact.buildNumber);
    REQUIRE(parsed.artifact.releaseChannel == original.artifact.releaseChannel);
    REQUIRE(parsed.build.config == original.build.config);
    REQUIRE(parsed.build.platform == original.build.platform);
    REQUIRE(parsed.build.arch == original.build.arch);
    REQUIRE(parsed.build.compiler == original.build.compiler);
    REQUIRE(parsed.build.timestamp == original.build.timestamp);
    REQUIRE(parsed.contents.assetCount == original.contents.assetCount);
    REQUIRE(parsed.contents.archivePath == original.contents.archivePath);
    REQUIRE(parsed.contents.archiveSizeBytes == original.contents.archiveSizeBytes);
    REQUIRE(parsed.contents.archiveSha256 == original.contents.archiveSha256);
}

TEST_CASE("E2E: manifest — WriteManifestFile → ReadManifestFile round-trip",
          "[e2e][manifest][cli_flow]") {
    const auto tmp = MakeTempDir("e2e_manifest_file");

    ManifestArtifactInfo artifact;
    artifact.name = "FileTest";
    artifact.version = "0.9.0";

    ManifestContentsInfo contents;
    contents.archivePath = "assets.horo";
    contents.archiveSha256 = "aaaa" + std::string(60, 'b');

    const auto original = GenerateManifest(artifact, "Release", contents, "Windows", "x86_64");
    const auto manifestPath = tmp / ".manifest.json";

    REQUIRE(WriteManifestFile(manifestPath, original));
    REQUIRE(fs::exists(manifestPath));

    ArtifactManifest restored;
    REQUIRE(ReadManifestFile(manifestPath, restored));
    REQUIRE(restored.artifact.name == "FileTest");
    REQUIRE(restored.artifact.version == "0.9.0");
    REQUIRE(restored.contents.archiveSha256 == original.contents.archiveSha256);

    // Verify file is valid JSON on disk
    std::ifstream in(manifestPath);
    REQUIRE(in.good());
    std::string raw;
    std::getline(in, raw);
    REQUIRE(raw.find('{') != std::string::npos);

    CleanupTempDir(tmp);
}

TEST_CASE("E2E: manifest — BuildArtifactManifest from draft + job",
          "[e2e][manifest][cli_flow]") {
    BuildPipelineDraft draft;
    draft.buildName = "MyHoroGame";
    draft.gameVersion = "1.5.0";
    draft.buildNumber = "101";
    draft.releaseChannel = 1;  // stable

    BuildJob job;
    job.os = BuildTargetOS::MacOS;
    job.arch = BuildArch::Arm64;
    job.config = BuildConfig::Release;

    const std::filesystem::path projectRoot = "/tmp/fake-project";

    const ArtifactManifest manifest =
        BuildArtifactManifest(draft, job, projectRoot);

    // Artifact info from draft
    REQUIRE(manifest.artifact.name == "MyHoroGame");
    REQUIRE(manifest.artifact.version == "1.5.0");
    REQUIRE(manifest.artifact.buildNumber == "101");
    REQUIRE(manifest.artifact.releaseChannel == "1");

    // Build info
    REQUIRE(manifest.build.config == "Release");
    REQUIRE_FALSE(manifest.build.platform.empty());
    REQUIRE_FALSE(manifest.build.arch.empty());

    // Contents — archive path is always "assets.horo"
    REQUIRE(manifest.contents.archivePath == "assets.horo");

    // Engine version populated
    REQUIRE_FALSE(manifest.engineVersion.empty());
}

TEST_CASE("E2E: manifest — BuildManifestShellCommand produces valid command",
          "[e2e][manifest][cli_flow]") {
    BuildPipelineDraft draft;
    draft.buildName = "ShellCmdGame";
    draft.gameVersion = "2.0.0";

    BuildJob job;
    job.os = BuildTargetOS::MacOS;
    job.arch = BuildArch::Arm64;
    job.config = BuildConfig::Debug;

    const std::filesystem::path projectRoot = "/tmp/shell-game";
    const std::string cmd = BuildManifestShellCommand(draft, job, projectRoot);

    REQUIRE_FALSE(cmd.empty());

    // Must reference the manifest path
    REQUIRE(cmd.find(".manifest.json") != std::string::npos);

    // Must contain key manifest JSON fields (serialised inline)
    REQUIRE(cmd.find("\"manifest_version\"") != std::string::npos);
    REQUIRE(cmd.find("\"engine_version\"") != std::string::npos);

    CleanupTempDir(fs::temp_directory_path() / "horo_e2e_test" / "e2e_manifest_shell");
}

TEST_CASE("E2E: manifest — full archive → checksum → manifest flow",
          "[e2e][manifest][cli_flow][full]") {
    const auto tmp = MakeTempDir("e2e_manifest_full");
    const auto archive_path = tmp / "assets.horo";

    // 1. Create a cooked texture and pack it into an archive
    const auto rgba = MakeSolidRGBA(64, 64, 255, 128, 64, 255);
    TextureCookSettings cookSettings;
    cookSettings.format = CompressedFormat::BC7_RGBA;
    cookSettings.generateMips = false;

    auto cooked = CookTextureFromRGBA(rgba.data(), 64, 64, cookSettings);
    REQUIRE(cooked);

    InMemoryAssetProvider provider;
    provider.Add("textures/hero.dds", cooked.data);

    Packager packer;
    packer.SetCompressionLevel(1);
    packer.SetSHA256Enabled(true);
    REQUIRE(packer.AddAsset("textures/hero.dds") == PackResult::Ok);
    REQUIRE(packer.Write(archive_path.string(), provider) == PackResult::Ok);
    REQUIRE(fs::exists(archive_path));

    // 2. Compute SHA-256 of the archive and verify it's non-empty
    const std::string sha256 = ComputeSha256Hex(archive_path);
    REQUIRE_FALSE(sha256.empty());
    REQUIRE(sha256.size() == 64);
    // All hex chars
    for (char c : sha256)
        REQUIRE(((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')));

    // 3. Verify archive size is non-zero
    std::error_code ec;
    const auto fileSize = fs::file_size(archive_path, ec);
    REQUIRE_FALSE(ec);
    REQUIRE(fileSize > 0);

    // 4. Generate a manifest with the real checksum and size
    ManifestArtifactInfo artifact;
    artifact.name = "FullFlowGame";
    artifact.version = "1.0.0";

    ManifestContentsInfo contents;
    contents.assetCount = 1;
    contents.archivePath = "assets.horo";
    contents.archiveSizeBytes = fileSize;
    contents.archiveSha256 = sha256;

    const auto manifest = GenerateManifest(artifact, "Release", contents, "Windows", "x86_64");

    REQUIRE(manifest.contents.assetCount == 1);
    REQUIRE(manifest.contents.archivePath == "assets.horo");
    REQUIRE(manifest.contents.archiveSizeBytes == fileSize);
    REQUIRE(manifest.contents.archiveSha256 == sha256);

    // 5. Serialize and verify checksum survives round-trip
    const std::string json = SerializeManifest(manifest);
    ArtifactManifest parsed;
    REQUIRE(DeserializeManifest(json, parsed));
    REQUIRE(parsed.contents.archiveSha256 == sha256);
    REQUIRE(parsed.contents.archiveSizeBytes == fileSize);

    // 6. Write manifest alongside archive (simulating CLI output)
    const auto manifestPath = tmp / ".manifest.json";
    REQUIRE(WriteManifestFile(manifestPath, manifest));
    REQUIRE(fs::exists(manifestPath));

    // 7. Verify manifest file is larger than a bare JSON skeleton
    const auto manifestFileSize = fs::file_size(manifestPath, ec);
    REQUIRE_FALSE(ec);
    REQUIRE(manifestFileSize > 100);

    CleanupTempDir(tmp);
}

TEST_CASE("E2E: manifest — engine version matches compile-time version",
          "[e2e][manifest][cli_flow]") {
    ManifestArtifactInfo artifact;
    artifact.name = "VersionCheck";

    ManifestContentsInfo contents;
    const auto manifest = GenerateManifest(artifact, "Release", contents, "Windows", "x86_64");

    // engineVersion from GenerateManifest uses EngineVersion()
    // which should match CurrentEngineVersion() from ReleasePipeline
    REQUIRE_FALSE(manifest.engineVersion.empty());
    REQUIRE(manifest.engineVersion == std::string(CurrentEngineVersion()));
}

TEST_CASE("E2E: manifest — DetectPlatform returns known platform",
          "[e2e][manifest][cli_flow]") {
    const std::string platform = DetectPlatform();
    REQUIRE_FALSE(platform.empty());

    // Must be one of the three supported platforms
    const bool valid = (platform == "Windows" ||
                        platform == "macOS" ||
                        platform == "Linux");
    REQUIRE(valid);
}

TEST_CASE("E2E: manifest — DetectArchitecture returns known arch",
          "[e2e][manifest][cli_flow]") {
    const std::string arch = DetectArchitecture();
    REQUIRE((arch == "x86_64" || arch == "arm64"));
}

TEST_CASE("E2E: manifest — DetectCompiler returns non-empty string",
          "[e2e][manifest][cli_flow]") {
    const std::string compiler = DetectCompiler();
    REQUIRE_FALSE(compiler.empty());
    // Must contain at least a compiler name
    REQUIRE((compiler.find("Clang") != std::string::npos ||
             compiler.find("GCC") != std::string::npos ||
             compiler.find("MSVC") != std::string::npos ||
             compiler.find("Apple") != std::string::npos ||
             compiler == "Unknown"));
}

TEST_CASE("E2E: manifest — DeserializeManifest rejects invalid JSON",
          "[e2e][manifest][cli_flow]") {
    ArtifactManifest out;
    REQUIRE_FALSE(DeserializeManifest("not json at all", out));
    REQUIRE_FALSE(DeserializeManifest("", out));
    REQUIRE_FALSE(DeserializeManifest("[]", out));  // array, not object
}

TEST_CASE("E2E: manifest — DeserializeManifest handles missing sections gracefully",
          "[e2e][manifest][cli_flow]") {
    // JSON with only manifest_version — all other sections should default
    const std::string minimalJson = R"({"manifest_version": "1"})";
    ArtifactManifest out;
    REQUIRE(DeserializeManifest(minimalJson, out));
    REQUIRE(out.manifestVersion == "1");
    REQUIRE(out.artifact.name.empty());  // defaults preserved
    REQUIRE(out.contents.assetCount == 0);
}

TEST_CASE("E2E: manifest — ComputeSha256Hex for non-existent file returns empty",
          "[e2e][manifest][cli_flow]") {
    const std::string hex = ComputeSha256Hex("/tmp/horo_nonexistent_for_sha256_test.xyz");
    REQUIRE(hex.empty());
}

TEST_CASE("E2E: manifest — ResolveGitSha returns known sentinel when no git",
          "[e2e][manifest][cli_flow]") {
    // When HORO_GIT_SHA is not set and no .git directory is available,
    // ResolveGitSha should return "unknown" rather than crashing.
    const std::string sha = ResolveGitSha("/tmp");
    // /tmp likely has no .git, so expect "unknown"
    // (If it happens to be a git repo, the SHA will be 40 chars)
    REQUIRE((sha == "unknown" || sha.size() == 40));
}
