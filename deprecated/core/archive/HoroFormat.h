/** @file HoroFormat.h
 *  @brief Binary layout and constants for the .horo asset archive format.
 *
 *  The .horo format is a chunk-based, optionally compressed and encrypted asset
 *  container designed for submodule-friendly deployment.  Archives are
 *  self-describing: a fixed-size header is followed by data chunks, with a
 *  Table of Contents (TOC) that maps asset-path hashes to chunk offsets.
 *
 *  Layout on disk:
 *  @code
 *  [Header: 32 B]
 *  [Data Chunk 0]
 *  [Data Chunk 1]
 *  ...
 *  [Data Chunk N-1]
 *  [TOC: N * 32 B]
 *  @endcode
 *
 *  The TOC is placed at the end of the file so that it can be re-written
 *  after appending new chunks without relocating existing data.  Runtime
 *  consumers memory-map the file and read the header + TOC in one linear scan.
 *
 *  @note All multi-byte integers are little-endian.  The format does not
 *        prescribe a specific compression library, but the reference
 *        implementation uses LZ4 (block mode, LZ4F).
 */
#pragma once

#include <array>
#include <cstdint>
#include <iosfwd>
#include <vector>

namespace Horo::Archive {

// ============================================================================
// Format-level constants
// ============================================================================

/** @brief Magic number identifying a valid .horo file. */
inline constexpr std::array<char, 4> kHoroMagic = {'H', 'O', 'R', 'O'};

/** @brief Current archive format version.  Bump when the binary layout changes
 *  in a backward-incompatible way. */
inline constexpr uint32_t kHoroVersion = 1;

/** @brief Maximum number of TOC entries per archive.
 *
 *  Kept at 2^24 (~16.7M) so that a single 32-bit index can address any entry
 *  while leaving the upper byte for internal flags in the entry index itself
 *  should we ever need it. */
inline constexpr uint32_t kMaxTocEntries = 16'777'216;

// ============================================================================
// Archive-level flags (stored in HoroHeader::flags)
// ============================================================================

/** @brief Flags that describe global properties of the archive. */
enum class HoroArchiveFlags : uint32_t {
    /** No special properties.  Data chunks are stored as-is. */
    None = 0,

    /** Chunks are LZ4-compressed (reference: LZ4F block mode). */
    CompressedLZ4 = 1 << 0,

    /** Chunks are AES-256-CTR encrypted.  The nonce is prepended to each chunk.
     *  Requires an externally-managed key (the archive does not store keys). */
    EncryptedAES256CTR = 1 << 1,

    /** TOCEntry::crc32 fields are populated with CRC32 checksums of the
     *  uncompressed data.  Consumers should verify CRC32 on extraction. */
    HashCRC32 = 1 << 2,

    /** A SHA-256 hash block follows the TOC.  The block is exactly
     *  toc_count × 32 bytes and starts at toc_offset + toc_count × 32.
     *  Each entry is the SHA-256 digest of the corresponding chunk's
     *  uncompressed data. */
    HashSHA256 = 1 << 3,

    // Reserved bits: 4–31
};

/** @brief Bitwise combinable HoroArchiveFlags. */
inline constexpr HoroArchiveFlags operator|(HoroArchiveFlags a,
                                            HoroArchiveFlags b) noexcept {
    return static_cast<HoroArchiveFlags>(static_cast<uint32_t>(a) |
                                         static_cast<uint32_t>(b));
}

inline constexpr HoroArchiveFlags operator&(HoroArchiveFlags a,
                                            HoroArchiveFlags b) noexcept {
    return static_cast<HoroArchiveFlags>(static_cast<uint32_t>(a) &
                                         static_cast<uint32_t>(b));
}

inline constexpr bool HasFlag(HoroArchiveFlags value,
                              HoroArchiveFlags flag) noexcept {
    return (static_cast<uint32_t>(value) & static_cast<uint32_t>(flag)) != 0;
}

// ============================================================================
// Per-entry flags (stored in TOCEntry::flags)
// ============================================================================

/** @brief Flags that describe a single TOC entry / data chunk. */
enum class TOCEntryFlags : uint32_t {
    /** No special properties. */
    None = 0,

    /** This chunk is compressed (inherits archive compression algo). */
    Compressed = 1 << 0,

    /** This chunk is encrypted (inherits archive encryption algo). */
    Encrypted = 1 << 1,

