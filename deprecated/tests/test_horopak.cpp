/** @file test_horopak.cpp
 *  @brief Comprehensive unit and integration tests for Phase 1 archive pipeline:
 *         HoroFormat binary validation, Packager pack/unpack/list/info,
 *         edge cases (corrupt archives, empty assets, error paths),
 *         concurrent reads, memory safety, and crypto integration.
 *
 *  Test coverage targets:
 *  - HoroHeader binary I/O (read/write/validate)
 *  - TOCEntry binary I/O and per-entry validation
 *  - Packager: pack → unpack round-trip (single, multiple, empty assets)
 *  - Packager: error handling (corrupt magic, bad version, invalid offsets)
 *  - Packager: ListAssets / Info after Open
 *  - Concurrent reads from the same archive (thread safety)
 *  - Move semantics and large data handling
 *  - Crypto: PBKDF2 wrong password, KCV mismatch, key material zeroing
 */
#include <catch2/catch_test_macros.hpp>

#include "core/AssetProvider.h"
#include "core/ProjectPath.h"
#include "core/archive/HoroFormat.h"
#include "core/archive/Packager.h"
#include "core/archive/HashVerifier.h"
#include "core/crypto/AESContext.h"

#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using namespace Horo::Archive;
using namespace Horo::Crypto;

namespace fs = std::filesystem;

// ═══════════════════════════════════════════════════════════════════════════
//  Test helpers
// ═══════════════════════════════════════════════════════════════════════════

namespace {

/** Return a unique temporary directory path for a test case.
 *  The directory is created by the caller. */
fs::path MakeTempDir(const std::string& test_name) {
    const auto base = fs::temp_directory_path() / "horo_test";
    const auto dir = base / test_name;
    std::error_code ec;
    fs::create_directories(dir, ec);
    fs::remove_all(dir, ec);  // Ensure clean state
    fs::create_directories(dir, ec);
    return dir;
}

/** Remove a temporary directory and all its contents. */
void CleanupTempDir(const fs::path& dir) {
    std::error_code ec;
    fs::remove_all(dir, ec);
}

/** Generate `count` random bytes using a deterministic seed for reproducibility. */
std::vector<uint8_t> MakeRandomData(size_t count, uint64_t seed = 42) {
    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<unsigned int> dist(0, 255);
    std::vector<uint8_t> data(count);
    for (auto& byte : data)
        byte = static_cast<uint8_t>(dist(rng));
    return data;
}

/** Asset data provider backed by an in-memory map (path → data). */
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

} // anonymous namespace

// ═══════════════════════════════════════════════════════════════════════════
//  Section 1: HoroFormat — Header binary I/O
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("HoroFormat: WriteHeader → ReadHeader round-trip", "[horopak][format][header]") {
    HoroHeader original{};
    std::memcpy(original.magic, kHoroMagic.data(), kHoroMagic.size());
    original.version = kHoroVersion;
    original.flags = 0;
    original.toc_count = 5;
    original.toc_offset = 1024;
    original.data_offset = 32;

    // Write to a stringstream
    std::stringstream ss(std::ios::binary | std::ios::in | std::ios::out);
    REQUIRE(WriteHeader(ss, original));

    // Read back
    HoroHeader restored{};
    ss.seekg(0);
    REQUIRE(ReadHeader(ss, restored));

    REQUIRE(std::memcmp(restored.magic, kHoroMagic.data(), 4) == 0);
    REQUIRE(restored.version == kHoroVersion);
    REQUIRE(restored.flags == 0);
    REQUIRE(restored.toc_count == 5);
    REQUIRE(restored.toc_offset == 1024);
    REQUIRE(restored.data_offset == 32);
}

TEST_CASE("HoroFormat: ReadHeader fails on empty stream", "[horopak][format][header][error]") {
    std::stringstream ss(std::ios::binary | std::ios::in | std::ios::out);
    HoroHeader header{};
    // Empty stream — no bytes to read
    REQUIRE_FALSE(ReadHeader(ss, header));
}

TEST_CASE("HoroFormat: ReadHeader fails on truncated stream", "[horopak][format][header][error]") {
    // Write only 10 bytes (less than sizeof(HoroHeader) == 32)
    std::stringstream ss(std::ios::binary | std::ios::in | std::ios::out);
    const char partial[10] = {};
    ss.write(partial, sizeof(partial));
    ss.seekg(0);

    HoroHeader header{};
    REQUIRE_FALSE(ReadHeader(ss, header));
}

// ═══════════════════════════════════════════════════════════════════════════
//  Section 2: HoroFormat — Header validation
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("HoroFormat::ValidateHeader accepts valid header", "[horopak][format][validate]") {
    HoroHeader header{};
    std::memcpy(header.magic, kHoroMagic.data(), kHoroMagic.size());
    header.version = kHoroVersion;
    header.flags = 0;
    header.toc_count = 3;
    header.toc_offset = 256;
    header.data_offset = 32;

    REQUIRE(ValidateHeader(header));
}

TEST_CASE("HoroFormat::ValidateHeader rejects wrong magic", "[horopak][format][validate][corrupt]") {
    HoroHeader header{};
    std::memcpy(header.magic, "XXXX", 4);  // Wrong magic
    header.version = kHoroVersion;
    header.flags = 0;
    header.toc_count = 1;
    header.toc_offset = 128;
    header.data_offset = 32;

    REQUIRE_FALSE(ValidateHeader(header));
}

TEST_CASE("HoroFormat::ValidateHeader rejects wrong magic 'horo' (lowercase)", "[horopak][format][validate][corrupt]") {
    HoroHeader header{};
    std::memcpy(header.magic, "horo", 4);  // Lowercase magic
    header.version = kHoroVersion;
    header.flags = 0;
    header.toc_count = 1;
    header.toc_offset = 128;
    header.data_offset = 32;

    REQUIRE_FALSE(ValidateHeader(header));
}

TEST_CASE("HoroFormat::ValidateHeader rejects unsupported version", "[horopak][format][validate][corrupt]") {
    HoroHeader header{};
    std::memcpy(header.magic, kHoroMagic.data(), kHoroMagic.size());
    header.version = 99;  // Future/invalid version
    header.flags = 0;
    header.toc_count = 1;
    header.toc_offset = 128;
    header.data_offset = 32;

    REQUIRE_FALSE(ValidateHeader(header));
}

TEST_CASE("HoroFormat::ValidateHeader rejects version zero", "[horopak][format][validate][corrupt]") {
    HoroHeader header{};
    std::memcpy(header.magic, kHoroMagic.data(), kHoroMagic.size());
    header.version = 0;
    header.flags = 0;
    header.toc_count = 1;
    header.toc_offset = 128;
    header.data_offset = 32;

    REQUIRE_FALSE(ValidateHeader(header));
}

TEST_CASE("HoroFormat::ValidateHeader rejects toc_count > kMaxTocEntries", "[horopak][format][validate][corrupt]") {
    HoroHeader header{};
    std::memcpy(header.magic, kHoroMagic.data(), kHoroMagic.size());
    header.version = kHoroVersion;
    header.flags = 0;
    header.toc_count = kMaxTocEntries + 1;
    header.toc_offset = 1024;
    header.data_offset = 32;

    REQUIRE_FALSE(ValidateHeader(header));
}

TEST_CASE("HoroFormat::ValidateHeader rejects toc_offset inside header", "[horopak][format][validate][corrupt]") {
    HoroHeader header{};
    std::memcpy(header.magic, kHoroMagic.data(), kHoroMagic.size());
    header.version = kHoroVersion;
    header.flags = 0;
    header.toc_count = 1;
    header.toc_offset = 16;  // Inside the header area (< sizeof(HoroHeader))
    header.data_offset = 32;

    REQUIRE_FALSE(ValidateHeader(header));
}

TEST_CASE("HoroFormat::ValidateHeader rejects data_offset inside header", "[horopak][format][validate][corrupt]") {
    HoroHeader header{};
    std::memcpy(header.magic, kHoroMagic.data(), kHoroMagic.size());
    header.version = kHoroVersion;
    header.flags = 0;
    header.toc_count = 1;
    header.toc_offset = 256;
    header.data_offset = 16;  // Inside header area

    REQUIRE_FALSE(ValidateHeader(header));
}

TEST_CASE("HoroFormat::ValidateHeader rejects data_offset beyond toc_offset", "[horopak][format][validate][corrupt]") {
    HoroHeader header{};
    std::memcpy(header.magic, kHoroMagic.data(), kHoroMagic.size());
    header.version = kHoroVersion;
    header.flags = 0;
    header.toc_count = 1;
    header.toc_offset = 128;
    header.data_offset = 256;  // data_offset must be < toc_offset

    REQUIRE_FALSE(ValidateHeader(header));
}

TEST_CASE("HoroFormat::ValidateHeader rejects reserved flag bits", "[horopak][format][validate][corrupt]") {
    HoroHeader header{};
    std::memcpy(header.magic, kHoroMagic.data(), kHoroMagic.size());
    header.version = kHoroVersion;
    header.flags = 0xF0000000u;  // Reserved upper 4 bits set
    header.toc_count = 1;
    header.toc_offset = 128;
    header.data_offset = 32;

    REQUIRE_FALSE(ValidateHeader(header));
}

