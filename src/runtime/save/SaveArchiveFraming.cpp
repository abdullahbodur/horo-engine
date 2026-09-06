#include "Horo/Runtime/Save/SaveArchiveFraming.h"

#include "Horo/Runtime/Save/SaveErrors.h"

#include <algorithm>
#include <bit>
#include <limits>
#include <utility>

namespace Horo::Runtime {
    namespace {
        /** @brief Reports whether directory limits are finite and internally coherent. */
        [[nodiscard]] bool HasValidLimits(const SaveChunkDirectoryLimits &limits) noexcept {
            return limits.maximumEntries != 0 && limits.maximumPayloadBytes != 0 && limits.maximumDecodedChunkBytes != 0 &&
                   limits.maximumAlignment != 0 && std::has_single_bit(limits.maximumAlignment);
        }

        /** @brief Looks up the manifest owner of one chunk identity. */
        [[nodiscard]] const SaveParticipantId *FindManifestOwner(const SaveGameManifest &manifest, const SaveRecordId record) noexcept {
            for (const SaveManifestParticipant &participant : manifest.participants) {
                if (std::ranges::binary_search(participant.chunks, record))
                    return &participant.participant;
            }
            return nullptr;
        }

        /** @brief Validates one entry's identity, codec, and alignment fields. */
        [[nodiscard]] bool HasValidEntryIdentity(const SaveChunkDirectoryEntry &entry, const SaveChunkDirectoryLimits &limits) noexcept {
            return entry.record.IsValid() && entry.owner.IsValid() && entry.alignment != 0 && entry.alignment <= limits.maximumAlignment &&
                   std::has_single_bit(entry.alignment) && entry.codec == SaveChunkCodec::Raw;
        }

        /** @brief Validates one entry's length fields and checked end offset. */
        [[nodiscard]] bool HasValidEntryLengths(const SaveChunkDirectoryEntry &entry, const SaveChunkDirectoryLimits &limits) noexcept {
            return entry.storedByteLength != 0 && entry.decodedByteLength != 0 &&
                   entry.decodedByteLength <= limits.maximumDecodedChunkBytes && entry.storedByteLength == entry.decodedByteLength &&
                   entry.offset <= std::numeric_limits<std::uint64_t>::max() - entry.storedByteLength;
        }

        /** @brief Counts manifest chunks without touching archive payload bytes. */
        [[nodiscard]] std::size_t CountManifestChunks(const SaveGameManifest &manifest) noexcept {
            std::size_t count = 0;
            for (const SaveManifestParticipant &participant : manifest.participants)
                count += participant.chunks.size();
            return count;
        }

        /** @brief Validates one entry against its expected contiguous payload position. */
        [[nodiscard]] bool HasValidEntryLayout(const SaveChunkDirectoryEntry &entry, const std::uint64_t expectedOffset,
                                               const std::uint64_t payloadByteLength, const SaveChunkDirectoryLimits &limits) noexcept {
            return HasValidEntryIdentity(entry, limits) && HasValidEntryLengths(entry, limits) && entry.offset == expectedOffset &&
                   entry.offset % entry.alignment == 0 && entry.offset + entry.storedByteLength <= payloadByteLength;
        }

        /** @brief Validates the complete contiguous entry sequence and ownership mapping. */
        [[nodiscard]] bool HasValidEntries(const SaveChunkDirectory &directory, const SaveGameManifest &manifest,
                                           const SaveChunkDirectoryLimits &limits) noexcept {
            std::uint64_t expectedOffset = 0;
            const SaveRecordId *previousRecord = nullptr;
            for (const SaveChunkDirectoryEntry &entry : directory.entries) {
                const bool layoutValid = HasValidEntryLayout(entry, expectedOffset, directory.payloadByteLength, limits);
                const bool orderValid = previousRecord == nullptr || *previousRecord < entry.record;
                const SaveParticipantId *owner = FindManifestOwner(manifest, entry.record);
                if (!layoutValid || !orderValid || owner == nullptr || *owner != entry.owner)
                    return false;
                expectedOffset += entry.storedByteLength;
                previousRecord = &entry.record;
            }
            return expectedOffset == directory.payloadByteLength;
        }
    }  // namespace

    ValidatedSaveChunkDirectory::ValidatedSaveChunkDirectory(SaveChunkDirectory directory) : directory_(std::move(directory)) {}

    /** @copydoc ValidatedSaveChunkDirectory::PayloadByteLength */
    std::uint64_t ValidatedSaveChunkDirectory::PayloadByteLength() const noexcept {
        return directory_.payloadByteLength;
    }

    /** @copydoc ValidatedSaveChunkDirectory::Entries */
    std::span<const SaveChunkDirectoryEntry> ValidatedSaveChunkDirectory::Entries() const noexcept {
        return directory_.entries;
    }

    /** @copydoc ValidateSaveChunkDirectory */
    Result<ValidatedSaveChunkDirectory> ValidateSaveChunkDirectory(SaveChunkDirectory directory, const SaveGameManifest &manifest,
                                                                   const SaveChunkDirectoryLimits &limits) {
        if (!HasValidLimits(limits) || directory.payloadByteLength > limits.maximumPayloadBytes ||
            directory.payloadByteLength > std::numeric_limits<std::size_t>::max() || directory.entries.size() > limits.maximumEntries)
            return Result<ValidatedSaveChunkDirectory>::Failure(MakeError(SaveErrors::ArchiveFramingLimitExceeded));
        if (directory.entries.empty())
            return Result<ValidatedSaveChunkDirectory>::Failure(MakeError(SaveErrors::ArchiveDirectoryInvalid));

        if (directory.entries.size() != CountManifestChunks(manifest))
            return Result<ValidatedSaveChunkDirectory>::Failure(MakeError(SaveErrors::ArchiveDirectoryInvalid));
        if (!HasValidEntries(directory, manifest, limits))
            return Result<ValidatedSaveChunkDirectory>::Failure(MakeError(SaveErrors::ArchiveDirectoryInvalid));
        return Result<ValidatedSaveChunkDirectory>::Success(ValidatedSaveChunkDirectory{std::move(directory)});
    }

    /** @copydoc SelectSaveChunkPayload */
    Result<std::optional<std::span<const std::byte>>> SelectSaveChunkPayload(const std::span<const std::byte> payload,
                                                                             const ValidatedSaveChunkDirectory &directory,
                                                                             const SaveRecordId record) {
        if (payload.size() != directory.PayloadByteLength())
            return Result<std::optional<std::span<const std::byte>>>::Failure(MakeError(SaveErrors::ArchivePayloadTruncated));
        const std::span entries = directory.Entries();
        const auto found = std::ranges::lower_bound(entries, record, {}, &SaveChunkDirectoryEntry::record);
        if (found == entries.end() || found->record != record)
            return Result<std::optional<std::span<const std::byte>>>::Success(std::nullopt);
        const auto bytes = payload.subspan(static_cast<std::size_t>(found->offset), static_cast<std::size_t>(found->storedByteLength));
        if (ComputeSha256(bytes) != found->decodedHash)
            return Result<std::optional<std::span<const std::byte>>>::Failure(MakeError(SaveErrors::ArchiveChunkHashMismatch));
        return Result<std::optional<std::span<const std::byte>>>::Success(bytes);
    }
}  // namespace Horo::Runtime
