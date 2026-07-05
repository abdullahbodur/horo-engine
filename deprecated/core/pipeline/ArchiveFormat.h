/** @file ArchiveFormat.h
 *  @brief Binary layout and constants for the FNV-1a .horo archive format.
 *
 *  Defines the on-disk structures (header, TOC entry) and hash functions
 *  used by ArchiveBuilder and ArchiveReader for the pipeline-layer archive
 *  implementation.  Uses FNV-1a 64-bit hashing and AES-128-CTR encryption.
 *
 *  @note This is the pipeline-layer format (namespace Horo::Build).
 *        The canonical engine format lives in core/archive/HoroFormat.h
 *        (namespace Horo::Archive) and uses XXH64 + LZ4.
 */
#pragma once

#include <cstdint>
#include <string_view>

namespace Horo::Build {

// ============================================================================
// Constants
// ============================================================================

/** @brief AES block size in bytes (always 16 for AES-128 / AES-256). */
inline constexpr uint32_t kAesBlockSize = 16;

// ============================================================================
// Archive flags
// ============================================================================

/** @brief Flags stored in ArchiveHeader::flags. */
enum class ArchiveFlag : uint32_t {
    /** No special properties. */
    kNone = 0,

    /** Data chunks are AES-128-CTR encrypted. */
    kEncrypted = 1 << 0,

    // Reserved bits: 1–31
};

/** @brief Bitwise OR of two ArchiveFlag values.
 *
 *  Combines two flag bits.  Use to build the flags field in an ArchiveHeader. */
inline constexpr ArchiveFlag operator|(ArchiveFlag a, ArchiveFlag b) noexcept {
    return static_cast<ArchiveFlag>(static_cast<uint32_t>(a) |
                                    static_cast<uint32_t>(b));
}

/** @brief Bitwise AND of two ArchiveFlag values.
 *
 *  Use with HasFlag() to test whether a specific flag is set. */
inline constexpr ArchiveFlag operator&(ArchiveFlag a, ArchiveFlag b) noexcept {
    return static_cast<ArchiveFlag>(static_cast<uint32_t>(a) &
                                    static_cast<uint32_t>(b));
}

/** @brief Returns true when @p flag is set in @p value.
 *
 *  @param value  Combined flag field (e.g. ArchiveHeader::flags cast to ArchiveFlag).
 *  @param flag   Single flag bit to test.
 *  @return       True iff the bit is set. */
inline constexpr bool HasFlag(ArchiveFlag value, ArchiveFlag flag) noexcept {
    return (static_cast<uint32_t>(value) & static_cast<uint32_t>(flag)) != 0;
}

// ============================================================================
// Binary structures (packed for direct I/O)
// ============================================================================

#pragma pack(push, 1)

/**
 * @brief On-disk header of a pipeline .horo archive.
 *
 * Fixed at 48 bytes.  All multi-byte fields are little-endian.
 * The magic bytes are always the first 4 bytes for fast file-type detection.
 */
struct ArchiveHeader {
    /** Magic identifier: 'H', 'O', 'R', 'O'. */
    char magic[4];

    /** Archive format version (currently 1). */
    uint32_t version;

    /** Bitwise OR of ArchiveFlag. */
    uint32_t flags;

    /** Key Check Value: first 8 bytes of encrypting a 16-byte zero block.
     *  Used for password verification without decrypting real data. */
    uint8_t kcv[8];

    /** Byte offset from the start of the file to the TOC. */
    uint64_t tocOffset;

    /** Total size of the TOC in bytes (including the uint32_t entry count
     *  prefix). */
    uint64_t tocSize;

    /** Byte offset from the start of the file to the first data chunk. */
    uint64_t dataOffset;

    /** Reserved for future use (zero-filled). */
    uint32_t reserved;
};

/**
 * @brief A single entry in the Table of Contents.
 *
 * Each entry maps an asset-path FNV-1a hash to a contiguous chunk of
 * (optionally encrypted) data.  Entries are written sequentially after
 * a uint32_t count prefix.
 */
struct TocEntry {
    /** FNV-1a 64-bit hash of the asset path (UTF-8). */
    uint64_t pathHash;

    /** Byte offset from the start of the file to this chunk's data. */
    uint64_t offset;

    /** Size of the chunk as stored on disk (includes IV overhead if encrypted). */
    uint32_t storedSize;

    /** Size of the chunk after decryption (original asset size). */
    uint32_t originalSize;

    /** FNV-1a 64-bit hash of the asset content bytes (unencrypted). */
    uint64_t contentHash;
};

#pragma pack(pop)

/** @brief Size of the archive header in bytes. */
inline constexpr uint32_t kArchiveHeaderSize = sizeof(ArchiveHeader);

// Compile-time size verification.
static_assert(sizeof(ArchiveHeader) == 48,
              "ArchiveHeader must be exactly 48 bytes");
static_assert(sizeof(TocEntry) == 32,
              "TocEntry must be exactly 32 bytes");

// ============================================================================
// FNV-1a 64-bit hashing
// ============================================================================

/**
 * @brief Computes the FNV-1a 64-bit hash of a byte range.
 *
 * FNV-1a parameters (64-bit):
 *   - Offset basis: 14695981039346656037
 *   - Prime:        1099511628211
 *
 * @param data  Pointer to the start of the byte range.
 * @param size  Number of bytes to hash.
 * @return 64-bit FNV-1a hash.
 */
inline uint64_t Fnv1a64(const uint8_t *data, uint64_t size) {
    constexpr uint64_t kFnvOffsetBasis = 14695981039346656037ULL;
    constexpr uint64_t kFnvPrime = 1099511628211ULL;
    uint64_t hash = kFnvOffsetBasis;
    for (uint64_t i = 0; i < size; ++i) {
        hash ^= data[i];
        hash *= kFnvPrime;
    }
    return hash;
}

/**
 * @brief Computes the FNV-1a 64-bit hash of a string view.
 *
 * @param s  String to hash (interpreted as UTF-8 bytes).
 * @return 64-bit FNV-1a hash.
 */
inline uint64_t Fnv1a64(std::string_view s) {
    return Fnv1a64(reinterpret_cast<const uint8_t *>(s.data()), s.size());
}

} // namespace Horo::Build
