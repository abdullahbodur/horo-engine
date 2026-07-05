/** @file Packager.cpp
 *  @brief Archive packer/unpacker implementation for the .horo format. */
#include "core/archive/Packager.h"

#include "core/archive/HoroFormat.h"
#include "core/archive/HashVerifier.h"
#include "core/crypto/AESContext.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <lz4.h>
#include <lz4hc.h>
#include <random>
#include <span>
#include <unordered_map>
#include <xxhash.h>

namespace Horo::Archive {
namespace {

// ====================================================================
// Internal constants
// ====================================================================

/** Maximum chunk size to process in a single LZ4 call.  1 MiB is a
 *  reasonable default; larger inputs are processed in a loop. */
inline constexpr size_t kMaxChunkSize = 1 << 20;

/** Temporary file extension for atomic writes. */
inline constexpr const char* kTempExtension = ".horo.tmp";

/** Size of the nonce/counter prefix stored before each encrypted chunk. */
inline constexpr uint32_t kEncryptedChunkNonceSize = Horo::Crypto::kAesBlockSize;

// ====================================================================
// Utility: path hashing
// ====================================================================

/** Compute the XXH64 hash of an asset path string.
 *
 *  The hash is computed over the raw bytes of the path (UTF-8 encoded),
 *  including the null terminator to disambiguate prefix collisions. */
uint64_t HashPath(const std::string& path) noexcept {
    return XXH64(path.c_str(), path.size() + 1, 0);
}

// ====================================================================
// Utility: LZ4 wrappers
// ====================================================================

/** Compress `src` into `dst` using LZ4.
 *
 *  Uses LZ4_compress_HC when compression level > 1, otherwise falls back
 *  to the fast compressor (LZ4_compress_default).  The caller must ensure
 *  dst has enough capacity (LZ4_compressBound). */
bool CompressData(const std::vector<uint8_t>& src, std::vector<uint8_t>& dst,
                  int level) {
    const auto src_size = static_cast<int>(src.size());
    if (src_size == 0) {
        dst.clear();
        return true;
    }

    const auto bound = LZ4_compressBound(src_size);
    dst.resize(static_cast<size_t>(bound));

    int compressed_size = 0;
    if (level <= 1) {
        compressed_size = LZ4_compress_default(
            reinterpret_cast<const char*>(src.data()),
            reinterpret_cast<char*>(dst.data()), src_size, bound);
    } else {
        compressed_size = LZ4_compress_HC(
            reinterpret_cast<const char*>(src.data()),
            reinterpret_cast<char*>(dst.data()), src_size, bound,
            std::min(level, LZ4HC_CLEVEL_MAX));
    }

    if (compressed_size <= 0) {
        return false;
    }

    dst.resize(static_cast<size_t>(compressed_size));
    return true;
}

/** Decompress `src` into `dst` using LZ4.
 *
 *  The caller must set dst to the expected uncompressed size before
 *  calling. */
bool DecompressData(const std::vector<uint8_t>& src,
                    std::vector<uint8_t>& dst) {
    const auto src_size = static_cast<int>(src.size());
    const auto dst_capacity = static_cast<int>(dst.size());

    if (src_size == 0 || dst_capacity == 0) {
        return false;
    }

    const auto decompressed = LZ4_decompress_safe(
        reinterpret_cast<const char*>(src.data()),
        reinterpret_cast<char*>(dst.data()), src_size, dst_capacity);

    return decompressed == dst_capacity;
}

/** Fill a nonce buffer with cryptographically secure randomness. */
void GenerateNonce(std::array<uint8_t, kEncryptedChunkNonceSize>& nonce) {
    // std::random_device provides CSPRNG-quality entropy backed by OS
    // sources (RDRAND, /dev/urandom, etc.) on all supported platforms.
    std::random_device rd;
    for (uint32_t i = 0; i < 8; ++i) {
        nonce[i] = static_cast<uint8_t>(rd() & 0xFFu);
    }
    // Last 8 bytes are the CTR counter and intentionally start at zero.
    std::fill(nonce.begin() + 8, nonce.end(), uint8_t{0});
}

/** Encrypt a stored chunk in-place, prepending a nonce/counter block. */
bool EncryptChunk(std::vector<uint8_t>& chunk,
                  const std::array<uint8_t, 32>& key) {
    Horo::Crypto::AESContext ctx;
    if (!ctx.Init(key.data())) {
        return false;
    }

    std::vector<uint8_t> encrypted(kEncryptedChunkNonceSize + chunk.size());
    std::array<uint8_t, kEncryptedChunkNonceSize> nonce{};
    GenerateNonce(nonce);
    std::memcpy(encrypted.data(), nonce.data(), nonce.size());
    std::memcpy(encrypted.data() + kEncryptedChunkNonceSize,
                chunk.data(), chunk.size());

    std::array<uint8_t, kEncryptedChunkNonceSize> counter{};
    std::memcpy(counter.data(), encrypted.data(), counter.size());
    auto* data = encrypted.data() + kEncryptedChunkNonceSize;
    auto remaining = static_cast<uint32_t>(chunk.size());
    while (remaining > 0) {
        const auto step =
            std::min<uint32_t>(remaining, Horo::Crypto::kAesBlockSize);
        Horo::Crypto::AesCtrProcess(ctx, counter.data(), data, step);
        data += step;
        remaining -= step;
    }

    chunk = std::move(encrypted);
    return true;
}

/** Decrypt a stored chunk into its original compressed/raw payload. */
bool DecryptChunk(const std::vector<uint8_t>& chunk,
                  const std::array<uint8_t, 32>& key,
                  std::vector<uint8_t>& out_plaintext) {
    if (chunk.size() < kEncryptedChunkNonceSize) {
        return false;
    }

    Horo::Crypto::AESContext ctx;
    if (!ctx.Init(key.data())) {
        return false;
    }

    out_plaintext.assign(chunk.begin() + kEncryptedChunkNonceSize, chunk.end());
    std::array<uint8_t, kEncryptedChunkNonceSize> counter{};
    std::memcpy(counter.data(), chunk.data(), counter.size());

    auto* data = out_plaintext.data();
    auto remaining = static_cast<uint32_t>(out_plaintext.size());
    while (remaining > 0) {
        const auto step =
            std::min<uint32_t>(remaining, Horo::Crypto::kAesBlockSize);
        Horo::Crypto::AesCtrProcess(ctx, counter.data(), data, step);
        data += step;
        remaining -= step;
    }

    return true;
}

// ====================================================================
// Utility: file I/O helpers
// ====================================================================

/** Read a file into a byte vector. */
bool ReadFile(const std::string& path, std::vector<uint8_t>& out_data) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        return false;
    }

