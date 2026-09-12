#include "Horo/Runtime/Save/SaveSlotMetadata.h"

#include "Horo/Foundation/Utf8.h"
#include "Horo/Runtime/Save/SaveErrors.h"

namespace Horo::Runtime {
    namespace {
        /** @brief Reports whether a slot kind is one of the declared stable values. */
        [[nodiscard]] constexpr bool IsKnown(const SaveSlotKind kind) noexcept {
            using enum SaveSlotKind;
            switch (kind) {
                case Manual:
                case Quick:
                case Auto:
                case Checkpoint:
                case Recovery:
                case System:
                    return true;
            }
            return false;
        }

        /** @brief Reports whether a cloud summary is one of the declared stable values. */
        [[nodiscard]] constexpr bool IsKnown(const SaveSlotCloudState state) noexcept {
            using enum SaveSlotCloudState;
            switch (state) {
                case LocalOnly:
                case UploadPending:
                case Synchronized:
                case DownloadPending:
                case Conflict:
                    return true;
            }
            return false;
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
        if (metadata.projectBuildId.empty())
            return Result<void>::Failure(MakeError(SaveErrors::SlotMetadataInvalid));
        if (metadata.projectBuildId.size() > limits.maximumBuildIdBytes)
            return Result<void>::Failure(MakeError(SaveErrors::SlotMetadataLimitExceeded));
        if (!IsValidUtf8ScalarSequence(metadata.projectBuildId))
            return Result<void>::Failure(MakeError(SaveErrors::SlotMetadataInvalid));
        return Result<void>::Success();
    }

    /** @copydoc ValidateSaveSlotDisplayMetadata */
    Result<void> ValidateSaveSlotDisplayMetadata(const SaveSlotDisplayMetadata &metadata, const SaveSlotMetadataLimits &limits) {
        if (limits.maximumDisplayNameBytes == 0 || limits.maximumDisplaySummaryBytes == 0 ||
            metadata.displayName.size() > limits.maximumDisplayNameBytes || metadata.summary.size() > limits.maximumDisplaySummaryBytes)
            return Result<void>::Failure(MakeError(SaveErrors::SlotDisplayMetadataInvalid));
        if (!IsValidUtf8ScalarSequence(metadata.displayName) || !IsValidUtf8ScalarSequence(metadata.summary))
            return Result<void>::Failure(MakeError(SaveErrors::SlotDisplayMetadataInvalid));
        return Result<void>::Success();
    }

    /** @copydoc ValidateSaveSlotPublicationReplacement */
    Result<void> ValidateSaveSlotPublicationReplacement(const SaveSlotPublicationMetadata &previous,
                                                        const SaveSlotPublicationMetadata &replacement,
                                                        const SaveSlotMetadataLimits &limits) {
        if (auto previousValidation = ValidateSaveSlotPublicationMetadata(previous, limits); previousValidation.HasError())
            return previousValidation;
        if (auto replacementValidation = ValidateSaveSlotPublicationMetadata(replacement, limits); replacementValidation.HasError())
            return replacementValidation;
        if (previous.slot != replacement.slot || previous.generation == replacement.generation)
            return Result<void>::Failure(MakeError(SaveErrors::SlotGenerationConflict));
        return Result<void>::Success();
    }
}  // namespace Horo::Runtime
