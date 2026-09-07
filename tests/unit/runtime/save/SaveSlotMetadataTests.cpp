#include "Horo/Runtime/Save/SaveErrors.h"
#include "Horo/Runtime/Save/SaveSlotMetadata.h"
#include "SaveTestUtils.h"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <string>
#include <vector>

namespace Horo::Runtime {
    namespace {
        using namespace Test;

        [[nodiscard]] Sha256Digest Digest(const std::uint8_t suffix) {
            Sha256Digest value{};
            value.bytes.back() = suffix;
            return value;
        }

        [[nodiscard]] SaveSlotPublicationMetadata Publication(const std::uint8_t generation = 2) {
            return {.slot = Id<SaveGameSlotId>(1),
                    .generation = Id<SlotGenerationId>(generation),
                    .kind = SaveSlotKind::Manual,
                    .savedAtUnixMilliseconds = 1'700'000'000'000ULL,
                    .playTimeNanoseconds = 42'000'000'000ULL,
                    .baseScene = Id<SaveBaseSceneId>(3),
                    .checkpoint = Id<SaveCheckpointId>(4),
                    .thumbnail = Id<SaveThumbnailId>(5),
                    .productCompatibility = V<ProductSaveCompatibilityVersion>(6),
                    .saveSchema = V<SaveSchemaVersion>(7),
                    .projectBuildId = "release-2026.09",
                    .canonicalState = {.value = Digest(8)},
                    .archiveContent = {.value = Digest(9)},
                    .cloudState = SaveSlotCloudState::LocalOnly};
        }

        TEST_CASE("Built-in save slot kinds and cloud summaries are stable valid catalog values", "[unit][save][slot-metadata]") {
            for (const auto kind : {SaveSlotKind::Manual, SaveSlotKind::Quick, SaveSlotKind::Auto, SaveSlotKind::Checkpoint,
                                    SaveSlotKind::Recovery, SaveSlotKind::System}) {
                for (const auto cloud : {SaveSlotCloudState::LocalOnly, SaveSlotCloudState::UploadPending, SaveSlotCloudState::Synchronized,
                                         SaveSlotCloudState::DownloadPending, SaveSlotCloudState::Conflict}) {
                    auto metadata = Publication();
                    metadata.kind = kind;
                    metadata.cloudState = cloud;
                    REQUIRE(ValidateSaveSlotPublicationMetadata(metadata).HasValue());
                }
            }

            auto unknownKind = Publication();
            unknownKind.kind = static_cast<SaveSlotKind>(255);
            REQUIRE(ValidateSaveSlotPublicationMetadata(unknownKind).ErrorValue().code.Value() ==
                    SaveErrors::SlotMetadataInvalid.code.Value());
            auto unknownCloud = Publication();
            unknownCloud.cloudState = static_cast<SaveSlotCloudState>(255);
            REQUIRE(ValidateSaveSlotPublicationMetadata(unknownCloud).ErrorValue().code.Value() ==
                    SaveErrors::SlotMetadataInvalid.code.Value());
        }

        TEST_CASE("Trusted slot metadata rejects malformed identities provenance and limits", "[unit][save][slot-metadata]") {
            auto metadata = Publication();
            metadata.slot = {};
            REQUIRE(ValidateSaveSlotPublicationMetadata(metadata).ErrorValue().code.Value() ==
                    SaveErrors::SlotMetadataInvalid.code.Value());

            metadata = Publication();
            metadata.checkpoint = SaveCheckpointId{};
            REQUIRE(ValidateSaveSlotPublicationMetadata(metadata).HasError());
            metadata = Publication();
            metadata.savedAtUnixMilliseconds = 0;
            REQUIRE(ValidateSaveSlotPublicationMetadata(metadata).HasError());
            metadata = Publication();
            metadata.projectBuildId = std::string{"\xC3\x28", 2};
            REQUIRE(ValidateSaveSlotPublicationMetadata(metadata).HasError());

            const SaveSlotMetadataLimits zeroBuildLimit{.maximumBuildIdBytes = 0};
            REQUIRE(ValidateSaveSlotPublicationMetadata(Publication(), zeroBuildLimit).ErrorValue().code.Value() ==
                    SaveErrors::SlotMetadataLimitExceeded.code.Value());
            const SaveSlotMetadataLimits shortBuildLimit{.maximumBuildIdBytes = 3};
            REQUIRE(ValidateSaveSlotPublicationMetadata(Publication(), shortBuildLimit).ErrorValue().code.Value() ==
                    SaveErrors::SlotMetadataLimitExceeded.code.Value());
        }

