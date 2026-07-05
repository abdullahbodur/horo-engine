/** @file test_benchmark_pipeline.cpp
 *  @brief Performance benchmarks for the Horo Engine artifact pipeline:
 *         pack throughput, unpack throughput, and checksum throughput.
 *
 *  Benchmarks are kept separate from pass/fail functional tests.
 *  Catch2 BENCHMARK macros are used for micro-benchmarks (checksum,
 *  per-asset packing); manual wall-clock timing is used for
 *  macro-benchmarks (full-archive pack/unpack end-to-end).
 *
 *  No hard thresholds are enforced — the benchmarks report MB/s and
 *  elapsed time for informational / trend-monitoring purposes.
 *
 *  Typical invocation:
 *  @code
 *  ./test_benchmark_pipeline "[benchmark]"
 *  @endcode
 *
 *  Run with --benchmark-no-analysis to skip statistical warm-up:
 *  @code
 *  ./test_benchmark_pipeline "[benchmark]" --benchmark-no-analysis
 *  @endcode
 */

#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_test_macros.hpp>

#include "core/archive/HashVerifier.h"
#include "core/archive/HoroFormat.h"
#include "core/archive/Packager.h"

#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

using namespace Horo::Archive;

namespace fs = std::filesystem;

// ==========================================================================
//  Test helpers
// ==========================================================================