// ═══════════════════════════════════════════════════════════════════════════
//  Section 3: HoroFormat — TOC I/O and validation
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("HoroFormat: WriteTOC → ReadTOC round-trip", "[horopak][format][toc]") {
    std::vector<TOCEntry> original(3);
    original[0].hash = 0xDEADBEEF00000001ULL;
    original[0].offset = 100;
    original[0].compressed_size = 200;
    original[0].uncompressed_size = 256;
    original[0].flags = 0;
    original[0].crc32 = 0;

    original[1].hash = 0xDEADBEEF00000002ULL;
    original[1].offset = 300;
    original[1].compressed_size = 150;
    original[1].uncompressed_size = 150;
    original[1].flags = static_cast<uint32_t>(TOCEntryFlags::Compressed);
    original[1].crc32 = 0;

    original[2].hash = 0xDEADBEEF00000003ULL;
    original[2].offset = 450;
    original[2].compressed_size = 500;
    original[2].uncompressed_size = 1024;
    original[2].flags = static_cast<uint32_t>(TOCEntryFlags::Compressed) |
                        static_cast<uint32_t>(TOCEntryFlags::Encrypted);
    original[2].crc32 = 0;

    std::stringstream ss(std::ios::binary | std::ios::in | std::ios::out);
    REQUIRE(WriteTOC(ss, original));

    ss.seekg(0);
    std::vector<TOCEntry> restored;
    REQUIRE(ReadTOC(ss, 3, restored));

    REQUIRE(restored.size() == 3);
    for (size_t i = 0; i < 3; ++i) {
        REQUIRE(restored[i].hash == original[i].hash);
        REQUIRE(restored[i].offset == original[i].offset);
        REQUIRE(restored[i].compressed_size == original[i].compressed_size);
        REQUIRE(restored[i].uncompressed_size == original[i].uncompressed_size);
        REQUIRE(restored[i].flags == original[i].flags);
        REQUIRE(restored[i].crc32 == original[i].crc32);
    }
}

TEST_CASE("HoroFormat: ReadTOC accepts count=0", "[horopak][format][toc]") {
    std::stringstream ss;
    std::vector<TOCEntry> entries;
    entries.push_back({});  // Pre-populated; should be cleared
    REQUIRE(ReadTOC(ss, 0, entries));
    REQUIRE(entries.empty());
}

TEST_CASE("HoroFormat: ReadTOC rejects count > kMaxTocEntries", "[horopak][format][toc][error]") {
    std::stringstream ss;
    std::vector<TOCEntry> entries;
    REQUIRE_FALSE(ReadTOC(ss, kMaxTocEntries + 1, entries));
}

TEST_CASE("HoroFormat: ReadTOC rejects entry with zero sizes but non-zero hash", "[horopak][format][toc][error]") {
    std::vector<TOCEntry> bad(1);
    bad[0].hash = 0x1234;       // Non-zero hash
    bad[0].compressed_size = 0; // Invalid: must be > 0 when hash != 0
    bad[0].uncompressed_size = 0;
    bad[0].flags = 0;
    bad[0].crc32 = 0;

    std::stringstream ss(std::ios::binary | std::ios::in | std::ios::out);
    REQUIRE(WriteTOC(ss, bad));

    ss.seekg(0);
    std::vector<TOCEntry> restored;
    REQUIRE_FALSE(ReadTOC(ss, 1, restored));
}

TEST_CASE("HoroFormat: ReadTOC accepts non-zero crc32 field", "[horopak][format][toc]") {
    std::vector<TOCEntry> entry(1);
    entry[0].hash = 0x5678;
    entry[0].compressed_size = 100;
    entry[0].uncompressed_size = 100;
    entry[0].flags = 0;
    entry[0].crc32 = 0xDEADBEEF;  // Non-zero CRC32 is valid

    std::stringstream ss(std::ios::binary | std::ios::in | std::ios::out);
    REQUIRE(WriteTOC(ss, entry));

    ss.seekg(0);
    std::vector<TOCEntry> restored;
    REQUIRE(ReadTOC(ss, 1, restored));
    REQUIRE(restored[0].crc32 == 0xDEADBEEF);
}

TEST_CASE("HoroFormat: ReadTOC rejects entry with reserved flag bits", "[horopak][format][toc][error]") {
    std::vector<TOCEntry> bad(1);
    bad[0].hash = 0x9ABC;
    bad[0].compressed_size = 100;
    bad[0].uncompressed_size = 100;
    bad[0].flags = 0xF0000000u;  // Reserved bits
    bad[0].crc32 = 0;

    std::stringstream ss(std::ios::binary | std::ios::in | std::ios::out);
    REQUIRE(WriteTOC(ss, bad));

    ss.seekg(0);
    std::vector<TOCEntry> restored;
    REQUIRE_FALSE(ReadTOC(ss, 1, restored));
}

// ═══════════════════════════════════════════════════════════════════════════
//  Section 4: Packager — pack / unpack round-trip (unencrypted)
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Packager: pack → Open → list → extract → verify (single asset)", "[horopak][packager][integration]") {
    const auto tmp = MakeTempDir("single_asset");
    const auto archive_path = tmp / "bundle.horo";

    InMemoryAssetProvider provider;
    const std::vector<uint8_t> asset_data = {'H', 'e', 'l', 'l', 'o', ' ', 'W', 'o', 'r', 'l', 'd'};
    provider.Add("hello.txt", asset_data);

    Packager packer;
    packer.SetCompressionLevel(0);  // No compression for deterministic size
    REQUIRE(packer.AddAsset("hello.txt") == PackResult::Ok);
    REQUIRE(packer.AssetCount() == 1);

    const PackResult write_res = packer.Write(archive_path.string(), provider);
    REQUIRE(write_res == PackResult::Ok);

    // Verify file exists
    REQUIRE(fs::exists(archive_path));

    // Open and list
    Packager reader;
    REQUIRE(reader.Open(archive_path.string()) == PackResult::Ok);
    REQUIRE(reader.IsOpen());

    std::vector<std::string> paths;
    REQUIRE(reader.ListAssets(paths) == PackResult::Ok);
    REQUIRE(paths.size() == 1);
    REQUIRE(paths[0] == "hello.txt");

    // Extract and verify
    std::vector<uint8_t> extracted;
    REQUIRE(reader.Extract("hello.txt", extracted) == PackResult::Ok);
    REQUIRE(extracted == asset_data);

    CleanupTempDir(tmp);
}

TEST_CASE("AssetProvider: packaged archive resolves absolute source asset paths",
          "[horopak][asset-provider][packaged-runtime]") {
    const auto tmp = MakeTempDir("asset_provider_absolute_source_path");
    const auto archivePath = tmp / "assets.horo";

    InMemoryAssetProvider provider;
    const std::vector<uint8_t> sceneData = {'{', '"', 'o', 'k', '"', ':', 't', 'r', 'u', 'e', '}'};
    provider.Add("scenes/level.json", sceneData);

    Packager packer;
    packer.SetCompressionLevel(0);
    REQUIRE(packer.AddAsset("scenes/level.json") == PackResult::Ok);
    REQUIRE(packer.Write(archivePath.string(), provider) == PackResult::Ok);

    const std::filesystem::path originalRoot = Horo::ProjectPath::Root();
    Horo::ProjectPath::SetProjectRoot(tmp);

    std::string error;
    const auto absoluteSourceScene =
        (std::filesystem::temp_directory_path() / "horo-source-tree" / "assets" /
         "scenes" / "level.json").generic_string();
    const auto text = Horo::ReadAssetText(absoluteSourceScene, &error);

    if (!originalRoot.empty())
        Horo::ProjectPath::SetProjectRoot(originalRoot);

    REQUIRE(text.has_value());
    CHECK(*text == "{\"ok\":true}");
    CHECK(error.empty());

    CleanupTempDir(tmp);
}

TEST_CASE("AssetProvider: packaged macOS app resources archive is discoverable",
          "[horopak][asset-provider][packaged-runtime][macos]") {
    const fs::path tmp = MakeTempDir("asset_provider_macos_resources");
    const fs::path appRoot = tmp / "MyHoroGame.app";
    const fs::path macosDir = appRoot / "Contents" / "MacOS";
    const fs::path resourcesDir = appRoot / "Contents" / "Resources";
    REQUIRE(fs::create_directories(macosDir));
    REQUIRE(fs::create_directories(resourcesDir));

    InMemoryAssetProvider provider;
    const std::vector<uint8_t> sceneData = {'{', '"', 'o', 'k', '"', ':', 't', 'r', 'u', 'e', '}'};
    provider.Add("scenes/level.json", sceneData);

    Packager packer;
    packer.SetCompressionLevel(0);
    REQUIRE(packer.AddAsset("scenes/level.json") == PackResult::Ok);
    REQUIRE(packer.Write((resourcesDir / "assets.horo").string(), provider) ==
            PackResult::Ok);

    const fs::path previousCwd = fs::current_path();
    const fs::path previousRoot = Horo::ProjectPath::Root();
    std::error_code ec;
    fs::current_path(macosDir, ec);
    REQUIRE_FALSE(ec);
    Horo::ProjectPath::SetProjectRoot(macosDir);

    std::string error;
    const auto text = Horo::ReadAssetText("assets/scenes/level.json", &error);

    fs::current_path(previousCwd, ec);
    if (!previousRoot.empty())
        Horo::ProjectPath::SetProjectRoot(previousRoot);
    else
        Horo::ProjectPath::SetProjectRoot({});

    REQUIRE(text.has_value());
    CHECK(*text == "{\"ok\":true}");
    CHECK(error.empty());

    CleanupTempDir(tmp);
}

