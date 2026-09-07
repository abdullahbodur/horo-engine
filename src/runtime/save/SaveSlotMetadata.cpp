#include "Horo/Runtime/Save/SaveSlotMetadata.h"

#include "Horo/Runtime/Save/SaveErrors.h"

#include <limits>
#include <string_view>
#include <utf8proc.h>

namespace Horo::Runtime {
    namespace {
        /** @brief Reports whether a slot kind is one of the declared stable values. */
        [[nodiscard]] constexpr bool IsKnown(const SaveSlotKind kind) noexcept {
            using enum SaveSlotKind;
            return kind == Manual || kind == Quick || kind == Auto || kind == Checkpoint || kind == Recovery || kind == System;
        }

        /** @brief Reports whether a cloud summary is one of the declared stable values. */
        [[nodiscard]] constexpr bool IsKnown(const SaveSlotCloudState state) noexcept {
            using enum SaveSlotCloudState;
            return state == LocalOnly || state == UploadPending || state == Synchronized || state == DownloadPending || state == Conflict;
        }

        /** @brief Validates UTF-8 without allocation after the caller has enforced its byte bound. */
        [[nodiscard]] bool IsValidUtf8(const std::string_view text) noexcept {
            if (text.size() > static_cast<std::size_t>(std::numeric_limits<utf8proc_ssize_t>::max()))
                return false;
            const auto *cursor = reinterpret_cast<const utf8proc_uint8_t *>(text.data());
            auto remaining = static_cast<utf8proc_ssize_t>(text.size());
            while (remaining > 0) {
                utf8proc_int32_t codepoint{};
                const auto decoded = utf8proc_iterate(cursor, remaining, &codepoint);
                if (decoded <= 0)
                    return false;
                cursor += decoded;
                remaining -= decoded;
            }
            return true;
        }

        /** @brief Reports whether optional catalog content identities are absent or usable. */
        [[nodiscard]] bool HasValidOptionalIdentities(const SaveSlotPublicationMetadata &metadata) noexcept {
            return (!metadata.checkpoint || metadata.checkpoint->IsValid()) && (!metadata.thumbnail || metadata.thumbnail->IsValid());
        }

        /** @brief Reports whether all required and optional publication identities are usable. */
        [[nodiscard]] bool HasValidPublicationIdentities(const SaveSlotPublicationMetadata &metadata) noexcept {
            return metadata.slot.IsValid() && metadata.generation.IsValid() && metadata.baseScene.IsValid() &&
                   HasValidOptionalIdentities(metadata);
        }

        /** @brief Reports whether typed publication categories, versions, and scalar provenance are usable. */
        [[nodiscard]] bool HasValidPublicationValues(const SaveSlotPublicationMetadata &metadata) noexcept {
            return metadata.productCompatibility.IsValid() && metadata.saveSchema.IsValid() && metadata.savedAtUnixMilliseconds != 0 &&
                   IsKnown(metadata.kind) && IsKnown(metadata.cloudState);
        }

    }  // namespace

    /** @copydoc ValidateSaveSlotPublicationMetadata */
    Result<void> ValidateSaveSlotPublicationMetadata(const SaveSlotPublicationMetadata &metadata, const SaveSlotMetadataLimits &limits) {
        if (limits.maximumBuildIdBytes == 0)
            return Result<void>::Failure(MakeError(SaveErrors::SlotMetadataLimitExceeded));
        if (!HasValidPublicationIdentities(metadata) || !HasValidPublicationValues(metadata))
            return Result<void>::Failure(MakeError(SaveErrors::SlotMetadataInvalid));
        if (metadata.projectBuildId.empty() || metadata.projectBuildId.size() > limits.maximumBuildIdBytes)
            return Result<void>::Failure(MakeError(SaveErrors::SlotMetadataLimitExceeded));
        if (!IsValidUtf8(metadata.projectBuildId))
            return Result<void>::Failure(MakeError(SaveErrors::SlotMetadataInvalid));
        return Result<void>::Success();
    }

    /** @copydoc ValidateSaveSlotDisplayMetadata */
    Result<void> ValidateSaveSlotDisplayMetadata(const SaveSlotDisplayMetadata &metadata, const SaveSlotMetadataLimits &limits) {
        if (limits.maximumDisplayNameBytes == 0 || limits.maximumDisplaySummaryBytes == 0 ||
            metadata.displayName.size() > limits.maximumDisplayNameBytes || metadata.summary.size() > limits.maximumDisplaySummaryBytes)
            return Result<void>::Failure(MakeError(SaveErrors::SlotDisplayMetadataInvalid));
        if (!IsValidUtf8(metadata.displayName) || !IsValidUtf8(metadata.summary))
            return Result<void>::Failure(MakeError(SaveErrors::SlotDisplayMetadataInvalid));
        return Result<void>::Success();
    }

    /** @copydoc ValidateSaveSlotPublicationReplacement */
    Result<void> ValidateSaveSlotPublicationReplacement(const SaveSlotPublicationMetadata &previous,
                                                        const SaveSlotPublicationMetadata &replacement,
                                                        const SaveSlotMetadataLimits &limits) {
        auto previousValidation = ValidateSaveSlotPublicationMetadata(previous, limits);
        if (previousValidation.HasError())
            return previousValidation;
        auto replacementValidation = ValidateSaveSlotPublicationMetadata(replacement, limits);
        if (replacementValidation.HasError())
            return replacementValidation;
        if (previous.slot != replacement.slot || previous.generation == replacement.generation)
            return Result<void>::Failure(MakeError(SaveErrors::SlotGenerationConflict));
        return Result<void>::Success();
    }
}  // namespace Horo::Runtime