    const auto size = file.tellg();
    if (size <= 0) {
        out_data.clear();
        return true;
    }

    out_data.resize(static_cast<size_t>(size));
    file.seekg(0, std::ios::beg);
    file.read(reinterpret_cast<char*>(out_data.data()), size);
    return file.good();
}

/** Write a byte vector to a file, creating directories as needed. */
bool WriteFile(const std::string& path, const std::vector<uint8_t>& data) {
    // Create parent directory if it doesn't exist.
    if (const auto slash = path.find_last_of("/\\");
        slash != std::string::npos) {
        std::string dir = path.substr(0, slash);
        // Best-effort mkdir; we rely on the caller having a valid output dir.
        std::filesystem::create_directories(dir);
    }

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
        return false;
    }

    file.write(reinterpret_cast<const char*>(data.data()),
               static_cast<std::streamsize>(data.size()));
    return file.good();
}

} // anonymous namespace

// ====================================================================
// Packager::Impl
// ====================================================================

struct Packager::Impl {
    // ---- Pack state ----
    struct PendingAsset {
        std::string path;
        uint64_t hash = 0;
    };
    std::vector<PendingAsset> m_pending_assets;
    int m_compression_level = 1;
    bool m_encryption_enabled = false;
    std::array<uint8_t, 32> m_encryption_key{};
    bool m_has_encryption_key = false;
    bool m_sha256_enabled = false;

    // ---- Read state (populated by Open) ----
    bool m_is_open = false;
    std::string m_archive_path;
    HoroHeader m_header{};
    std::vector<TOCEntry> m_toc;
    // Reverse map: hash -> TOC index for O(1) extraction.
    std::unordered_map<uint64_t, size_t> m_hash_to_index;
    // Path strings stored alongside TOC (loaded from a string table,
    // or reconstructed — for Phase 1 we store them in a separate pass).
    // Since the .horo format uses hash-based lookup, we need a way to
    // map hash → path for ExtractAll/ListAssets.  In Phase 1, we store
    // a string table as a special "manifest" entry (hash = 0).
    std::unordered_map<uint64_t, std::string> m_hash_to_path;
    // SHA-256 digests loaded from the hash block during Open().
    // Indexed by TOC position (parallel to m_toc).  Empty when
    // the archive does not have the HashSHA256 flag.
    std::vector<Sha256Digest> m_sha256_digests;
};