TEST_CASE("Packager: pack → unpack → verify (multiple assets, various sizes)", "[horopak][packager][integration]") {
    const auto tmp = MakeTempDir("multi_asset");
    const auto archive_path = tmp / "bundle.horo";

    InMemoryAssetProvider provider;
    const std::vector<uint8_t> small_data = {1, 2, 3};
    const std::vector<uint8_t> medium_data = MakeRandomData(1024, 7);
    const std::vector<uint8_t> large_data = MakeRandomData(64 * 1024, 13);  // 64 KB

    provider.Add("small.bin", small_data);
    provider.Add("medium.bin", medium_data);
    provider.Add("large.bin", large_data);

    Packager packer;
    packer.SetCompressionLevel(1);
    REQUIRE(packer.AddAsset("small.bin") == PackResult::Ok);
    REQUIRE(packer.AddAsset("medium.bin") == PackResult::Ok);
    REQUIRE(packer.AddAsset("large.bin") == PackResult::Ok);
    REQUIRE(packer.AssetCount() == 3);

    REQUIRE(packer.Write(archive_path.string(), provider) == PackResult::Ok);

    // Open and extract all
    Packager reader;
    REQUIRE(reader.Open(archive_path.string()) == PackResult::Ok);

    std::vector<std::string> paths;
    REQUIRE(reader.ListAssets(paths) == PackResult::Ok);
    REQUIRE(paths.size() == 3);

    // Extract individually and verify each
    std::vector<uint8_t> out;
    REQUIRE(reader.Extract("small.bin", out) == PackResult::Ok);
    REQUIRE(out == small_data);

    REQUIRE(reader.Extract("medium.bin", out) == PackResult::Ok);
    REQUIRE(out == medium_data);

    REQUIRE(reader.Extract("large.bin", out) == PackResult::Ok);
    REQUIRE(out == large_data);

    // ExtractAll to disk
    const auto out_dir = tmp / "extracted";
    REQUIRE(reader.ExtractAll(out_dir.string()) == PackResult::Ok);
    REQUIRE(fs::exists(out_dir / "small.bin"));
    REQUIRE(fs::exists(out_dir / "medium.bin"));
    REQUIRE(fs::exists(out_dir / "large.bin"));

    CleanupTempDir(tmp);
}

TEST_CASE("Packager: round-trip with many assets (stress)", "[horopak][packager][stress]") {
    const auto tmp = MakeTempDir("many_assets");
    const auto archive_path = tmp / "bundle.horo";

    constexpr int kAssetCount = 100;
    InMemoryAssetProvider provider;
    std::unordered_map<std::string, std::vector<uint8_t>> originals;

    Packager packer;
    packer.SetCompressionLevel(1);
    for (int i = 0; i < kAssetCount; ++i) {
        const std::string path = "asset_" + std::to_string(i) + ".dat";
        auto data = MakeRandomData(static_cast<size_t>(100 + i * 10), static_cast<uint64_t>(i));
        provider.Add(path, data);
        originals[path] = data;
        REQUIRE(packer.AddAsset(path) == PackResult::Ok);
    }
    REQUIRE(packer.AssetCount() == kAssetCount);

    REQUIRE(packer.Write(archive_path.string(), provider) == PackResult::Ok);

    Packager reader;
    REQUIRE(reader.Open(archive_path.string()) == PackResult::Ok);

    std::vector<std::string> paths;
    REQUIRE(reader.ListAssets(paths) == PackResult::Ok);
    REQUIRE(paths.size() == static_cast<size_t>(kAssetCount));

    // Verify every asset
    for (const auto& [path, expected] : originals) {
        std::vector<uint8_t> out;
        REQUIRE(reader.Extract(path, out) == PackResult::Ok);
        REQUIRE(out == expected);
    }

    CleanupTempDir(tmp);
}

// ═══════════════════════════════════════════════════════════════════════════
//  Section 5: Packager — empty / edge case assets
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Packager: empty asset (zero bytes) is rejected by TOC validation", "[horopak][packager][edge][empty]") {
    const auto tmp = MakeTempDir("empty_asset");
    const auto archive_path = tmp / "bundle.horo";

    InMemoryAssetProvider provider;
    provider.Add("empty.txt", {});  // Zero-byte asset

    Packager packer;
    packer.SetCompressionLevel(0);
    REQUIRE(packer.AddAsset("empty.txt") == PackResult::Ok);
    // Write succeeds (data chunk is written), but TOC validation on read
    // rejects entries with non-zero hash and zero sizes.
    const PackResult write_res = packer.Write(archive_path.string(), provider);
    REQUIRE(write_res == PackResult::Ok);

    // Open fails: TOC entry has hash != 0 but compressed_size == uncompressed_size == 0
    Packager reader;
    const PackResult open_res = reader.Open(archive_path.string());
    REQUIRE(open_res == PackResult::InvalidTOC);

    CleanupTempDir(tmp);
}

TEST_CASE("Packager: single-byte asset round-trips", "[horopak][packager][edge]") {
    const auto tmp = MakeTempDir("one_byte");
    const auto archive_path = tmp / "bundle.horo";

    InMemoryAssetProvider provider;
    provider.Add("one.bin", {0xFF});

    Packager packer;
    packer.SetCompressionLevel(0);
    REQUIRE(packer.AddAsset("one.bin") == PackResult::Ok);
    REQUIRE(packer.Write(archive_path.string(), provider) == PackResult::Ok);

    Packager reader;
    REQUIRE(reader.Open(archive_path.string()) == PackResult::Ok);

    std::vector<uint8_t> extracted;
    REQUIRE(reader.Extract("one.bin", extracted) == PackResult::Ok);
    REQUIRE(extracted.size() == 1);
    REQUIRE(extracted[0] == 0xFF);

    CleanupTempDir(tmp);
}

TEST_CASE("Packager: Write fails when no assets registered", "[horopak][packager][error]") {
    Packager packer;
    InMemoryAssetProvider provider;
    provider.Add("x", {1, 2, 3});

    // No assets added → Write should fail
    REQUIRE(packer.Write("/tmp/test.horo", provider) == PackResult::InvalidInput);
}

TEST_CASE("Packager: Write fails with null provider", "[horopak][packager][error]") {
    Packager packer;
    REQUIRE(packer.AddAsset("test.txt") == PackResult::Ok);
    REQUIRE(packer.Write("/tmp/test.horo", nullptr) == PackResult::InvalidInput);
    packer.Clear();
}

// ═══════════════════════════════════════════════════════════════════════════
//  Section 6: Packager — error paths (corrupt archives)
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Packager::Open fails on non-existent file", "[horopak][packager][error][corrupt]") {
    Packager packer;
    REQUIRE(packer.Open("/tmp/horo_nonexistent_archive_xyz.horo") == PackResult::IoError);
    REQUIRE_FALSE(packer.IsOpen());
}

TEST_CASE("Packager::Open fails on empty file", "[horopak][packager][error][corrupt]") {
    const auto tmp = MakeTempDir("open_empty");
    const auto path = tmp / "empty.horo";

    // Create an empty file
    std::ofstream(path, std::ios::binary).close();

    Packager packer;
    const PackResult result = packer.Open(path.string());
    // Should fail because header can't be read
    REQUIRE(result != PackResult::Ok);
    REQUIRE_FALSE(packer.IsOpen());

    CleanupTempDir(tmp);
}

TEST_CASE("Packager::Open fails on file with invalid magic (garbage data)", "[horopak][packager][error][corrupt]") {
    const auto tmp = MakeTempDir("bad_magic");
    const auto path = tmp / "bad.horo";

    // Write a file with garbage data (no valid magic)
    {
        std::ofstream file(path, std::ios::binary);
        std::vector<uint8_t> garbage(64, 0xAA);
        file.write(reinterpret_cast<const char*>(garbage.data()),
                   static_cast<std::streamsize>(garbage.size()));
    }

    Packager packer;
    REQUIRE(packer.Open(path.string()) == PackResult::InvalidMagic);
    REQUIRE_FALSE(packer.IsOpen());

    CleanupTempDir(tmp);
}

TEST_CASE("Packager::Open fails on truncated header (file too small)", "[horopak][packager][error][corrupt]") {
    const auto tmp = MakeTempDir("truncated");
    const auto path = tmp / "trunc.horo";

    // Write only 4 bytes: valid magic, but no header body
    {
        std::ofstream file(path, std::ios::binary);
        file.write("HORO", 4);
    }

    Packager packer;
    // Should fail: stream has only 4 bytes, header is 32 bytes
    REQUIRE(packer.Open(path.string()) != PackResult::Ok);
    REQUIRE_FALSE(packer.IsOpen());

    CleanupTempDir(tmp);
}

TEST_CASE("Packager::Extract fails when no archive is open", "[horopak][packager][error]") {
    Packager packer;
    std::vector<uint8_t> out;
    REQUIRE(packer.Extract("any.txt", out) == PackResult::InvalidInput);
}

TEST_CASE("Packager::ListAssets fails when no archive is open", "[horopak][packager][error]") {
    Packager packer;
    std::vector<std::string> paths;
    REQUIRE(packer.ListAssets(paths) == PackResult::InvalidInput);
}

TEST_CASE("Packager::Extract fails for non-existent asset path", "[horopak][packager][error]") {
    const auto tmp = MakeTempDir("missing_asset");
    const auto archive_path = tmp / "bundle.horo";

    InMemoryAssetProvider provider;
    provider.Add("real.txt", {1, 2, 3});

    Packager packer;
    packer.AddAsset("real.txt");
    REQUIRE(packer.Write(archive_path.string(), provider) == PackResult::Ok);

    Packager reader;
    REQUIRE(reader.Open(archive_path.string()) == PackResult::Ok);

    std::vector<uint8_t> out;
    REQUIRE(reader.Extract("nonexistent.txt", out) == PackResult::InvalidPath);

    CleanupTempDir(tmp);
}

TEST_CASE("Packager::AddAsset rejects empty path", "[horopak][packager][error]") {
    Packager packer;
    REQUIRE(packer.AddAsset("") == PackResult::InvalidPath);
}

TEST_CASE("Packager::AddAsset rejects path with null bytes", "[horopak][packager][error]") {
    Packager packer;
    std::string bad_path = "test\0hidden.txt";
    bad_path.resize(16);  // Ensure the null byte is part of the string
    REQUIRE(packer.AddAsset(bad_path) == PackResult::InvalidPath);
}

