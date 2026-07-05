/** @file HashVerifier.h
 *  @brief CRC32 and SHA-256 hash computation utilities for .horo archive
 *         integrity verification.
 *
 *  Provides streaming, allocation-free hashers that operate on raw byte
 *  ranges.  CRC32 is used for fast per-chunk integrity checks (stored in
 *  TOCEntry::crc32); SHA-256 provides cryptographic verification stored
 *  in an optional hash block appended after the TOC.
 *
 *  Both implementations are self-contained — no external crypto libraries
 *  required.  CRC32 uses the standard Ethernet/gzip polynomial
 *  (0x04C11DB7, reflected form 0xEDB88320).  SHA-256 follows FIPS 180-4.
 */

#pragma once

#include <array>
#include <filesystem>

namespace Horo::Archive {

// ============================================================================
// CRC32
// ============================================================================

/** @brief CRC32 digest size in bytes. */
inline constexpr uint32_t kCrc32DigestSize = 4;

/** @brief Compute the CRC32 checksum of a byte range.
 *
 *  Uses the standard Ethernet / gzip / PNG polynomial (0xEDB88320
 *  reflected).  The initial value is 0xFFFFFFFF and the result is XOR'd
 *  with 0xFFFFFFFF before returning (matching the behaviour of common
 *  CRC32 implementations).
 *
 *  @param data    Pointer to the start of the data.
 *  @param length  Number of bytes to process.
 *  @return        32-bit CRC32 checksum. */
uint32_t ComputeCRC32(const uint8_t* data, size_t length) noexcept;

// ============================================================================
// SHA-256
// ============================================================================

/** @brief SHA-256 digest size in bytes (256 bits). */
inline constexpr uint32_t kSha256Size = 32;

/** @brief SHA-256 digest stored as a fixed-size byte array. */
using Sha256Digest = std::array<uint8_t, kSha256Size>;

/** @brief Streaming SHA-256 hasher.
 *
 *  Feed data in arbitrary chunks via Update() and finalize with Finish().
 *  A single Sha256Hasher produces exactly one digest; call Reset() to
 *  reuse the object for a new stream.
 *
 *  Reference: FIPS PUB 180-4. */
class Sha256Hasher {
public:
    Sha256Hasher() noexcept;
    ~Sha256Hasher() = default;

    // Non-copyable, movable.
    Sha256Hasher(const Sha256Hasher&) = delete;
    Sha256Hasher& operator=(const Sha256Hasher&) = delete;
    Sha256Hasher(Sha256Hasher&&) noexcept = default;
    Sha256Hasher& operator=(Sha256Hasher&&) noexcept = default;

    /** @brief Feed `length` bytes into the hash computation. */
    void Update(const uint8_t* data, size_t length) noexcept;

    /** @brief Finalize and write the 32-byte digest to `out_digest`.
     *
     *  After Finish(), the hasher is in an undefined state — call Reset()
     *  before feeding more data. */
    void Finish(uint8_t out_digest[kSha256Size]) noexcept;

    /** @brief Reset the hasher to its initial state for a new stream. */
    void Reset() noexcept;

private:
    uint32_t m_state[8];
    uint64_t m_bit_count;
    uint8_t  m_buffer[64];
    size_t   m_buffer_used;

    void ProcessBlock(const uint8_t block[64]) noexcept;
};

/** @brief Convenience: compute SHA-256 of a single contiguous byte range.
 *
 *  @param data    Pointer to the start of the data.
 *  @param length  Number of bytes to process.
 *  @param out_digest  Receives the 32-byte SHA-256 digest. */
void ComputeSHA256(const uint8_t* data, size_t length,
                   uint8_t out_digest[kSha256Size]) noexcept;

// ============================================================================
// File-level hashing
// ============================================================================

/** @brief Compute the SHA-256 digest of a file using streaming I/O.
 *
 *  Reads the file in fixed-size chunks (default 64 KiB) and feeds them
 *  through a Sha256Hasher, avoiding loading the entire file into memory.
 *  Safe for large files (multi-GB) with constant memory overhead.
 *
 *  @param file_path   Path to the file to hash.
 *  @param out_digest  Receives the 32-byte SHA-256 digest.
 *  @return true on success, false if the file cannot be opened or a read
 *          error occurs during streaming. */
bool ComputeFileSHA256(const std::filesystem::path& file_path,
                       uint8_t out_digest[kSha256Size]) noexcept;

} // namespace Horo::Archive