PackResult ReadStringTable(
    std::ifstream& file, const std::vector<TOCEntry>& toc,
    const std::unordered_map<uint64_t, size_t>& hash_to_index,
    bool has_encryption_key, const std::array<uint8_t, 32>& encryption_key,
    std::unordered_map<uint64_t, std::string>& out_hash_to_path) {
    using enum PackResult;
    if (!hash_to_index.contains(0))
        return Ok;
    const auto& entry = toc.at(hash_to_index.at(0));
    std::vector<uint8_t> chunk(entry.compressed_size);
    file.seekg(static_cast<std::streamoff>(entry.offset), std::ios::beg);
    file.read(reinterpret_cast<char*>(chunk.data()),
              static_cast<std::streamsize>(entry.compressed_size));
    if (!file.good())
        return IoError;

    const bool encrypted =
        (entry.flags & static_cast<uint32_t>(TOCEntryFlags::Encrypted)) != 0;
    std::vector<uint8_t> data;
    if (encrypted) {
        if (!has_encryption_key ||
            !DecryptChunk(chunk, encryption_key, data))
            return EncryptionFailed;
    } else {
        data = std::move(chunk);
    }

    if (const bool compressed =
            (entry.flags &
             static_cast<uint32_t>(TOCEntryFlags::Compressed)) != 0;
        compressed) {
        std::vector<uint8_t> decompressed(entry.uncompressed_size);
        if (!DecompressData(data, decompressed))
            return DecompressionFailed;
        data = std::move(decompressed);
    }

    if (data.size() < sizeof(uint32_t))
        return InvalidTOC;
    uint32_t count{};
    std::memcpy(&count, data.data(), sizeof(count));
    if (const auto expected_count = toc.size() - 1;
        count != expected_count)
        return encrypted ? EncryptionFailed : InvalidTOC;

    out_hash_to_path.clear();
    out_hash_to_path.reserve(count);
    auto remaining =
        std::as_bytes(std::span{data}).subspan(sizeof(uint32_t));
    for (uint32_t i = 0; i < count; ++i) {
        if (remaining.size() < sizeof(uint64_t) + sizeof(uint32_t))
            return InvalidTOC;
        uint64_t hash{};
        std::memcpy(&hash, remaining.data(), sizeof(hash));
        remaining = remaining.subspan(sizeof(hash));
        uint32_t path_length{};
        std::memcpy(&path_length, remaining.data(), sizeof(path_length));
        remaining = remaining.subspan(sizeof(path_length));
        if (remaining.size() < path_length)
            return InvalidTOC;
        std::string path(path_length, '\0');
        std::memcpy(path.data(), remaining.data(), path_length);
        remaining = remaining.subspan(path_length);
        out_hash_to_path[hash] = std::move(path);
    }
    return Ok;
}

PackResult ReadSha256Block(std::ifstream& file, const HoroHeader& header,
                           std::vector<Sha256Digest>& out_digests) {
    using enum PackResult;
    if (!HasFlag(static_cast<HoroArchiveFlags>(header.flags),
                 HoroArchiveFlags::HashSHA256) ||
        header.toc_count == 0)
        return Ok;

    const auto block_offset =
        header.toc_offset +
        static_cast<uint64_t>(header.toc_count) * sizeof(TOCEntry);
    const auto block_size =
        static_cast<std::streamsize>(header.toc_count * kSha256Size);
    file.seekg(static_cast<std::streamoff>(block_offset), std::ios::beg);
    out_digests.resize(header.toc_count);
    file.read(reinterpret_cast<char*>(out_digests.data()), block_size);
    return file.good() ? Ok : HashMismatch;
}

// ====================================================================
// Construction / Destruction
// ====================================================================

/** @copydoc Packager::Packager */
Packager::Packager() : m_impl(std::make_unique<Impl>()) {}

/** @copydoc Packager::~Packager */
Packager::~Packager() = default;

Packager::Packager(Packager&& other) noexcept = default;
Packager& Packager::operator=(Packager&& other) noexcept = default;