// ═══════════════════════════════════════════════════════════════════════════
//  Section 7: Packager — re-open and re-use
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Packager: Clear removes pending assets", "[horopak][packager][lifecycle]") {
    Packager packer;
    REQUIRE(packer.AddAsset("a.txt") == PackResult::Ok);
    REQUIRE(packer.AddAsset("b.txt") == PackResult::Ok);
    REQUIRE(packer.AssetCount() == 2);

    packer.Clear();
    REQUIRE(packer.AssetCount() == 0);
}

TEST_CASE("Packager: Open replaces previous open archive", "[horopak][packager][lifecycle]") {
    const auto tmp = MakeTempDir("reopen");
    const auto path1 = tmp / "bundle1.horo";
    const auto path2 = tmp / "bundle2.horo";

    InMemoryAssetProvider provider;
    provider.Add("one.txt", {1});
    provider.Add("two.txt", {2, 2});

    // Archive 1: single asset
    {
        Packager p;
        p.AddAsset("one.txt");
        REQUIRE(p.Write(path1.string(), provider) == PackResult::Ok);
    }
    // Archive 2: different asset
    {
        Packager p;
        p.AddAsset("two.txt");
        REQUIRE(p.Write(path2.string(), provider) == PackResult::Ok);
    }

    // Open first, then second — second should replace
    Packager reader;
    REQUIRE(reader.Open(path1.string()) == PackResult::Ok);

    std::vector<std::string> paths;
    reader.ListAssets(paths);
    REQUIRE(paths.size() == 1);
    REQUIRE(paths[0] == "one.txt");

    // Re-open with different archive
    REQUIRE(reader.Open(path2.string()) == PackResult::Ok);
    reader.ListAssets(paths);
    REQUIRE(paths.size() == 1);
    REQUIRE(paths[0] == "two.txt");

    CleanupTempDir(tmp);
}

// ═══════════════════════════════════════════════════════════════════════════
//  Section 8: Concurrent reads
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Packager: concurrent reads from same archive (different Packager instances)",
          "[horopak][packager][concurrency]") {
    const auto tmp = MakeTempDir("concurrent");
    const auto archive_path = tmp / "bundle.horo";

    // Create a multi-asset archive
    InMemoryAssetProvider provider;
    constexpr int kAssetCount = 20;
    std::vector<std::string> asset_paths;
    for (int i = 0; i < kAssetCount; ++i) {
        const std::string path = "asset_" + std::to_string(i) + ".bin";
        provider.Add(path, MakeRandomData(256, static_cast<uint64_t>(i)));
        asset_paths.push_back(path);
    }

    {
        Packager p;
        for (const auto& path : asset_paths)
            REQUIRE(p.AddAsset(path) == PackResult::Ok);
        REQUIRE(p.Write(archive_path.string(), provider) == PackResult::Ok);
    }

    // Spawn multiple threads, each opening its own Packager and reading assets
    constexpr int kThreadCount = 4;
    std::vector<std::thread> threads;
    std::atomic<int> errors{0};

    for (int t = 0; t < kThreadCount; ++t) {
        threads.emplace_back([&, t]() {
            Packager reader;
            if (reader.Open(archive_path.string()) != PackResult::Ok) {
                ++errors;
                return;
            }

            // Each thread reads a subset of assets
            for (int i = t * 5; i < std::min(kAssetCount, (t + 1) * 5); ++i) {
                const std::string& path = asset_paths[static_cast<size_t>(i)];
                std::vector<uint8_t> data;
                if (reader.Extract(path, data) != PackResult::Ok) {
                    ++errors;
                }
                // Verify non-empty
                if (data.empty()) {
                    ++errors;
                }
            }
        });
    }

    for (auto& th : threads) th.join();

    REQUIRE(errors == 0);

    CleanupTempDir(tmp);
}

TEST_CASE("Packager: concurrent open + list (separate instances, same file)",
          "[horopak][packager][concurrency]") {
    const auto tmp = MakeTempDir("concurrent_list");
    const auto archive_path = tmp / "bundle.horo";

    InMemoryAssetProvider provider;
    provider.Add("data.bin", MakeRandomData(512, 1));

    {
        Packager p;
        p.AddAsset("data.bin");
        REQUIRE(p.Write(archive_path.string(), provider) == PackResult::Ok);
    }

    constexpr int kThreadCount = 8;
    std::vector<std::thread> threads;
    std::atomic<int> errors{0};

    for (int t = 0; t < kThreadCount; ++t) {
        threads.emplace_back([&]() {
            Packager reader;
            auto res = reader.Open(archive_path.string());
            if (res != PackResult::Ok) { ++errors; return; }

            std::vector<std::string> paths;
            res = reader.ListAssets(paths);
            if (res != PackResult::Ok) { ++errors; return; }
            if (paths.size() != 1) { ++errors; return; }
            if (paths[0] != "data.bin") { ++errors; }
        });
    }

    for (auto& th : threads) th.join();
    REQUIRE(errors == 0);

    CleanupTempDir(tmp);
}

// ═══════════════════════════════════════════════════════════════════════════
//  Section 9: Move semantics
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Packager: move constructor transfers state", "[horopak][packager][move]") {
    Packager src;
    REQUIRE(src.AddAsset("test.txt") == PackResult::Ok);
    REQUIRE(src.AssetCount() == 1);

    Packager dst(std::move(src));
    // src should be in a valid-but-unspecified state
    // dst should have the pending assets
    REQUIRE(dst.AssetCount() == 1);

    // dst should be usable
    const auto tmp = MakeTempDir("move_ctor");
    const auto archive_path = tmp / "bundle.horo";
    InMemoryAssetProvider provider;
    provider.Add("test.txt", {4, 5, 6});
    REQUIRE(dst.Write(archive_path.string(), provider) == PackResult::Ok);

    CleanupTempDir(tmp);
}

TEST_CASE("Packager: move assignment transfers state", "[horopak][packager][move]") {
    Packager src;
    REQUIRE(src.AddAsset("a.txt") == PackResult::Ok);
    REQUIRE(src.AssetCount() == 1);

    Packager dst;
    REQUIRE(dst.AddAsset("b.txt") == PackResult::Ok);

    dst = std::move(src);
    // dst should now have src's assets
    REQUIRE(dst.AssetCount() == 1);

    const auto tmp = MakeTempDir("move_assign");
    const auto archive_path = tmp / "bundle.horo";
    InMemoryAssetProvider provider;
    provider.Add("a.txt", {7, 8, 9});
    REQUIRE(dst.Write(archive_path.string(), provider) == PackResult::Ok);

    CleanupTempDir(tmp);
}

TEST_CASE("Packager: self-move-assignment is safe", "[horopak][packager][move][safety]") {
    Packager p;
    REQUIRE(p.AddAsset("self.txt") == PackResult::Ok);

    // Self-move: pathological but must not crash
    auto& ref = p;
    p = std::move(ref);

    // Should still be functional enough to add more assets
    REQUIRE(p.AddAsset("another.txt") == PackResult::Ok);
    REQUIRE(p.AssetCount() == 2);

    p.Clear();
}

// ═══════════════════════════════════════════════════════════════════════════
//  Section 10: Compression edge cases
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Packager: compression level 0 produces uncompressed data", "[horopak][packager][compression]") {
    const auto tmp = MakeTempDir("comp_off");
    const auto archive_path = tmp / "bundle.horo";

    InMemoryAssetProvider provider;
    const auto data = MakeRandomData(4096, 99);
    provider.Add("data.bin", data);

    Packager packer;
    packer.SetCompressionLevel(0);
    packer.AddAsset("data.bin");
    REQUIRE(packer.Write(archive_path.string(), provider) == PackResult::Ok);

    // Verify round-trip
    Packager reader;
    REQUIRE(reader.Open(archive_path.string()) == PackResult::Ok);
    std::vector<uint8_t> out;
    REQUIRE(reader.Extract("data.bin", out) == PackResult::Ok);
    REQUIRE(out == data);

    CleanupTempDir(tmp);
}

TEST_CASE("Packager: compression level 12 works and round-trips", "[horopak][packager][compression]") {
    const auto tmp = MakeTempDir("comp_max");
    const auto archive_path = tmp / "bundle.horo";

    InMemoryAssetProvider provider;
    // Highly compressible data
    std::vector<uint8_t> data(16384, 0x42);
    provider.Add("repeat.bin", data);

    Packager packer;
    packer.SetCompressionLevel(12);  // Max LZ4 HC
    packer.AddAsset("repeat.bin");
    REQUIRE(packer.Write(archive_path.string(), provider) == PackResult::Ok);

    Packager reader;
    REQUIRE(reader.Open(archive_path.string()) == PackResult::Ok);
    std::vector<uint8_t> out;
    REQUIRE(reader.Extract("repeat.bin", out) == PackResult::Ok);
    REQUIRE(out == data);

    // Compressed archive should be smaller than uncompressed data
    const auto file_size = fs::file_size(archive_path);
    REQUIRE(file_size > 0);
    REQUIRE(file_size < data.size() + 200);  // Should compress significantly

    CleanupTempDir(tmp);
}

// ═══════════════════════════════════════════════════════════════════════════
//  Section 11: Crypto — PBKDF2 wrong password detection
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Crypto: PBKDF2 wrong password produces different key", "[horopak][crypto][wrong_password]") {
    const uint8_t salt[] = {'s', 'a', 'l', 't'};
    constexpr uint32_t kIterations = 100'000;

    uint8_t correct_key[16]{};
    uint8_t wrong_key[16]{};

    REQUIRE(DeriveKeyPbkdf2("correct_password", 16, salt, 4, kIterations, correct_key, 16));
    REQUIRE(DeriveKeyPbkdf2("wrong_password", 14, salt, 4, kIterations, wrong_key, 16));

    // Different passwords → different keys
    REQUIRE(std::memcmp(correct_key, wrong_key, 16) != 0);
}

