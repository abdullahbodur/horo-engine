#include "Horo/Runtime/Save/SaveArchiveFraming.h"
#include "SaveTestUtils.h"

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <utility>
#include <vector>

namespace {
    using namespace Horo;
    using namespace Horo::Runtime;
    using namespace Horo::Runtime::Test;

    std::vector<std::byte> Payload() {
        return {std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}, std::byte{5},
                std::byte{6}, std::byte{7}, std::byte{8}, std::byte{9}};
    }

    SaveGameManifest Manifest() {
        return {.saveSchemaVersion = V<SaveSchemaVersion>(1),
                .canonicalState = {ComputeSha256({})},
                .participants = {{.participant = SaveParticipantId::Parse("horo.scene.core.v1").Value(),
                                  .schemaVersion = V<ParticipantSchemaVersion>(1),
                                  .required = true,
                                  .chunks = {Id<SaveRecordId>(20), Id<SaveRecordId>(21)}},
                                 {.participant = SaveParticipantId::Parse("project.gameplay.v1").Value(),
                                  .schemaVersion = V<ParticipantSchemaVersion>(1),
                                  .required = false,
                                  .chunks = {Id<SaveRecordId>(22)}}}};
    }

    SaveChunkDirectory Directory(const std::span<const std::byte> payload) {
        const auto manifest = Manifest();
        const auto entry = [&](const std::size_t index, const std::uint64_t offset, const std::uint64_t length,
                               const SaveParticipantId owner) {
            return SaveChunkDirectoryEntry{.record = Id<SaveRecordId>(static_cast<std::uint8_t>(20 + index)),
                                           .owner = owner,
                                           .offset = offset,
                                           .storedByteLength = length,
                                           .decodedByteLength = length,
                                           .alignment = 1,
                                           .codec = SaveChunkCodec::Raw,
                                           .decodedHash = ComputeSha256(payload.subspan(offset, length))};
        };
        return {.payloadByteLength = payload.size(),
                .entries = {entry(0, 0, 3, manifest.participants[0].participant), entry(1, 3, 2, manifest.participants[0].participant),
                            entry(2, 5, 4, manifest.participants[1].participant)}};
    }

    TEST_CASE("Save chunk directory validates metadata without reading payload bytes", "[runtime][save][framing]") {
        const auto payload = Payload();
        CHECK(ValidateSaveChunkDirectory(Directory(payload), Manifest()).HasValue());
    }

    TEST_CASE("Selective chunk access verifies only the requested raw payload", "[runtime][save][framing]") {
        auto payload = Payload();
        const auto directory = ValidateSaveChunkDirectory(Directory(payload), Manifest()).Value();
        const auto selected = SelectSaveChunkPayload(payload, directory, Id<SaveRecordId>(21));
        REQUIRE(selected.HasValue());
        REQUIRE(selected.Value());
        CHECK(selected.Value()->size() == 2);
        CHECK(selected.Value()->front() == std::byte{4});

        const auto unknown = SelectSaveChunkPayload(payload, directory, Id<SaveRecordId>(99));
        REQUIRE(unknown.HasValue());
        CHECK_FALSE(unknown.Value());

        payload[3] = std::byte{99};
        CHECK(SelectSaveChunkPayload(payload, directory, Id<SaveRecordId>(21)).HasError());
    }

    TEST_CASE("Save chunk directory rejects gaps overlap truncation and trailing bytes", "[runtime][save][framing]") {
        const auto payload = Payload();
        auto directory = Directory(payload);
        directory.entries[1].offset = 2;
        CHECK(ValidateSaveChunkDirectory(directory, Manifest()).HasError());
        directory = Directory(payload);
        directory.entries[1].offset = 4;
        CHECK(ValidateSaveChunkDirectory(directory, Manifest()).HasError());
        directory = Directory(payload);
        directory.payloadByteLength = 8;
        CHECK(ValidateSaveChunkDirectory(directory, Manifest()).HasError());

        directory = Directory(payload);
        const auto validated = ValidateSaveChunkDirectory(std::move(directory), Manifest()).Value();
        CHECK(SelectSaveChunkPayload(std::span<const std::byte>{payload}.first(8), validated, Id<SaveRecordId>(20)).HasError());
        auto trailing = payload;
        trailing.push_back(std::byte{});
        CHECK(SelectSaveChunkPayload(trailing, validated, Id<SaveRecordId>(20)).HasError());
    }

    TEST_CASE("Save chunk directory rejects duplicate ordering ownership and manifest mismatch", "[runtime][save][framing]") {
        const auto payload = Payload();
        auto directory = Directory(payload);
        directory.entries[1].record = directory.entries[0].record;
        CHECK(ValidateSaveChunkDirectory(directory, Manifest()).HasError());
        directory = Directory(payload);
        directory.entries[1].owner = directory.entries[2].owner;
        CHECK(ValidateSaveChunkDirectory(directory, Manifest()).HasError());
        directory = Directory(payload);
        directory.entries.pop_back();
        directory.payloadByteLength = 5;
        CHECK(ValidateSaveChunkDirectory(directory, Manifest()).HasError());
    }

    TEST_CASE("Save chunk directory rejects unsafe scalar fields and explicit limit violations", "[runtime][save][framing]") {
        const auto payload = Payload();
        auto directory = Directory(payload);
        directory.entries[0].alignment = 3;
        CHECK(ValidateSaveChunkDirectory(directory, Manifest()).HasError());
        directory = Directory(payload);
        directory.entries[0].storedByteLength = std::numeric_limits<std::uint64_t>::max();
        CHECK(ValidateSaveChunkDirectory(directory, Manifest()).HasError());
        directory = Directory(payload);
        directory.entries[0].codec = static_cast<SaveChunkCodec>(99);
        CHECK(ValidateSaveChunkDirectory(directory, Manifest()).HasError());

        auto limits = SaveChunkDirectoryLimits{};
        limits.maximumEntries = 2;
        CHECK(ValidateSaveChunkDirectory(Directory(payload), Manifest(), limits).HasError());
        limits = {};
        limits.maximumDecodedChunkBytes = 2;
        CHECK(ValidateSaveChunkDirectory(Directory(payload), Manifest(), limits).HasError());
    }
}  // namespace