// ====================================================================
// Packing
// ====================================================================

/** @copydoc Packager::AddAsset */
PackResult Packager::AddAsset(const std::string& path) {
    using enum PackResult;
    if (path.empty()) {
        return InvalidPath;
    }
    // Reject paths with null bytes (would break C-string hashing).
    if (path.find('\0') != std::string::npos) {
        return InvalidPath;
    }

    Impl::PendingAsset asset;
    asset.path = path;
    asset.hash = HashPath(path);
    m_impl->m_pending_assets.push_back(std::move(asset));
    return Ok;
}

/** @brief A processed asset ready for TOC construction. */
struct ProcessedAsset {
    uint64_t hash;
    std::vector<uint8_t> compressed_data;
    uint32_t compressed_size;
    uint32_t uncompressed_size;
    TOCEntryFlags flags;
    uint32_t crc32; // CRC32 of uncompressed data
};

struct AssetProcessingOptions {
    bool sha256_enabled;
    int compression_level;
    bool encryption_enabled;
    const std::array<uint8_t, 32>& encryption_key;
    bool has_encryption_key;
};

/** @brief Processes a single pending asset: fetch, compress, encrypt, CRC32, SHA-256.
 *
 *  Fills a ProcessedAsset and appends a SHA-256 digest when enabled.
 *  Returns PackResult::Ok on success. */
template <typename Provider>
PackResult ProcessSingleAsset(const std::string& pending_path,
                              uint64_t pending_hash,
                              const Provider& provider,
                              const AssetProcessingOptions& options,
                              HoroArchiveFlags& archive_flags,
                              std::vector<ProcessedAsset>& processed,
                              std::vector<Sha256Digest>& sha256_digests) {
    using enum PackResult;

    std::vector<uint8_t> raw_data;
    if (!provider(pending_path, raw_data))
        return InvalidInput;

    ProcessedAsset pa;
    pa.hash = pending_hash;
    pa.uncompressed_size = static_cast<uint32_t>(raw_data.size());
    pa.flags = TOCEntryFlags::None;
    pa.crc32 = ComputeCRC32(raw_data.data(), raw_data.size());

    if (options.sha256_enabled) {
        Sha256Digest digest{};
        ComputeSHA256(raw_data.data(), raw_data.size(), digest.data());
        sha256_digests.push_back(digest);
    }

    if (options.compression_level > 0 && !raw_data.empty()) {
        std::vector<uint8_t> compressed;
        if (!CompressData(raw_data, compressed, options.compression_level))
            return CompressionFailed;
        if (compressed.size() < raw_data.size()) {
            pa.compressed_data = std::move(compressed);
            pa.flags = pa.flags | TOCEntryFlags::Compressed;
            archive_flags = archive_flags | HoroArchiveFlags::CompressedLZ4;
        } else {
            pa.compressed_data = std::move(raw_data);
        }
    } else {
        pa.compressed_data = std::move(raw_data);
    }

    pa.compressed_size = static_cast<uint32_t>(pa.compressed_data.size());

    if (options.encryption_enabled) {
        if (!options.has_encryption_key)
            return InvalidInput;
        if (!EncryptChunk(pa.compressed_data, options.encryption_key))
            return EncryptionFailed;
        pa.flags = pa.flags | TOCEntryFlags::Encrypted;
        archive_flags = archive_flags | HoroArchiveFlags::EncryptedAES256CTR;
        pa.compressed_size = static_cast<uint32_t>(pa.compressed_data.size());
    }

    processed.push_back(std::move(pa));
    return Ok;
}

template <typename T>
void AppendObjectBytes(std::vector<uint8_t>& destination, const T& value) {
    const auto offset = destination.size();
    destination.resize(offset + sizeof(T));
    std::memcpy(destination.data() + offset, &value, sizeof(T));
}