TEST_CASE("Crypto: PBKDF2 key mismatch detected via KCV", "[horopak][crypto][kcv]") {
    const char* password = "archive_password";
    const uint8_t salt[8] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};

    // Derive correct key
    uint8_t correct_key[16]{};
    REQUIRE(DeriveKeyPbkdf2(password, 16, salt, 8, kMinPbkdf2Iterations,
                            correct_key, 16));

    // Generate KCV from correct key
    AESContext ctx_correct;
    REQUIRE(ctx_correct.Init(correct_key));
    uint8_t correct_kcv[kKcvSize];
    ctx_correct.GenerateKcv(correct_kcv);

    // Derive wrong key
    uint8_t wrong_key[16]{};
    REQUIRE(DeriveKeyPbkdf2("wrong_password", 14, salt, 8, kMinPbkdf2Iterations,
                            wrong_key, 16));

    AESContext ctx_wrong;
    REQUIRE(ctx_wrong.Init(wrong_key));
    uint8_t wrong_kcv[kKcvSize];
    ctx_wrong.GenerateKcv(wrong_kcv);

    // KCVs must differ (password verification)
    REQUIRE(std::memcmp(correct_kcv, wrong_kcv, kKcvSize) != 0);

    // Clean up key material
    std::memset(correct_key, 0, sizeof(correct_key));
    std::memset(wrong_key, 0, sizeof(wrong_key));
}

TEST_CASE("Crypto: AES-CTR decrypt with wrong key produces garbage", "[horopak][crypto][wrong_key]") {
    // Encrypt with key A
    const uint8_t key_a[16] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                               0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};
    const uint8_t key_b[16] = {0x0f, 0x0e, 0x0d, 0x0c, 0x0b, 0x0a, 0x09, 0x08,
                               0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01, 0x00};

    const uint8_t plaintext[32] = "This is a secret message!!!..";
    uint8_t ciphertext[32];
    std::memcpy(ciphertext, plaintext, 32);

    // Encrypt with key A
    AESContext ctx_a;
    REQUIRE(ctx_a.Init(key_a));
    uint8_t counter[16]{};
    AesCtrProcess(ctx_a, counter, ciphertext, 32);

    // Ciphertext should differ from plaintext
    REQUIRE(std::memcmp(ciphertext, plaintext, 32) != 0);

    // Try to decrypt with key B — should NOT recover plaintext
    std::memset(counter, 0, 16);
    AESContext ctx_b;
    REQUIRE(ctx_b.Init(key_b));
    AesCtrProcess(ctx_b, counter, ciphertext, 32);

    // Should still be garbage (not equal to original plaintext)
    REQUIRE(std::memcmp(ciphertext, plaintext, 32) != 0);

    // Now decrypt with key A — should recover
    std::memset(counter, 0, 16);
    uint8_t ciphertext_copy[32];
    // Re-encrypt from scratch to get a fresh ciphertext
    std::memcpy(ciphertext_copy, plaintext, 32);
    AesCtrProcess(ctx_a, counter, ciphertext_copy, 32);
    std::memset(counter, 0, 16);
    AesCtrProcess(ctx_a, counter, ciphertext_copy, 32);
    REQUIRE(std::memcmp(ciphertext_copy, plaintext, 32) == 0);
}

// ═══════════════════════════════════════════════════════════════════════════
//  Section 12: Crypto — key material zeroing (sensitive data hygiene)
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Crypto: PBKDF2 output can be explicitly zeroed", "[horopak][crypto][safety]") {
    const uint8_t salt[] = {'s', 'a', 'l', 't'};
    uint8_t key[16]{};

    REQUIRE(DeriveKeyPbkdf2("secret", 6, salt, 4, 1000, key, 16));

    // Verify key is non-zero
    bool all_zero = true;
    for (int i = 0; i < 16; ++i) {
        if (key[i] != 0) { all_zero = false; break; }
    }
    REQUIRE_FALSE(all_zero);

    // Explicit zeroing
    std::memset(key, 0, sizeof(key));

    all_zero = true;
    for (int i = 0; i < 16; ++i) {
        if (key[i] != 0) { all_zero = false; break; }
    }
    REQUIRE(all_zero);
}

TEST_CASE("Crypto: AESContext re-Init with zeroed buffer produces valid state", "[horopak][crypto][safety]") {
    // Simulate key lifecycle: derive → use → zero
    uint8_t key[16];
    // Fill with known data
    for (int i = 0; i < 16; ++i) key[i] = static_cast<uint8_t>(i + 1);

    AESContext ctx;
    REQUIRE(ctx.Init(key));

    // Zero the key buffer
    std::memset(key, 0, sizeof(key));

    // Context should still be functional (it has its own copy)
    uint8_t block[16] = {'T', 'e', 's', 't', ' ', 'z', 'e', 'r',
                          'o', 'e', 'd', ' ', 'k', 'e', 'y', '!'};
    ctx.EncryptBlock(block);
    // After encryption, output should differ from input
    const uint8_t original[16] = {'T', 'e', 's', 't', ' ', 'z', 'e', 'r',
                                   'o', 'e', 'd', ' ', 'k', 'e', 'y', '!'};
    REQUIRE(std::memcmp(block, original, 16) != 0);
}

// ═══════════════════════════════════════════════════════════════════════════
//  Section 13: Archive information / metadata
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Packager: Info — archive metadata accessible after Open", "[horopak][packager][info]") {
    const auto tmp = MakeTempDir("info_test");
    const auto archive_path = tmp / "bundle.horo";

    InMemoryAssetProvider provider;
    provider.Add("readme.md", {'#', ' ', 'T', 'e', 's', 't'});
    provider.Add("config.json", {'{', '}'});

    Packager packer;
    packer.SetCompressionLevel(1);
    packer.AddAsset("readme.md");
    packer.AddAsset("config.json");
    REQUIRE(packer.Write(archive_path.string(), provider) == PackResult::Ok);

    Packager reader;
    REQUIRE(reader.Open(archive_path.string()) == PackResult::Ok);
    REQUIRE(reader.IsOpen());

    std::vector<std::string> paths;
    REQUIRE(reader.ListAssets(paths) == PackResult::Ok);
    REQUIRE(paths.size() == 2);

    // Verify file exists and has content
    const auto file_size = fs::file_size(archive_path);
    REQUIRE(file_size > 0);
    // Minimum: header (32) + data + TOC (3 entries × 32 = 96)
    REQUIRE(file_size >= 128);

    CleanupTempDir(tmp);
}

// ═══════════════════════════════════════════════════════════════════════════
//  Section 14: Large data / stress tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Packager: large asset (1 MB) round-trips correctly", "[horopak][packager][stress][large]") {
    const auto tmp = MakeTempDir("large_asset");
    const auto archive_path = tmp / "bundle.horo";

    InMemoryAssetProvider provider;
    const auto large_data = MakeRandomData(1024 * 1024, 12345);  // 1 MB
    provider.Add("large.bin", large_data);

    Packager packer;
    packer.SetCompressionLevel(1);  // LZ4 fast
    packer.AddAsset("large.bin");
    REQUIRE(packer.Write(archive_path.string(), provider) == PackResult::Ok);

    Packager reader;
    REQUIRE(reader.Open(archive_path.string()) == PackResult::Ok);

    std::vector<uint8_t> extracted;
    REQUIRE(reader.Extract("large.bin", extracted) == PackResult::Ok);
    REQUIRE(extracted.size() == large_data.size());
    REQUIRE(extracted == large_data);

    CleanupTempDir(tmp);
}

TEST_CASE("Packager: repeated pack/unpack cycles don't leak state", "[horopak][packager][stress][memory]") {
    const auto tmp = MakeTempDir("cycles");
    const auto path_a = tmp / "a.horo";
    const auto path_b = tmp / "b.horo";

    InMemoryAssetProvider provider;
    provider.Add("data.bin", MakeRandomData(4096, 42));

    Packager packer;

    // Cycle 1
    packer.AddAsset("data.bin");
    REQUIRE(packer.Write(path_a.string(), provider) == PackResult::Ok);
    packer.Clear();

    // Cycle 2
    packer.AddAsset("data.bin");
    REQUIRE(packer.Write(path_b.string(), provider) == PackResult::Ok);
    packer.Clear();

    // Both archives should be independently valid
    const auto expected_data = MakeRandomData(4096, 42);

    for (const auto& path : {path_a, path_b}) {
        Packager reader;
        REQUIRE(reader.Open(path.string()) == PackResult::Ok);
        std::vector<uint8_t> out;
        REQUIRE(reader.Extract("data.bin", out) == PackResult::Ok);
        REQUIRE(out == expected_data);
    }

    CleanupTempDir(tmp);
}

// ═══════════════════════════════════════════════════════════════════════════
//  Section 15: Double-close / lifecycle edge cases
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Packager: default constructed is not open", "[horopak][packager][lifecycle]") {
    Packager packer;
    REQUIRE_FALSE(packer.IsOpen());
    REQUIRE(packer.AssetCount() == 0);
}

TEST_CASE("Packager: IsOpen returns false after failed Open", "[horopak][packager][lifecycle]") {
    Packager packer;
    packer.Open("/tmp/nonexistent.horo");
    REQUIRE_FALSE(packer.IsOpen());
}