    // Reserved bits: 2–31
};

inline constexpr TOCEntryFlags operator|(TOCEntryFlags a,
                                         TOCEntryFlags b) noexcept {
    return static_cast<TOCEntryFlags>(static_cast<uint32_t>(a) |
                                      static_cast<uint32_t>(b));
}

inline constexpr TOCEntryFlags operator&(TOCEntryFlags a,
                                         TOCEntryFlags b) noexcept {
    return static_cast<TOCEntryFlags>(static_cast<uint32_t>(a) &
                                      static_cast<uint32_t>(b));
}

// ============================================================================
// Binary structures (packed for direct I/O)
// ============================================================================

#pragma pack(push, 1)

/** @brief On-disk header of a .horo archive.
 *
 *  Fixed at 32 bytes.  All fields are little-endian.  The magic bytes are
 *  always the first 4 bytes of the file, enabling fast file-type detection
 *  without reading the full header. */
struct HoroHeader {
    /** Magic identifier: 'H', 'O', 'R', 'O'. */
    char magic[4];

    /** Format version (currently 1). */
    uint32_t version;

    /** Bitwise OR of HoroArchiveFlags. */
    uint32_t flags;

    /** Number of TOC entries in the archive. */
    uint32_t toc_count;

    /** Byte offset from the start of the file to the first TOC entry. */
    uint64_t toc_offset;

    /** Byte offset from the start of the file to the first data chunk.
     *  All data chunks are laid out contiguously from this offset. */
    uint64_t data_offset;
};

/** @brief A single entry in the Table of Contents.
 *
 *  Each entry maps an asset-path hash to a contiguous chunk of (possibly
 *  compressed / encrypted) data.  Entries are 32 bytes each so the entire
 *  TOC can be read with a single bulk read. */
struct TOCEntry {
    /** XXH64 hash of the original asset path (UTF-8, null-terminated). */
    uint64_t hash;

    /** Byte offset from the start of the file to this chunk's data. */
    uint64_t offset;

    /** Size of the chunk as stored on disk (after compression/encryption). */
    uint32_t compressed_size;

    /** Size of the chunk after decompression/decryption. */
    uint32_t uncompressed_size;

    /** Bitwise OR of TOCEntryFlags. */
    uint32_t flags;

    /** CRC32 checksum of the uncompressed asset data (Ethernet/gzip polynomial).
     *  Only meaningful when HoroArchiveFlags::HashCRC32 is set in the header.
     *  Zero on archives written without CRC32 enabled. */
    uint32_t crc32;
};

#pragma pack(pop)

// Compile-time size verification (critical for binary I/O correctness).
static_assert(sizeof(HoroHeader) == 32,
              "HoroHeader must be exactly 32 bytes");
static_assert(sizeof(TOCEntry) == 32,
              "TOCEntry must be exactly 32 bytes");
static_assert(alignof(HoroHeader) == 1,
              "HoroHeader must have packed (1-byte) alignment");
static_assert(alignof(TOCEntry) == 1,
              "TOCEntry must have packed (1-byte) alignment");
static_assert(offsetof(HoroHeader, magic) == 0,
              "magic must be at offset 0");
static_assert(offsetof(HoroHeader, version) == 4,
              "version must be at offset 4");
static_assert(offsetof(HoroHeader, toc_offset) == 16,
              "toc_offset must be at offset 16");
static_assert(offsetof(HoroHeader, data_offset) == 24,
              "data_offset must be at offset 24");

// ============================================================================
// Format I/O helpers
// ============================================================================

/** @brief Read a HoroHeader from a binary stream.
 *  @param stream  Input stream positioned at the start of the header.
 *  @param out_header  Destination for the read header.
 *  @return true on success, false on incomplete read. */
bool ReadHeader(std::istream& stream, HoroHeader& out_header);

/** @brief Write a HoroHeader to a binary stream.
 *  @param stream  Output stream.
 *  @param header  Header to write.
 *  @return true on success. */
bool WriteHeader(std::ostream& stream, const HoroHeader& header);

/** @brief Validate a HoroHeader's fields against the format specification.
 *
 *  Checks magic, version bounds, field consistency (offsets must be
 *  monotonically increasing; data_offset must sit between header and TOC),
 *  and reserved flag bits.
 *
 *  @param header  Header to validate.
 *  @return true if the header represents a well-formed .horo archive. */
bool ValidateHeader(const HoroHeader& header);

/** @brief Read the Table of Contents from a binary stream.
 *  @param stream  Input stream positioned at the first TOCEntry.
 *  @param count   Number of entries to read.
 *  @param out_entries  Destination vector (resized & populated).
 *  @return true on success, false on incomplete read or invalid entry. */
bool ReadTOC(std::istream& stream, uint32_t count,
             std::vector<TOCEntry>& out_entries);

/** @brief Write the Table of Contents to a binary stream.
 *  @param stream   Output stream.
 *  @param entries  Entries to write.
 *  @return true on success. */
bool WriteTOC(std::ostream& stream, const std::vector<TOCEntry>& entries);

} // namespace Horo::Archive