namespace {

/** @brief Return a unique temporary directory path for a test case.
 *  Identical to the helper in test_horopak.cpp / test_release_pipeline.cpp. */
fs::path MakeTempDir(const std::string& test_name) {
    const auto base = fs::temp_directory_path() / "horo_bench";
    const auto dir = base / test_name;
    std::error_code ec;
    fs::create_directories(dir, ec);
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    return dir;
}

/** @brief Remove a temporary directory and all its contents. */
void CleanupTempDir(const fs::path& dir) {
    std::error_code ec;
    fs::remove_all(dir, ec);
}

/** @brief Generate a deterministic buffer of `size` bytes using a seeded
 *         PRNG.  Determinism ensures benchmark comparisons across runs. */
std::vector<uint8_t> MakeBenchData(size_t size, uint64_t seed = 0xDEADBEEF) {
    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<unsigned int> dist(0, 255);
    std::vector<uint8_t> data(size);
    for (auto& byte : data)
        byte = static_cast<uint8_t>(dist(rng));
    return data;
}

/** @brief In-memory asset provider (identical to
 *         test_horopak.cpp / test_release_pipeline.cpp). */
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

/** @brief Convert nanoseconds to a human-readable wall-time string. */
std::string FormatWallTime(std::chrono::nanoseconds ns) {
    using namespace std::chrono;
    auto count = ns.count();

    if (count < 1'000LL)
        return std::to_string(count) + " ns";
    if (count < 1'000'000LL)
        return std::to_string(count / 1'000.0) + " us";
    if (count < 1'000'000'000LL)
        return std::to_string(count / 1'000'000.0) + " ms";
    return std::to_string(count / 1'000'000'000.0) + " s";
}

} // anonymous namespace

// ==========================================================================
//  Section 1: Checksum throughput — CRC32
// ==========================================================================

TEST_CASE("Bench: CRC32 throughput", "[benchmark][checksum][crc32]") {
    for (const auto size : {size_t(1024), size_t(64 * 1024),
                             size_t(1024 * 1024), size_t(16 * 1024 * 1024),
                             size_t(64 * 1024 * 1024)}) {
        const auto data = MakeBenchData(size);
        std::string label = "CRC32-" + std::to_string(size / 1024) + "KB";

        BENCHMARK(std::move(label)) {
            volatile auto crc = ComputeCRC32(data.data(), data.size());
            (void)crc;
            return crc;
        };
    }
}

// ==========================================================================
//  Section 2: Checksum throughput — SHA-256
// ==========================================================================

TEST_CASE("Bench: SHA-256 throughput", "[benchmark][checksum][sha256]") {
    for (const auto size : {size_t(1024), size_t(64 * 1024),
                             size_t(1024 * 1024), size_t(16 * 1024 * 1024),
                             size_t(64 * 1024 * 1024)}) {
        const auto data = MakeBenchData(size);
        std::string label = "SHA256-" + std::to_string(size / 1024) + "KB";

        BENCHMARK(std::move(label)) {
            uint8_t digest[kSha256Size];
            ComputeSHA256(data.data(), data.size(), digest);
            return digest[0];
        };
    }
}

// ==========================================================================
//  Section 3: Checksum throughput — CRC32 + SHA-256 combined
// ==========================================================================

TEST_CASE("Bench: CRC32 + SHA-256 combined throughput",
          "[benchmark][checksum][combined]") {
    for (const auto size : {size_t(1024), size_t(64 * 1024),
                             size_t(1024 * 1024), size_t(16 * 1024 * 1024),
                             size_t(64 * 1024 * 1024)}) {
        const auto data = MakeBenchData(size);
        std::string label = "CRC32+SHA256-" +
                                  std::to_string(size / 1024) + "KB";

        BENCHMARK(std::move(label)) {
            volatile auto crc = ComputeCRC32(data.data(), data.size());
            uint8_t digest[kSha256Size];
            ComputeSHA256(data.data(), data.size(), digest);
            return crc ^ digest[0];
        };
    }
}

// ==========================================================================
//  Section 4: Pack throughput — single large asset
// ==========================================================================

TEST_CASE("Bench: pack throughput — single asset, varying size",
          "[benchmark][pack][throughput]") {
    // Sizes: 64KB, 1MB, 16MB, 64MB
    for (const auto size : {size_t(64 * 1024), size_t(1024 * 1024),
                             size_t(16 * 1024 * 1024),
                             size_t(64 * 1024 * 1024)}) {
        const auto tmp = MakeTempDir("bench_pack_single");
        const auto data = MakeBenchData(size, 0xCAFE0000 + size);
        const auto archive_path = tmp / "single.horo";

        InMemoryAssetProvider provider;
        provider.Add("large_asset.bin", data);

        INFO("Pack: " << size / 1024 << " KB single asset");

        BENCHMARK("Pack-" + std::to_string(size / 1024) + "KB") {
            Packager packer;
            packer.SetCompressionLevel(0); // No compression — measure raw I/O
            REQUIRE(packer.AddAsset("large_asset.bin") == PackResult::Ok);
            REQUIRE(packer.Write(archive_path.string(), provider) ==
                     PackResult::Ok);
            return fs::file_size(archive_path);
        };

        CleanupTempDir(tmp);
    }
}

// ==========================================================================
//  Section 5: Pack throughput — many small assets
// ==========================================================================

TEST_CASE("Bench: pack throughput — many small assets",
          "[benchmark][pack][multi_asset]") {
    constexpr size_t kAssetCount = 200;
    constexpr size_t kPerAssetSize = 8192;  // 8 KB each → 1.6 MB total
    const size_t total_size = kAssetCount * kPerAssetSize;

    const auto tmp = MakeTempDir("bench_pack_multi");
    const auto archive_path = tmp / "multi.horo";

    InMemoryAssetProvider provider;
    for (size_t i = 0; i < kAssetCount; ++i) {
        const auto path =
            "assets/file_" + std::to_string(i) + ".bin";
        provider.Add(path, MakeBenchData(kPerAssetSize,
                                        0xBEEF0000 + static_cast<uint64_t>(i)));
    }

    INFO("Pack: " << kAssetCount << " assets × " << kPerAssetSize
         << " bytes = " << total_size / 1024 << " KB total");

    BENCHMARK("Pack-" + std::to_string(kAssetCount) + "x" +
              std::to_string(kPerAssetSize / 1024) + "KB") {
        Packager packer;
        packer.SetCompressionLevel(0);
        for (size_t i = 0; i < kAssetCount; ++i) {
            const auto path =
                "assets/file_" + std::to_string(i) + ".bin";
            REQUIRE(packer.AddAsset(path) == PackResult::Ok);
        }
        REQUIRE(packer.Write(archive_path.string(), provider) ==
                 PackResult::Ok);
        return fs::file_size(archive_path);
    };

    CleanupTempDir(tmp);
}

// ==========================================================================
//  Section 6: Pack throughput — with SHA-256 enabled
// ==========================================================================

TEST_CASE("Bench: pack throughput — SHA-256 enabled vs disabled",
          "[benchmark][pack][sha256]") {
    const auto tmp = MakeTempDir("bench_pack_sha");
    const auto archive_plain = tmp / "plain.horo";
    const auto archive_sha = tmp / "sha.horo";

    constexpr size_t kSize = 64 * 1024 * 1024;  // 64 MB
    const auto data = MakeBenchData(kSize, 0xFEEDF00D);

    InMemoryAssetProvider provider;
    provider.Add("payload.bin", data);

    BENCHMARK("Pack-64MB-noSHA256") {
        Packager packer;
        packer.SetCompressionLevel(0);
        packer.SetSHA256Enabled(false);
        packer.AddAsset("payload.bin");
        packer.Write(archive_plain.string(), provider);
        return fs::file_size(archive_plain);
    };

    BENCHMARK("Pack-64MB-SHA256") {
        Packager packer;
        packer.SetCompressionLevel(0);
        packer.SetSHA256Enabled(true);
        packer.AddAsset("payload.bin");
        packer.Write(archive_sha.string(), provider);
        return fs::file_size(archive_sha);
    };

    CleanupTempDir(tmp);
}

// ==========================================================================
//  Section 7: Unpack throughput — single large asset
// ==========================================================================

TEST_CASE("Bench: unpack throughput — single asset, varying size",
          "[benchmark][unpack][throughput]") {
    for (const auto size : {size_t(64 * 1024), size_t(1024 * 1024),
                             size_t(16 * 1024 * 1024),
                             size_t(64 * 1024 * 1024)}) {
        const auto tmp = MakeTempDir("bench_unpack_single");
        const auto data = MakeBenchData(size, 0xBABE0000 + size);
        const auto archive_path = tmp / "unpack_src.horo";

        // Build the archive once
        {
            InMemoryAssetProvider provider;
            provider.Add("payload.bin", data);

            Packager packer;
            packer.SetCompressionLevel(0);
            packer.AddAsset("payload.bin");
            REQUIRE(packer.Write(archive_path.string(), provider) ==
                     PackResult::Ok);
        }

        INFO("Unpack: " << size / 1024 << " KB single asset");

        BENCHMARK("Unpack-" + std::to_string(size / 1024) + "KB") {
            Packager reader;
            REQUIRE(reader.Open(archive_path.string()) == PackResult::Ok);
            std::vector<uint8_t> extracted;
            REQUIRE(reader.Extract("payload.bin", extracted) ==
                     PackResult::Ok);
            REQUIRE(extracted.size() == size);
            return extracted.size();
        };

        CleanupTempDir(tmp);
    }
}

// ==========================================================================
//  Section 8: Unpack throughput — many small assets
// ==========================================================================

TEST_CASE("Bench: unpack throughput — many small assets",
          "[benchmark][unpack][multi_asset]") {
    constexpr size_t kAssetCount = 200;
    constexpr size_t kPerAssetSize = 8192;  // 8 KB each
    const size_t total_size = kAssetCount * kPerAssetSize;

    const auto tmp = MakeTempDir("bench_unpack_multi");
    const auto archive_path = tmp / "unpack_multi.horo";

    // Build the archive once
    {
        InMemoryAssetProvider provider;
        for (size_t i = 0; i < kAssetCount; ++i) {
            const auto path =
                "assets/file_" + std::to_string(i) + ".bin";
            provider.Add(path,
                         MakeBenchData(kPerAssetSize,
                                       0xFACE0000 + static_cast<uint64_t>(i)));
        }

        Packager packer;
        packer.SetCompressionLevel(0);
        for (size_t i = 0; i < kAssetCount; ++i) {
            packer.AddAsset("assets/file_" + std::to_string(i) + ".bin");
        }
        REQUIRE(packer.Write(archive_path.string(), provider) ==
                 PackResult::Ok);
    }

    INFO("Unpack: " << kAssetCount << " assets × " << kPerAssetSize
         << " bytes = " << total_size / 1024 << " KB total");

    BENCHMARK("Unpack-" + std::to_string(kAssetCount) + "x" +
              std::to_string(kPerAssetSize / 1024) + "KB") {
        Packager reader;
        REQUIRE(reader.Open(archive_path.string()) == PackResult::Ok);

        size_t total_extracted = 0;
        for (size_t i = 0; i < kAssetCount; ++i) {
            std::vector<uint8_t> extracted;
            const auto path =
                "assets/file_" + std::to_string(i) + ".bin";
            REQUIRE(reader.Extract(path, extracted) == PackResult::Ok);
            total_extracted += extracted.size();
        }
        return total_extracted;
    };

    CleanupTempDir(tmp);
}

// ==========================================================================
//  Section 9: Full round-trip throughput — pack → extract with wall clock
// ==========================================================================

TEST_CASE("Bench: full round-trip — pack + extract end-to-end wall time",
          "[benchmark][roundtrip][wall]") {
    const auto tmp = MakeTempDir("bench_roundtrip");
    const auto archive_path = tmp / "roundtrip.horo";

    constexpr size_t kDataSize = 64 * 1024 * 1024;  // 64 MB
    const auto data = MakeBenchData(kDataSize, 0x1337C0DE);

    // ── Pack timing ──────────────────────────────────────────────────────
    InMemoryAssetProvider provider;
    provider.Add("roundtrip.bin", data);

    auto pack_start = std::chrono::high_resolution_clock::now();

    Packager packer;
    packer.AddAsset("roundtrip.bin");
    REQUIRE(packer.Write(archive_path.string(), provider) == PackResult::Ok);

    auto pack_end = std::chrono::high_resolution_clock::now();
    auto pack_elapsed = pack_end - pack_start;

    auto archive_size = fs::file_size(archive_path);

    // ── Unpack timing ────────────────────────────────────────────────────
    auto unpack_start = std::chrono::high_resolution_clock::now();

    Packager reader;
    REQUIRE(reader.Open(archive_path.string()) == PackResult::Ok);
    std::vector<uint8_t> extracted;
    REQUIRE(reader.Extract("roundtrip.bin", extracted) == PackResult::Ok);

    auto unpack_end = std::chrono::high_resolution_clock::now();
    auto unpack_elapsed = unpack_end - unpack_start;

    // ── Report ───────────────────────────────────────────────────────────
    double pack_ns =
        static_cast<double>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(pack_elapsed)
                .count());
    double unpack_ns =
        static_cast<double>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(unpack_elapsed)
                .count());

    double pack_mbps = (static_cast<double>(kDataSize) / (pack_ns / 1e9)) /
                       (1024.0 * 1024.0);
    double unpack_mbps =
        (static_cast<double>(kDataSize) / (unpack_ns / 1e9)) /
        (1024.0 * 1024.0);

    INFO("Pack wall time:   "
         << FormatWallTime(pack_elapsed)
         << "  (" << pack_mbps << " MB/s)");
    INFO("Unpack wall time: "
         << FormatWallTime(unpack_elapsed)
         << "  (" << unpack_mbps << " MB/s)");
    INFO("Archive size on disk: " << archive_size << " bytes ("
         << (archive_size * 100 / kDataSize) << "% of original)");

    REQUIRE(extracted.size() == kDataSize);
    REQUIRE(extracted[0] == data[0]);
    REQUIRE(extracted[kDataSize / 2] == data[kDataSize / 2]);
    REQUIRE(extracted[kDataSize - 1] == data[kDataSize - 1]);

    CleanupTempDir(tmp);
}

// ==========================================================================
//  Section 10: Compression impact benchmark
// ==========================================================================

TEST_CASE("Bench: pack throughput — compression level impact",
          "[benchmark][pack][compression]") {
    const auto tmp = MakeTempDir("bench_compress");
    constexpr size_t kSize = 16 * 1024 * 1024;  // 16 MB

    // Use highly compressible data (all zeros) so compression matters
    std::vector<uint8_t> compressible(kSize, 0x00);
    const auto random_data = MakeBenchData(kSize, 0xDECAFBAD);

    InMemoryAssetProvider prov_compressible;
    prov_compressible.Add("zeros.bin", compressible);

    InMemoryAssetProvider prov_random;
    prov_random.Add("random.bin", random_data);

    for (int level : {0, 1, 6, 12}) {
        const std::string label = "Pack-L" + std::to_string(level);

        BENCHMARK(label + "-compressible") {
            Packager packer;
            packer.SetCompressionLevel(level);
            packer.AddAsset("zeros.bin");
            packer.Write((tmp / "bench_compressible.horo").string(),
                         prov_compressible);
            return 0;
        };

        BENCHMARK(label + "-random") {
            Packager packer;
            packer.SetCompressionLevel(level);
            packer.AddAsset("random.bin");
            packer.Write((tmp / "bench_random.horo").string(),
                         prov_random);
            return 0;
        };
    }

    CleanupTempDir(tmp);
}

// ==========================================================================
//  Section 11: Round-trip with checksums — full pipeline timing
// ==========================================================================

TEST_CASE("Bench: round-trip with CRC32 + SHA-256 — wall time",
          "[benchmark][roundtrip][hash][wall]") {
    const auto tmp = MakeTempDir("bench_hash_rt");
    const auto archive_path = tmp / "hash_rt.horo";

    // Multiple asset sizes to show scaling
    constexpr size_t kSizes[] = {
        1024,                        // 1 KB
        64 * 1024,                   // 64 KB
        1024 * 1024,                 // 1 MB
        16 * 1024 * 1024,            // 16 MB
    };

    for (auto size : kSizes) {
        const auto data = MakeBenchData(size, 0xFEEDF00D + size);

        InMemoryAssetProvider provider;
        provider.Add("asset.bin", data);

        // ── Pack with SHA-256 ──────────────────────────────────────────
        auto t0 = std::chrono::high_resolution_clock::now();

        Packager packer;
        packer.SetCompressionLevel(0);
        packer.SetSHA256Enabled(true);
        packer.AddAsset("asset.bin");
        REQUIRE(packer.Write(archive_path.string(), provider) ==
                 PackResult::Ok);

        auto t1 = std::chrono::high_resolution_clock::now();

        // ── Extract (hash verified) ────────────────────────────────────
        Packager reader;
        REQUIRE(reader.Open(archive_path.string()) == PackResult::Ok);
        std::vector<uint8_t> extracted;
        REQUIRE(reader.Extract("asset.bin", extracted) == PackResult::Ok);

        auto t2 = std::chrono::high_resolution_clock::now();

        double pack_s =
            static_cast<double>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0)
                    .count()) / 1e9;
        double unpack_s =
            static_cast<double>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(t2 - t1)
                    .count()) / 1e9;
        double pack_mbps =
            (static_cast<double>(size) / pack_s) / (1024.0 * 1024.0);
        double unpack_mbps =
            (static_cast<double>(size) / unpack_s) / (1024.0 * 1024.0);

        INFO("Size: " << size / 1024 << " KB");
        INFO("  Pack:   " << pack_mbps << " MB/s  (" << pack_s << " s)");
        INFO("  Unpack: " << unpack_mbps << " MB/s  (" << unpack_s << " s)");

        REQUIRE(extracted.size() == size);
        REQUIRE(extracted[0] == data[0]);
        REQUIRE(extracted[size / 2] == data[size / 2]);
        REQUIRE(extracted[size - 1] == data[size - 1]);
    }

    CleanupTempDir(tmp);
}