TEST_CASE("Packager: ExtractAll on valid archive", "[horopak][packager][extract_all]") {
    const auto tmp = MakeTempDir("extract_all");
    const auto archive_path = tmp / "bundle.horo";
    const auto out_dir = tmp / "out";

    InMemoryAssetProvider provider;
    provider.Add("a.txt", {'A'});
    provider.Add("sub/b.txt", {'B'});
    provider.Add("sub/deep/c.txt", {'C'});

    {
        Packager p;
        p.AddAsset("a.txt");
        p.AddAsset("sub/b.txt");
        p.AddAsset("sub/deep/c.txt");
        REQUIRE(p.Write(archive_path.string(), provider) == PackResult::Ok);
    }

    Packager reader;
    REQUIRE(reader.Open(archive_path.string()) == PackResult::Ok);
    REQUIRE(reader.ExtractAll(out_dir.string()) == PackResult::Ok);

    // Verify all files were extracted with correct paths
    REQUIRE(fs::exists(out_dir / "a.txt"));
    REQUIRE(fs::exists(out_dir / "sub" / "b.txt"));
    REQUIRE(fs::exists(out_dir / "sub" / "deep" / "c.txt"));

    CleanupTempDir(tmp);
}

// ═══════════════════════════════════════════════════════════════════════════
//  Section 16: ArchiveFlags round-trip through header
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("HoroFormat: HoroArchiveFlags CompressedLZ4 round-trips through header", "[horopak][format][flags]") {
    HoroHeader header{};
    std::memcpy(header.magic, kHoroMagic.data(), kHoroMagic.size());
    header.version = kHoroVersion;
    header.flags = static_cast<uint32_t>(HoroArchiveFlags::CompressedLZ4);
    header.toc_count = 1;
    header.toc_offset = 128;
    header.data_offset = 32;

    REQUIRE(ValidateHeader(header));

    std::stringstream ss(std::ios::binary | std::ios::in | std::ios::out);
    REQUIRE(WriteHeader(ss, header));

    HoroHeader restored{};
    ss.seekg(0);
    REQUIRE(ReadHeader(ss, restored));

    REQUIRE(HasFlag(static_cast<HoroArchiveFlags>(restored.flags),
                    HoroArchiveFlags::CompressedLZ4));
}

TEST_CASE("HoroFormat: HoroArchiveFlags EncryptedAES256CTR round-trips through header", "[horopak][format][flags]") {
    HoroHeader header{};
    std::memcpy(header.magic, kHoroMagic.data(), kHoroMagic.size());
    header.version = kHoroVersion;
    header.flags = static_cast<uint32_t>(HoroArchiveFlags::EncryptedAES256CTR);
    header.toc_count = 1;
    header.toc_offset = 128;
    header.data_offset = 32;

    REQUIRE(ValidateHeader(header));

    std::stringstream ss(std::ios::binary | std::ios::in | std::ios::out);
    REQUIRE(WriteHeader(ss, header));

    HoroHeader restored{};
    ss.seekg(0);
    REQUIRE(ReadHeader(ss, restored));

    REQUIRE(HasFlag(static_cast<HoroArchiveFlags>(restored.flags),
                    HoroArchiveFlags::EncryptedAES256CTR));
}

TEST_CASE("HoroFormat: compound flags (Compressed + Encrypted) round-trip", "[horopak][format][flags]") {
    HoroHeader header{};
    std::memcpy(header.magic, kHoroMagic.data(), kHoroMagic.size());
    header.version = kHoroVersion;
    const auto compound = HoroArchiveFlags::CompressedLZ4 |
                          HoroArchiveFlags::EncryptedAES256CTR;
    header.flags = static_cast<uint32_t>(compound);
    header.toc_count = 1;
    header.toc_offset = 128;
    header.data_offset = 32;

    REQUIRE(ValidateHeader(header));

    std::stringstream ss(std::ios::binary | std::ios::in | std::ios::out);
    REQUIRE(WriteHeader(ss, header));

    HoroHeader restored{};
    ss.seekg(0);
    REQUIRE(ReadHeader(ss, restored));

    const auto restored_flags = static_cast<HoroArchiveFlags>(restored.flags);
    REQUIRE(HasFlag(restored_flags, HoroArchiveFlags::CompressedLZ4));
    REQUIRE(HasFlag(restored_flags, HoroArchiveFlags::EncryptedAES256CTR));
}

// ═══════════════════════════════════════════════════════════════════════════
//  Section 17: TOCEntryFlags round-trip
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("HoroFormat: TOCEntryFlags Compressed round-trips", "[horopak][format][toc_flags]") {
    std::vector<TOCEntry> entries(1);
    entries[0].hash = 0xAAAA;
    entries[0].offset = 100;
    entries[0].compressed_size = 50;
    entries[0].uncompressed_size = 200;
    entries[0].flags = static_cast<uint32_t>(TOCEntryFlags::Compressed);
    entries[0].crc32 = 0;

    std::stringstream ss(std::ios::binary | std::ios::in | std::ios::out);
    REQUIRE(WriteTOC(ss, entries));

    ss.seekg(0);
    std::vector<TOCEntry> restored;
    REQUIRE(ReadTOC(ss, 1, restored));

    const auto flags = static_cast<TOCEntryFlags>(restored[0].flags);
    REQUIRE((flags & TOCEntryFlags::Compressed) == TOCEntryFlags::Compressed);
}

TEST_CASE("HoroFormat: TOCEntryFlags Encrypted round-trips", "[horopak][format][toc_flags]") {
    std::vector<TOCEntry> entries(1);
    entries[0].hash = 0xBBBB;
    entries[0].offset = 200;
    entries[0].compressed_size = 100;
    entries[0].uncompressed_size = 100;
    entries[0].flags = static_cast<uint32_t>(TOCEntryFlags::Encrypted);
    entries[0].crc32 = 0;

    std::stringstream ss(std::ios::binary | std::ios::in | std::ios::out);
    REQUIRE(WriteTOC(ss, entries));

    ss.seekg(0);
    std::vector<TOCEntry> restored;
    REQUIRE(ReadTOC(ss, 1, restored));

    const auto flags = static_cast<TOCEntryFlags>(restored[0].flags);
    REQUIRE((flags & TOCEntryFlags::Encrypted) == TOCEntryFlags::Encrypted);
}

// ═══════════════════════════════════════════════════════════════════════════
//  Section 18: Corrupted archive — byte-level tampering
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Packager::Open detects corrupted archive (header bytes flipped)", "[horopak][packager][corrupt]") {
    const auto tmp = MakeTempDir("corrupt_header");
    const auto archive_path = tmp / "bundle.horo";

    // Create valid archive first
    InMemoryAssetProvider provider;
    provider.Add("data.bin", {1, 2, 3, 4, 5});

    {
        Packager p;
        p.AddAsset("data.bin");
        REQUIRE(p.Write(archive_path.string(), provider) == PackResult::Ok);
    }

    // Tamper with header: flip magic byte
    {
        std::fstream file(archive_path, std::ios::binary | std::ios::in | std::ios::out);
        file.seekp(0);
        file.put('X');  // Corrupt magic: 'HORO' → 'XORO'
    }

    Packager reader;
    REQUIRE(reader.Open(archive_path.string()) == PackResult::InvalidMagic);

    CleanupTempDir(tmp);
}

TEST_CASE("Packager::Open detects truncated TOC (file truncated before TOC)", "[horopak][packager][corrupt]") {
    const auto tmp = MakeTempDir("corrupt_toc_trunc");
    const auto archive_path = tmp / "bundle.horo";

    InMemoryAssetProvider provider;
    provider.Add("data.bin", MakeRandomData(512, 1));

    {
        Packager p;
        p.AddAsset("data.bin");
        REQUIRE(p.Write(archive_path.string(), provider) == PackResult::Ok);
    }

    // Truncate file to header size (32 bytes) — TOC is gone
    fs::resize_file(archive_path, 32);

    Packager reader;
    // Should fail because TOC can't be read
    REQUIRE(reader.Open(archive_path.string()) != PackResult::Ok);

    CleanupTempDir(tmp);
}

TEST_CASE("Packager::Open detects truncated TOC (partial TOC)", "[horopak][packager][corrupt]") {
    const auto tmp = MakeTempDir("corrupt_toc_partial");
    const auto archive_path = tmp / "bundle.horo";

    InMemoryAssetProvider provider;
    provider.Add("data.bin", MakeRandomData(256, 2));

    {
        Packager p;
        p.AddAsset("data.bin");
        REQUIRE(p.Write(archive_path.string(), provider) == PackResult::Ok);
    }

    const auto full_size = fs::file_size(archive_path);
    // Truncate by 16 bytes (partial TOC)
    fs::resize_file(archive_path, full_size - 16);

    Packager reader;
    REQUIRE(reader.Open(archive_path.string()) != PackResult::Ok);

    CleanupTempDir(tmp);
}

// ═══════════════════════════════════════════════════════════════════════════
//  Section 19: Compression level boundary tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Packager::SetCompressionLevel clamps negative to zero", "[horopak][packager][compression]") {
    Packager packer;
    packer.SetCompressionLevel(-5);

    InMemoryAssetProvider provider;
    provider.Add("x.txt", {'x'});
    packer.AddAsset("x.txt");

    const auto tmp = MakeTempDir("comp_neg");
    const auto archive_path = tmp / "bundle.horo";
    // Should not crash or throw
    REQUIRE(packer.Write(archive_path.string(), provider) == PackResult::Ok);

    CleanupTempDir(tmp);
}

TEST_CASE("Packager::SetEncryptionEnabled toggles without crash", "[horopak][packager][encryption]") {
    Packager packer;
    packer.SetEncryptionEnabled(true);
    packer.SetEncryptionEnabled(false);

    InMemoryAssetProvider provider;
    provider.Add("x.txt", {'x'});
    packer.AddAsset("x.txt");

    const auto tmp = MakeTempDir("enc_toggle");
    const auto archive_path = tmp / "bundle.horo";
    REQUIRE(packer.Write(archive_path.string(), provider) == PackResult::Ok);

    CleanupTempDir(tmp);
}