template <typename PendingAssets>
PackResult BuildStringTable(
    const PendingAssets& pending_assets,
    const AssetProcessingOptions& options, HoroArchiveFlags& archive_flags,
    std::vector<ProcessedAsset>& processed,
    std::vector<Sha256Digest>& sha256_digests) {
    using enum PackResult;
    std::vector<uint8_t> string_table;
    const auto count = static_cast<uint32_t>(pending_assets.size());
    AppendObjectBytes(string_table, count);
    for (const auto& pending : pending_assets) {
        AppendObjectBytes(string_table, pending.hash);
        const auto path_length = static_cast<uint32_t>(pending.path.size());
        AppendObjectBytes(string_table, path_length);
        string_table.insert(string_table.end(), pending.path.begin(),
                            pending.path.end());
    }

    ProcessedAsset table{};
    table.hash = 0;
    table.uncompressed_size = static_cast<uint32_t>(string_table.size());
    table.compressed_data = std::move(string_table);
    table.compressed_size = table.uncompressed_size;
    table.flags = TOCEntryFlags::None;
    table.crc32 = ComputeCRC32(table.compressed_data.data(),
                               table.compressed_data.size());
    if (options.sha256_enabled) {
        Sha256Digest digest{};
        ComputeSHA256(table.compressed_data.data(),
                      table.compressed_data.size(), digest.data());
        sha256_digests.push_back(digest);
    }
    if (options.encryption_enabled) {
        if (!options.has_encryption_key)
            return InvalidInput;
        if (!EncryptChunk(table.compressed_data, options.encryption_key))
            return EncryptionFailed;
        table.flags = table.flags | TOCEntryFlags::Encrypted;
        archive_flags =
            archive_flags | HoroArchiveFlags::EncryptedAES256CTR;
        table.compressed_size =
            static_cast<uint32_t>(table.compressed_data.size());
    }
    processed.push_back(std::move(table));
    return Ok;
}

PackResult WriteArchiveFile(
    const std::filesystem::path& output_path,
    const std::vector<ProcessedAsset>& processed,
    const std::vector<Sha256Digest>& sha256_digests,
    HoroArchiveFlags archive_flags, bool sha256_enabled) {
    using enum PackResult;
    constexpr uint64_t kHeaderSize = sizeof(HoroHeader);
    uint64_t data_offset = kHeaderSize;
    std::vector<TOCEntry> toc;
    toc.reserve(processed.size());
    for (const auto& asset : processed) {
        toc.push_back({asset.hash, data_offset, asset.compressed_size,
                       asset.uncompressed_size,
                       static_cast<uint32_t>(asset.flags), asset.crc32});
        data_offset += asset.compressed_size;
    }

    archive_flags = archive_flags | HoroArchiveFlags::HashCRC32;
    if (sha256_enabled)
        archive_flags = archive_flags | HoroArchiveFlags::HashSHA256;

    HoroHeader header{};
    std::memcpy(header.magic, kHoroMagic.data(), kHoroMagic.size());
    header.version = kHoroVersion;
    header.flags = static_cast<uint32_t>(archive_flags);
    header.toc_count = static_cast<uint32_t>(toc.size());
    header.toc_offset = data_offset;
    header.data_offset = kHeaderSize;

    auto temporary_path = output_path;
    temporary_path += kTempExtension;
    {
        std::ofstream file(temporary_path,
                           std::ios::binary | std::ios::trunc);
        if (!file.is_open() || !WriteHeader(file, header))
            return IoError;
        for (const auto& asset : processed) {
            file.write(
                reinterpret_cast<const char*>(asset.compressed_data.data()),
                static_cast<std::streamsize>(asset.compressed_data.size()));
            if (!file.good())
                return IoError;
        }
        if (!WriteTOC(file, toc))
            return IoError;
        if (sha256_enabled) {
            for (const auto& digest : sha256_digests) {
                file.write(reinterpret_cast<const char*>(digest.data()),
                           static_cast<std::streamsize>(digest.size()));
                if (!file.good())
                    return IoError;
            }
        }
    }

    std::error_code error;
    std::filesystem::rename(temporary_path, output_path, error);
    if (error) {
        std::filesystem::remove(temporary_path);
        return IoError;
    }
    return Ok;
}