        TEST_CASE("Caller-owned display metadata is bounded UTF-8 and non-authoritative", "[unit][save][slot-metadata]") {
            const SaveSlotDisplayMetadata empty{};
            REQUIRE(ValidateSaveSlotDisplayMetadata(empty).HasValue());
            const SaveSlotDisplayMetadata unicode{.displayName = "Kayıt 🌍", .summary = "İkinci bölüm"};
            REQUIRE(ValidateSaveSlotDisplayMetadata(unicode).HasValue());

            const SaveSlotDisplayMetadata malformed{.displayName = std::string{"\xC3\x28", 2}, .summary = "ignored"};
            REQUIRE(ValidateSaveSlotDisplayMetadata(malformed).ErrorValue().code.Value() ==
                    SaveErrors::SlotDisplayMetadataInvalid.code.Value());
            REQUIRE(ValidateSaveSlotPublicationMetadata(Publication()).HasValue());

            const SaveSlotMetadataLimits oneByte{.maximumBuildIdBytes = 256, .maximumDisplayNameBytes = 1, .maximumDisplaySummaryBytes = 1};
            REQUIRE(ValidateSaveSlotDisplayMetadata({.displayName = "ab"}, oneByte).HasError());
            REQUIRE(ValidateSaveSlotDisplayMetadata({.summary = "ab"}, oneByte).HasError());
            REQUIRE(ValidateSaveSlotPublicationMetadata(Publication(), oneByte).HasValue());
        }

        TEST_CASE("Publication replacement advances generation independently from content hashes", "[unit][save][slot-metadata]") {
            const auto previous = Publication(2);
            auto replacement = Publication(3);
            REQUIRE(replacement.canonicalState == previous.canonicalState);
            REQUIRE(replacement.archiveContent == previous.archiveContent);
            REQUIRE(ValidateSaveSlotPublicationReplacement(previous, replacement).HasValue());

            replacement.generation = previous.generation;
            REQUIRE(ValidateSaveSlotPublicationReplacement(previous, replacement).ErrorValue().code.Value() ==
                    SaveErrors::SlotGenerationConflict.code.Value());
            replacement = Publication(3);
            replacement.slot = Id<SaveGameSlotId>(10);
            REQUIRE(ValidateSaveSlotPublicationReplacement(previous, replacement).ErrorValue().code.Value() ==
                    SaveErrors::SlotGenerationConflict.code.Value());
        }

        TEST_CASE("Display rename and UI ordering cannot mutate slot kind or generation", "[unit][save][slot-metadata]") {
            const auto trusted = Publication();
            auto other = Publication(10);
            other.slot = Id<SaveGameSlotId>(11);
            std::vector<SaveSlotCatalogEntry> entries{{.publication = trusted, .display = {.displayName = "Zulu"}},
                                                      {.publication = other, .display = {.displayName = "Alpha"}}};
            entries.front().display.displayName = "Renamed";
            std::ranges::sort(entries, {}, [](const SaveSlotCatalogEntry &entry) {
                return entry.display.displayName;
            });

            const auto found = std::ranges::find(entries, trusted.slot, [](const SaveSlotCatalogEntry &entry) {
                return entry.publication.slot;
            });
            REQUIRE(found != entries.end());
            REQUIRE(found->publication.kind == trusted.kind);
            REQUIRE(found->publication.generation == trusted.generation);
            REQUIRE(found->display.displayName == "Renamed");
        }
    }  // namespace
}  // namespace Horo::Runtime