TEST_CASE("Packager::Open rejects invalid encrypted string-table count",
          "[horopak][packager][encryption][corrupt]") {
    const auto tmp = MakeTempDir("encrypted_string_table_count");
    const auto archive_path = tmp / "bundle.horo";
    const std::array<uint8_t, 32> key = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
        0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
        0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
    };

    InMemoryAssetProvider provider;
    provider.Add("secret.txt", {'s', 'e', 'c', 'r', 'e', 't'});

    Packager writer;
    writer.SetEncryptionEnabled(true);
    writer.SetEncryptionKey(key);
    REQUIRE(writer.AddAsset("secret.txt") == PackResult::Ok);
    REQUIRE(writer.Write(archive_path.string(), provider) == PackResult::Ok);

    HoroHeader header{};
    std::vector<TOCEntry> toc;
    {
        std::ifstream archive(archive_path, std::ios::binary);
        REQUIRE(ReadHeader(archive, header));
        archive.seekg(static_cast<std::streamoff>(header.toc_offset));
        REQUIRE(ReadTOC(archive, header.toc_count, toc));
    }

    const auto string_table = std::find_if(
        toc.begin(), toc.end(),
        [](const TOCEntry& entry) { return entry.hash == 0; });
    REQUIRE(string_table != toc.end());

    // CTR encryption is malleable. Change the plaintext count from 1 to
    // UINT32_MAX without knowing or changing the key.
    constexpr std::array<uint8_t, 4> kCountMask = {
        0xfe, 0xff, 0xff, 0xff,
    };
    std::fstream archive(archive_path,
                         std::ios::binary | std::ios::in | std::ios::out);
    REQUIRE(archive.is_open());
    archive.seekg(static_cast<std::streamoff>(
        string_table->offset + kAesBlockSize));
    std::array<uint8_t, 4> encrypted_count{};
    archive.read(reinterpret_cast<char*>(encrypted_count.data()),
                 static_cast<std::streamsize>(encrypted_count.size()));
    REQUIRE(archive.good());
    for (size_t i = 0; i < encrypted_count.size(); ++i)
        encrypted_count[i] ^= kCountMask[i];
    archive.seekp(static_cast<std::streamoff>(
        string_table->offset + kAesBlockSize));
    archive.write(reinterpret_cast<const char*>(encrypted_count.data()),
                  static_cast<std::streamsize>(encrypted_count.size()));
    REQUIRE(archive.good());
    archive.close();

    Packager reader;
    reader.SetEncryptionKey(key);
    REQUIRE(reader.Open(archive_path.string()) == PackResult::EncryptionFailed);

    CleanupTempDir(tmp);
}

// ═══════════════════════════════════════════════════════════════════════════
//  Section 20: Hash verification — CRC32 and SHA-256
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("HashVerifier: CRC32 of known test vector", "[horopak][hash][crc32]") {
    // "123456789" → 0xCBF43926 (standard Check value)
    const uint8_t data[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    const uint32_t crc = ComputeCRC32(data, sizeof(data));
    REQUIRE(crc == 0xCBF43926u);
}

TEST_CASE("HashVerifier: CRC32 of empty data is zero", "[horopak][hash][crc32]") {
    const uint32_t crc = ComputeCRC32(nullptr, 0);
    REQUIRE(crc == 0x00000000u);
}

TEST_CASE("HashVerifier: CRC32 of single byte matches known value", "[horopak][hash][crc32]") {
    const uint8_t data[] = {0x00};
    const uint32_t crc = ComputeCRC32(data, 1);
    // CRC32(0x00) with init=0xFFFFFFFF, final XOR → known value
    REQUIRE(crc == 0xD202EF8Du);
}

TEST_CASE("HashVerifier: SHA-256 of empty string", "[horopak][hash][sha256]") {
    // SHA-256("") = e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855
    uint8_t digest[kSha256Size];
    ComputeSHA256(nullptr, 0, digest);

    const uint8_t expected[kSha256Size] = {
        0xE3, 0xB0, 0xC4, 0x42, 0x98, 0xFC, 0x1C, 0x14,
        0x9A, 0xFB, 0xF4, 0xC8, 0x99, 0x6F, 0xB9, 0x24,
        0x27, 0xAE, 0x41, 0xE4, 0x64, 0x9B, 0x93, 0x4C,
        0xA4, 0x95, 0x99, 0x1B, 0x78, 0x52, 0xB8, 0x55
    };
    REQUIRE(std::memcmp(digest, expected, kSha256Size) == 0);
}

TEST_CASE("HashVerifier: SHA-256 of 'abc' matches FIPS 180-4 test vector", "[horopak][hash][sha256]") {
    // SHA-256("abc") = ba7816bf...
    const uint8_t data[] = {'a', 'b', 'c'};
    uint8_t digest[kSha256Size];
    ComputeSHA256(data, sizeof(data), digest);

    const uint8_t expected[kSha256Size] = {
        0xBA, 0x78, 0x16, 0xBF, 0x8F, 0x01, 0xCF, 0xEA,
        0x41, 0x41, 0x40, 0xDE, 0x5D, 0xAE, 0x22, 0x23,
        0xB0, 0x03, 0x61, 0xA3, 0x96, 0x17, 0x7A, 0x9C,
        0xB4, 0x10, 0xFF, 0x61, 0xF2, 0x00, 0x15, 0xAD
    };
    REQUIRE(std::memcmp(digest, expected, kSha256Size) == 0);
}

TEST_CASE("HashVerifier: SHA-256 streaming matches one-shot", "[horopak][hash][sha256]") {
    const std::vector<uint8_t> data = MakeRandomData(1024, 99);

    uint8_t one_shot[kSha256Size];
    ComputeSHA256(data.data(), data.size(), one_shot);

    // Streaming: feed in 3 chunks
    Sha256Hasher hasher;
    hasher.Update(data.data(), 256);
    hasher.Update(data.data() + 256, 512);
    hasher.Update(data.data() + 768, 256);
    uint8_t streaming[kSha256Size];
    hasher.Finish(streaming);

    REQUIRE(std::memcmp(one_shot, streaming, kSha256Size) == 0);
}

TEST_CASE("HashVerifier: Sha256Hasher::Reset produces fresh stream", "[horopak][hash][sha256]") {
    Sha256Hasher hasher;

    const uint8_t a[] = {'A'};
    hasher.Update(a, 1);
    uint8_t digest_a[kSha256Size];
    hasher.Finish(digest_a);

    // Reset and hash a different byte
    hasher.Reset();
    const uint8_t b[] = {'B'};
    hasher.Update(b, 1);
    uint8_t digest_b[kSha256Size];
    hasher.Finish(digest_b);

    // Should differ
    REQUIRE(std::memcmp(digest_a, digest_b, kSha256Size) != 0);
}

TEST_CASE("Packager: round-trip verifies CRC32 on extraction", "[horopak][hash][integration]") {
    const auto tmp = MakeTempDir("hash_crc32");
    const auto archive_path = tmp / "bundle.horo";

    InMemoryAssetProvider provider;
    const std::vector<uint8_t> asset_data = MakeRandomData(4096, 42);
    provider.Add("data.bin", asset_data);

    Packager packer;
    packer.SetCompressionLevel(0);
    REQUIRE(packer.AddAsset("data.bin") == PackResult::Ok);
    REQUIRE(packer.Write(archive_path.string(), provider) == PackResult::Ok);

    // Open and extract — CRC32 is verified internally.
    Packager reader;
    REQUIRE(reader.Open(archive_path.string()) == PackResult::Ok);

    std::vector<uint8_t> extracted;
    REQUIRE(reader.Extract("data.bin", extracted) == PackResult::Ok);
    REQUIRE(extracted == asset_data);

    CleanupTempDir(tmp);
}

TEST_CASE("Packager: CRC32 mismatch is detected on corrupt archive", "[horopak][hash][corrupt]") {
    const auto tmp = MakeTempDir("hash_crc32_corrupt");
    const auto archive_path = tmp / "bundle.horo";

    InMemoryAssetProvider provider;
    const std::vector<uint8_t> asset_data = MakeRandomData(2048, 77);
    provider.Add("corrupt.bin", asset_data);

    Packager packer;
    packer.SetCompressionLevel(0);
    REQUIRE(packer.AddAsset("corrupt.bin") == PackResult::Ok);
    REQUIRE(packer.Write(archive_path.string(), provider) == PackResult::Ok);

    // Manually flip a byte in the data section (after the 32-byte header).
    {
        std::fstream file(archive_path, std::ios::binary | std::ios::in |
                                          std::ios::out);
        file.seekp(40, std::ios::beg); // Skip header + a few bytes into data
        char corrupt_byte;
        file.read(&corrupt_byte, 1);
        file.seekp(40, std::ios::beg);
        corrupt_byte ^= 0xFF; // Flip all bits
        file.write(&corrupt_byte, 1);
    }

    Packager reader;
    REQUIRE(reader.Open(archive_path.string()) == PackResult::Ok);

    std::vector<uint8_t> extracted;
    // CRC32 should now mismatch.
    REQUIRE(reader.Extract("corrupt.bin", extracted) == PackResult::HashMismatch);

    CleanupTempDir(tmp);
}

TEST_CASE("Packager: round-trip with SHA-256 enabled", "[horopak][hash][sha256][integration]") {
    const auto tmp = MakeTempDir("hash_sha256");
    const auto archive_path = tmp / "bundle.horo";

    InMemoryAssetProvider provider;
    const std::vector<uint8_t> asset_data = MakeRandomData(8192, 123);
    provider.Add("large.bin", asset_data);

    Packager packer;
    packer.SetCompressionLevel(0);
    packer.SetSHA256Enabled(true);
    REQUIRE(packer.AddAsset("large.bin") == PackResult::Ok);
    REQUIRE(packer.Write(archive_path.string(), provider) == PackResult::Ok);

    Packager reader;
    REQUIRE(reader.Open(archive_path.string()) == PackResult::Ok);

    std::vector<uint8_t> extracted;
    REQUIRE(reader.Extract("large.bin", extracted) == PackResult::Ok);
    REQUIRE(extracted == asset_data);

    CleanupTempDir(tmp);
}

TEST_CASE("Packager: SHA-256 mismatch is detected on corrupt archive", "[horopak][hash][sha256][corrupt]") {
    const auto tmp = MakeTempDir("hash_sha256_corrupt");
    const auto archive_path = tmp / "bundle.horo";

    InMemoryAssetProvider provider;
    const std::vector<uint8_t> asset_data = MakeRandomData(2048, 55);
    provider.Add("corrupt.bin", asset_data);

    Packager packer;
    packer.SetCompressionLevel(0);
    packer.SetSHA256Enabled(true);
    REQUIRE(packer.AddAsset("corrupt.bin") == PackResult::Ok);
    REQUIRE(packer.Write(archive_path.string(), provider) == PackResult::Ok);

    // Flip a byte in the compressed data section.
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

    Packager reader;
    REQUIRE(reader.Open(archive_path.string()) == PackResult::Ok);

    std::vector<uint8_t> extracted;
    // Both CRC32 and SHA-256 should fail (CRC32 checked first).
    const PackResult result = reader.Extract("corrupt.bin", extracted);
    REQUIRE(result == PackResult::HashMismatch);

    CleanupTempDir(tmp);
}

TEST_CASE("Packager: archive with CRC32 but without SHA-256 is backward-compatible", "[horopak][hash][compat]") {
    const auto tmp = MakeTempDir("hash_compat");
    const auto archive_path = tmp / "bundle.horo";

    InMemoryAssetProvider provider;
    provider.Add("asset.bin", MakeRandomData(512, 1));

    // Write without SHA-256 (default).
    Packager packer;
    packer.SetCompressionLevel(0);
    REQUIRE(packer.AddAsset("asset.bin") == PackResult::Ok);
    REQUIRE(packer.Write(archive_path.string(), provider) == PackResult::Ok);

    // Reader should handle archive without SHA-256 block gracefully.
    Packager reader;
    REQUIRE(reader.Open(archive_path.string()) == PackResult::Ok);
    // m_sha256_digests should be empty.
    REQUIRE(reader.IsOpen());

    std::vector<uint8_t> extracted;
    REQUIRE(reader.Extract("asset.bin", extracted) == PackResult::Ok);

    CleanupTempDir(tmp);
}

TEST_CASE("Packager: multiple assets with SHA-256 all verify", "[horopak][hash][sha256][stress]") {
    const auto tmp = MakeTempDir("hash_sha256_multi");
    const auto archive_path = tmp / "bundle.horo";

    constexpr int kCount = 20;
    InMemoryAssetProvider provider;
    std::unordered_map<std::string, std::vector<uint8_t>> originals;

    Packager packer;
    packer.SetCompressionLevel(1);
    packer.SetSHA256Enabled(true);

    for (int i = 0; i < kCount; ++i) {
        const std::string path = "asset_" + std::to_string(i) + ".dat";
        auto data = MakeRandomData(static_cast<size_t>(256 + i * 64),
                                   static_cast<uint64_t>(i));
        provider.Add(path, data);
        originals[path] = data;
        REQUIRE(packer.AddAsset(path) == PackResult::Ok);
    }

    REQUIRE(packer.Write(archive_path.string(), provider) == PackResult::Ok);

    Packager reader;
    REQUIRE(reader.Open(archive_path.string()) == PackResult::Ok);

    for (const auto& [path, expected] : originals) {
        std::vector<uint8_t> out;
        REQUIRE(reader.Extract(path, out) == PackResult::Ok);
        REQUIRE(out == expected);
    }

    CleanupTempDir(tmp);
}

// ═══════════════════════════════════════════════════════════════════════════
//  Section 21: Extended Concurrent Reads
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Packager: concurrent ExtractAll from same archive (separate instances)",
          "[horopak][packager][concurrency][extract_all]") {
    const auto tmp = MakeTempDir("concurrent_extractall");
    const auto archive_path = tmp / "bundle.horo";

    // Multi-asset archive with nested paths
    InMemoryAssetProvider provider;
    constexpr int kAssetCount = 15;
    for (int i = 0; i < kAssetCount; ++i) {
        const std::string path = "group_" + std::to_string(i % 3) +
                                 "/asset_" + std::to_string(i) + ".bin";
        provider.Add(path, MakeRandomData(512, static_cast<uint64_t>(i)));
    }

    {
        Packager p;
        for (int i = 0; i < kAssetCount; ++i) {
            const std::string path = "group_" + std::to_string(i % 3) +
                                     "/asset_" + std::to_string(i) + ".bin";
            REQUIRE(p.AddAsset(path) == PackResult::Ok);
        }
        REQUIRE(p.Write(archive_path.string(), provider) == PackResult::Ok);
    }

    // 4 threads, each doing ExtractAll to a different output dir
    constexpr int kThreadCount = 4;
    std::vector<std::thread> threads;
    std::atomic<int> errors{0};

    for (int t = 0; t < kThreadCount; ++t) {
        threads.emplace_back([&, t]() {
            Packager reader;
            if (reader.Open(archive_path.string()) != PackResult::Ok) {
                ++errors;
                return;
            }

            const auto out_dir = tmp / ("extracted_" + std::to_string(t));
            if (reader.ExtractAll(out_dir.string()) != PackResult::Ok) {
                ++errors;
                return;
            }

            // Spot-check a few files exist
            if (!fs::exists(out_dir / "group_0" / "asset_0.bin")) ++errors;
            if (!fs::exists(out_dir / "group_1" / "asset_4.bin")) ++errors;
            if (!fs::exists(out_dir / "group_2" / "asset_2.bin")) ++errors;
        });
    }

    for (auto& th : threads) th.join();
    REQUIRE(errors == 0);

    CleanupTempDir(tmp);
}