/** @copydoc Packager::Write */
PackResult Packager::Write(const std::string& output_path,
                           const AssetDataProvider& provider) const {
    using enum PackResult;
    if (m_impl->m_pending_assets.empty()) {
        return InvalidInput;
    }
    if (!provider) {
        return InvalidInput;
    }

    // ---- Phase 1: Collect and compress all assets ----

    std::vector<ProcessedAsset> processed;
    processed.reserve(m_impl->m_pending_assets.size());

    HoroArchiveFlags archive_flags = HoroArchiveFlags::None;

    // SHA-256 digests collected in parallel with `processed`.
    std::vector<Sha256Digest> sha256_digests;
    sha256_digests.reserve(m_impl->m_pending_assets.size() + 1); // +1 for string table
    const AssetProcessingOptions options{
        m_impl->m_sha256_enabled,      m_impl->m_compression_level,
        m_impl->m_encryption_enabled,  m_impl->m_encryption_key,
        m_impl->m_has_encryption_key};

    for (const auto& pending : m_impl->m_pending_assets) {
        if (const auto result = ProcessSingleAsset(
                pending.path, pending.hash, provider, options, archive_flags,
                processed, sha256_digests);
            result != Ok)
            return result;
    }

    if (const auto result =
            BuildStringTable(m_impl->m_pending_assets, options, archive_flags,
                             processed, sha256_digests);
        result != Ok)
        return result;

    return WriteArchiveFile(std::filesystem::path(output_path), processed,
                            sha256_digests, archive_flags,
                            m_impl->m_sha256_enabled);
}

/** @copydoc Packager::Clear */
void Packager::Clear() {
    m_impl->m_pending_assets.clear();
}

/** @copydoc Packager::AssetCount */
size_t Packager::AssetCount() const {
    return m_impl->m_pending_assets.size();
}

// ====================================================================
// Unpacking
// ====================================================================

/** @copydoc Packager::Open */
PackResult Packager::Open(const std::string& archive_path) {
    using enum PackResult;
    // Close any previously open archive.
    m_impl->m_is_open = false;
    m_impl->m_toc.clear();
    m_impl->m_hash_to_index.clear();
    m_impl->m_hash_to_path.clear();
    m_impl->m_sha256_digests.clear();

    std::ifstream file(archive_path, std::ios::binary);
    if (!file.is_open()) {
        return IoError;
    }

    // Read and validate header.
    HoroHeader header{};
    if (!ReadHeader(file, header)) {
        return IoError;
    }
    if (!ValidateHeader(header)) {
        return InvalidMagic;
    }

    // Read TOC.
    file.seekg(static_cast<std::streamoff>(header.toc_offset),
               std::ios::beg);
    std::vector<TOCEntry> toc;
    if (!ReadTOC(file, header.toc_count, toc)) {
        return InvalidTOC;
    }

    // Build reverse lookup (hash → TOC index).
    std::unordered_map<uint64_t, size_t> hash_to_index;
    hash_to_index.reserve(toc.size());
    for (size_t i = 0; i < toc.size(); ++i) {
        // Duplicate hash check.
        if (hash_to_index.contains(toc[i].hash)) {
            return InvalidTOC;
        }
        hash_to_index[toc[i].hash] = i;
    }

    if (const auto result =
            ReadStringTable(file, toc, hash_to_index,
                            m_impl->m_has_encryption_key,
                            m_impl->m_encryption_key,
                            m_impl->m_hash_to_path);
        result != Ok)
        return result;

    m_impl->m_header = header;
    m_impl->m_toc = std::move(toc);
    m_impl->m_hash_to_index = std::move(hash_to_index);
    m_impl->m_archive_path = archive_path;
    m_impl->m_is_open = true;

    if (const auto result =
            ReadSha256Block(file, header, m_impl->m_sha256_digests);
        result != Ok) {
        m_impl->m_is_open = false;
        return result;
    }

    return Ok;
}

