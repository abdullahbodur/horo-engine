#pragma once

/**
 * @file SaveArchiveFraming.h
 * @brief Bounded save chunk directory validation and selective payload access.
 */

#include "Horo/Foundation/Result.h"
#include "Horo/Foundation/Sha256.h"
#include "Horo/Runtime/Save/SaveArchiveMetadata.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace Horo::Runtime {
    /** @brief Storage codec applied to one independently addressable archive chunk. */
    enum class SaveChunkCodec : std::uint16_t {
        Raw = 0,
    };

    /** @brief Explicit admission limits for an untrusted archive chunk directory. */
    struct SaveChunkDirectoryLimits final {
        std::size_t maximumEntries{16'384};                   /**< Maximum directory records. */
        std::uint64_t maximumPayloadBytes{4ULL << 30U};       /**< Maximum aggregate stored payload bytes. */
        std::uint64_t maximumDecodedChunkBytes{64ULL << 20U}; /**< Maximum decoded bytes for one chunk. */
        std::uint32_t maximumAlignment{4'096};                /**< Maximum supported power-of-two alignment. */
    };

    /** @brief Validated-on-admission framing metadata for one manifest-owned chunk. */
    struct SaveChunkDirectoryEntry final {
        SaveRecordId record;                       /**< Globally unique stable chunk identity. */
        SaveParticipantId owner;                   /**< Manifest participant that owns the chunk. */
        std::uint64_t offset{};                    /**< Byte offset relative to the payload region. */
        std::uint64_t storedByteLength{};          /**< Exact bytes occupied in the archive. */
        std::uint64_t decodedByteLength{};         /**< Exact bytes after codec processing. */
        std::uint32_t alignment{1};                /**< Required power-of-two payload alignment. */
        SaveChunkCodec codec{SaveChunkCodec::Raw}; /**< Storage codec identity. */
        Sha256Digest decodedHash;                  /**< SHA-256 of canonical decoded data. */

        [[nodiscard]] auto operator<=>(const SaveChunkDirectoryEntry &) const noexcept = default;
    };

    /** @brief A bounded directory that can be inspected without loading chunk payloads. */
    struct SaveChunkDirectory final {
        std::uint64_t payloadByteLength{};            /**< Exact stored payload region size. */
        std::vector<SaveChunkDirectoryEntry> entries; /**< Stable record-identity ordered entries. */

        [[nodiscard]] auto operator<=>(const SaveChunkDirectory &) const noexcept = default;
    };

    /** @brief Immutable owned proof that a chunk directory passed full manifest and framing validation. */
    class ValidatedSaveChunkDirectory final {
    public:
        /** @brief Returns the exact admitted payload size. @return Stored payload byte count. */
        [[nodiscard]] std::uint64_t PayloadByteLength() const noexcept;
        /** @brief Returns stable record-ordered validated entries. @return Borrowed immutable directory entries. */
        [[nodiscard]] std::span<const SaveChunkDirectoryEntry> Entries() const noexcept;

    private:
        explicit ValidatedSaveChunkDirectory(SaveChunkDirectory directory);
        SaveChunkDirectory directory_;

        friend Result<ValidatedSaveChunkDirectory> ValidateSaveChunkDirectory(SaveChunkDirectory, const SaveGameManifest &,
                                                                              const SaveChunkDirectoryLimits &);
    };

    /**
     * @brief Validates directory bounds, layout, ownership, checksums, and manifest correspondence.
     * @param directory Untrusted directory metadata, consumed into an immutable validated token on success.
     * @param manifest Already validated canonical save manifest.
     * @param limits Trusted admission limits.
     * @return Owned validated directory token, or a stable framing error.
     */
    [[nodiscard]] Result<ValidatedSaveChunkDirectory> ValidateSaveChunkDirectory(SaveChunkDirectory directory,
                                                                                 const SaveGameManifest &manifest,
                                                                                 const SaveChunkDirectoryLimits &limits = {});

    /**
     * @brief Returns one verified raw chunk without copying or decoding unrelated payloads.
     * @param payload Exact payload region bytes.
     * @param directory Immutable directory token validated once before any selections.
     * @param record Desired stable chunk identity.
     * @return Borrowed verified bytes, or an empty optional when the record is unknown and may be skipped.
     */
    [[nodiscard]] Result<std::optional<std::span<const std::byte>>> SelectSaveChunkPayload(std::span<const std::byte> payload,
                                                                                           const ValidatedSaveChunkDirectory &directory,
                                                                                           SaveRecordId record);
}  // namespace Horo::Runtime