TEST_CASE("Packager: concurrent mixed operations — list + extract from same archive",
          "[horopak][packager][concurrency][mixed]") {
    const auto tmp = MakeTempDir("concurrent_mixed");
    const auto archive_path = tmp / "bundle.horo";

    InMemoryAssetProvider provider;
    constexpr int kAssetCount = 30;
    for (int i = 0; i < kAssetCount; ++i) {
        const std::string path = "data_" + std::to_string(i) + ".bin";
        provider.Add(path, MakeRandomData(256, static_cast<uint64_t>(i)));
    }

    {
        Packager p;
        for (int i = 0; i < kAssetCount; ++i) {
            REQUIRE(p.AddAsset("data_" + std::to_string(i) + ".bin") == PackResult::Ok);
        }
        REQUIRE(p.Write(archive_path.string(), provider) == PackResult::Ok);
    }

    // Thread A: lists assets repeatedly
    // Thread B: extracts odd-indexed assets
    // Thread C: extracts even-indexed assets
    std::atomic<int> errors{0};

    std::thread lister([&]() {
        for (int pass = 0; pass < 10; ++pass) {
            Packager reader;
            if (reader.Open(archive_path.string()) != PackResult::Ok) {
                ++errors;
                return;
            }
            std::vector<std::string> paths;
            if (reader.ListAssets(paths) != PackResult::Ok) {
                ++errors;
                return;
            }
            if (paths.size() != static_cast<size_t>(kAssetCount)) {
                ++errors;
                return;
            }
        }
    });

    std::thread extractor_odd([&]() {
        for (int pass = 0; pass < 5; ++pass) {
            Packager reader;
            if (reader.Open(archive_path.string()) != PackResult::Ok) {
                ++errors;
                return;
            }
            for (int i = 1; i < kAssetCount; i += 2) {
                std::vector<uint8_t> data;
                if (reader.Extract("data_" + std::to_string(i) + ".bin", data) != PackResult::Ok) {
                    ++errors;
                }
                if (data.empty()) ++errors;
            }
        }
    });

    std::thread extractor_even([&]() {
        for (int pass = 0; pass < 5; ++pass) {
            Packager reader;
            if (reader.Open(archive_path.string()) != PackResult::Ok) {
                ++errors;
                return;
            }
            for (int i = 0; i < kAssetCount; i += 2) {
                std::vector<uint8_t> data;
                if (reader.Extract("data_" + std::to_string(i) + ".bin", data) != PackResult::Ok) {
                    ++errors;
                }
                if (data.empty()) ++errors;
            }
        }
    });

    lister.join();
    extractor_odd.join();
    extractor_even.join();
    REQUIRE(errors == 0);

    CleanupTempDir(tmp);
}

TEST_CASE("Packager: high-thread-count concurrent open + extract stress",
          "[horopak][packager][concurrency][stress]") {
    const auto tmp = MakeTempDir("concurrent_stress");
    const auto archive_path = tmp / "bundle.horo";

    InMemoryAssetProvider provider;
    const std::vector<uint8_t> asset_data = MakeRandomData(4096, 123);
    provider.Add("stress.bin", asset_data);

    {
        Packager p;
        p.AddAsset("stress.bin");
        REQUIRE(p.Write(archive_path.string(), provider) == PackResult::Ok);
    }

    constexpr int kThreadCount = 16;
    std::vector<std::thread> threads;
    std::atomic<int> errors{0};

    for (int t = 0; t < kThreadCount; ++t) {
        threads.emplace_back([&]() {
            Packager reader;
            if (reader.Open(archive_path.string()) != PackResult::Ok) {
                ++errors;
                return;
            }
            std::vector<uint8_t> data;
            if (reader.Extract("stress.bin", data) != PackResult::Ok) {
                ++errors;
                return;
            }
            if (data != asset_data) {
                ++errors;
            }
        });
    }

    for (auto& th : threads) th.join();
    REQUIRE(errors == 0);

    CleanupTempDir(tmp);
}