/** @copydoc Packager::Extract */
PackResult Packager::Extract(const std::string& asset_path,
                             std::vector<uint8_t>& out_data) {
    if (!m_impl->m_is_open) {
        return PackResult::InvalidInput;
    }

    const auto target_hash = HashPath(asset_path);
    auto it = m_impl->m_hash_to_index.find(target_hash);
    if (it == m_impl->m_hash_to_index.end()) {
        return PackResult::InvalidPath;
    }

    const auto& entry = m_impl->m_toc[it->second];

    // Read the compressed (or raw) data from the archive.
    std::vector<uint8_t> raw_chunk(entry.compressed_size);

    std::ifstream file(m_impl->m_archive_path, std::ios::binary);
    if (!file.is_open()) {
        return PackResult::IoError;
    }

    file.seekg(static_cast<std::streamoff>(entry.offset), std::ios::beg);
    file.read(reinterpret_cast<char*>(raw_chunk.data()),
              static_cast<std::streamsize>(entry.compressed_size));
    if (!file.good()) {
        return PackResult::IoError;
    }

    const bool is_compressed =
        (entry.flags & static_cast<uint32_t>(TOCEntryFlags::Compressed)) != 0;
    const bool is_encrypted =
        (entry.flags & static_cast<uint32_t>(TOCEntryFlags::Encrypted)) != 0;

    std::vector<uint8_t> stored_chunk;
    if (is_encrypted) {
        if (!m_impl->m_has_encryption_key) {
            return PackResult::EncryptionFailed;
        }
        if (!DecryptChunk(raw_chunk, m_impl->m_encryption_key, stored_chunk)) {
            return PackResult::EncryptionFailed;
        }
    } else {
        stored_chunk = std::move(raw_chunk);
    }

    if (is_compressed) {
        // Decompress.
        out_data.resize(entry.uncompressed_size);
        if (!DecompressData(stored_chunk, out_data)) {
            return PackResult::DecompressionFailed;
        }
    } else {
        // Raw copy.
        out_data = std::move(stored_chunk);
    }

    // Integrity check: verify decompressed size.
    if (out_data.size() != entry.uncompressed_size) {
        return PackResult::DecompressionFailed;
    }

    // CRC32 verification (always present on archives written by this version).
    if (HasFlag(static_cast<HoroArchiveFlags>(m_impl->m_header.flags),
                HoroArchiveFlags::HashCRC32) &&
        entry.crc32 != 0) {
        const auto computed =
            ComputeCRC32(out_data.data(), out_data.size());
        if (computed != entry.crc32) {
            return PackResult::HashMismatch;
        }
    }

    // SHA-256 verification (only when archive has the HashSHA256 flag).
    if (!m_impl->m_sha256_digests.empty()) {
        const size_t toc_idx = it->second;
        std::array<uint8_t, kSha256Size> computed_sha{};
        ComputeSHA256(out_data.data(), out_data.size(),
                      computed_sha.data());
        if (std::memcmp(computed_sha.data(),
                        m_impl->m_sha256_digests[toc_idx].data(),
                        kSha256Size) != 0) {
            return PackResult::HashMismatch;
        }
    }

    return PackResult::Ok;
}

/** @copydoc Packager::ExtractAll */
PackResult Packager::ExtractAll(const std::string& output_dir) {
    using enum PackResult;
    if (!m_impl->m_is_open) {
        return InvalidInput;
    }

    for (const auto& [hash, path] : m_impl->m_hash_to_path) {
        if (hash == 0) {
            continue; // Skip string table.
        }

        std::vector<uint8_t> data;
        if (const auto result = Extract(path, data); result != Ok) {
            return result;
        }

        const auto full_path = output_dir + "/" + path;
        if (!WriteFile(full_path, data)) {
            return IoError;
        }
    }

    return Ok;
}

/** @copydoc Packager::ListAssets */
PackResult Packager::ListAssets(std::vector<std::string>& out_paths) const {
    if (!m_impl->m_is_open) {
        return PackResult::InvalidInput;
    }

    out_paths.clear();
    out_paths.reserve(m_impl->m_hash_to_path.size());
    for (const auto& [hash, path] : m_impl->m_hash_to_path) {
        if (hash != 0) {
            out_paths.push_back(path);
        }
    }
    return PackResult::Ok;
}

/** @copydoc Packager::IsOpen */
bool Packager::IsOpen() const {
    return m_impl->m_is_open;
}

// ====================================================================
// Configuration
// ====================================================================

/** @copydoc Packager::SetCompressionLevel */
void Packager::SetCompressionLevel(int level) {
    if (level < 0) {
        level = 0;
    }
    if (level > LZ4HC_CLEVEL_MAX) {
        level = LZ4HC_CLEVEL_MAX;
    }
    m_impl->m_compression_level = level;
}

/** @copydoc Packager::SetEncryptionEnabled */
void Packager::SetEncryptionEnabled(bool enabled) {
    m_impl->m_encryption_enabled = enabled;
}

/** @copydoc Packager::SetEncryptionKey */
void Packager::SetEncryptionKey(const std::array<uint8_t, 32>& key) {
    m_impl->m_encryption_key = key;
    m_impl->m_has_encryption_key = true;
}

/** @copydoc Packager::SetSHA256Enabled */
void Packager::SetSHA256Enabled(bool enabled) {
    m_impl->m_sha256_enabled = enabled;
}

} // namespace Horo::Archive
