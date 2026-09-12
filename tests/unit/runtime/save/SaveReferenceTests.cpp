#include "Horo/Runtime/Save/SaveErrors.h"
#include "Horo/Runtime/Save/SaveReference.h"

#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <ranges>
#include <string_view>
#include <variant>
#include <vector>

namespace {
    using namespace Horo;
    using namespace Horo::Runtime;

    template <typename Identity> Identity Id(const std::uint8_t suffix) {
        std::array<std::uint8_t, 16> bytes{};
        bytes.back() = suffix;
        return Identity::FromBytes(bytes).Value();
    }

    void AppendIdentity(std::vector<std::byte> &bytes, const std::uint8_t suffix) {
        bytes.insert(bytes.end(), 15, std::byte{});
        bytes.push_back(static_cast<std::byte>(suffix));
    }

    void AppendParticipant(std::vector<std::byte> &bytes, const std::string_view participant) {
        const auto size = static_cast<std::uint32_t>(participant.size());
        bytes.push_back(static_cast<std::byte>(size));
        bytes.push_back(std::byte{});
        bytes.push_back(std::byte{});
        bytes.push_back(std::byte{});
        for (const char character : participant)
            bytes.push_back(static_cast<std::byte>(character));
    }

    std::vector<std::byte> EncodedBytes(const SaveReferenceTarget &target, const CanonicalCodecLimits limits = {}) {
        auto encoded = EncodeSaveReference(target, limits);
        REQUIRE(encoded.HasValue());
        return {encoded.Value().Bytes().begin(), encoded.Value().Bytes().end()};
    }

    template <typename Target> Target RoundTrip(Target target) {
        const auto bytes = EncodedBytes(SaveReferenceTarget{target});
        auto decoded = DecodeSaveReference(bytes);
        REQUIRE(decoded.HasValue());
        REQUIRE(std::holds_alternative<Target>(decoded.Value()));
        return std::get<Target>(std::move(decoded).Value());
    }

    TEST_CASE("Durable reference wire tags and payloads have exact golden bytes", "[runtime][save][reference]") {
        const auto participant = SaveParticipantId::Parse("project.gameplay.v1").Value();
        const std::array targets{SaveReferenceTarget{std::monostate{}},
                                 SaveReferenceTarget{SaveAssetReference{Id<SaveAssetId>(1)}},
                                 SaveReferenceTarget{SaveSceneReference{Id<SaveWorldId>(2), Id<SaveBaseSceneId>(3)}},
                                 SaveReferenceTarget{SaveEntityReference{Id<SaveWorldId>(4), Id<PersistentEntityId>(5)}},
                                 SaveReferenceTarget{SavePrefabProvenance{Id<SaveAssetId>(6), Id<SavePrefabInstanceId>(7)}},
                                 SaveReferenceTarget{SaveParticipantReference{participant}},
                                 SaveReferenceTarget{SaveRecordReference{participant, Id<SaveRecordId>(8)}}};

        std::array<std::vector<std::byte>, 7> expected;
        expected[0] = {std::byte{0x00}};
        expected[1] = {std::byte{0x01}};
        AppendIdentity(expected[1], 1);
        expected[2] = {std::byte{0x02}};
        AppendIdentity(expected[2], 2);
        AppendIdentity(expected[2], 3);
        expected[3] = {std::byte{0x03}};
        AppendIdentity(expected[3], 4);
        AppendIdentity(expected[3], 5);
        expected[4] = {std::byte{0x04}};
        AppendIdentity(expected[4], 6);
        AppendIdentity(expected[4], 7);
        expected[5] = {std::byte{0x05}};
        AppendParticipant(expected[5], "project.gameplay.v1");
        expected[6] = {std::byte{0x06}};
        AppendParticipant(expected[6], "project.gameplay.v1");
        AppendIdentity(expected[6], 8);

        for (std::size_t index = 0; index < targets.size(); ++index)
            CHECK(std::ranges::equal(EncodedBytes(targets[index]), expected[index]));
    }

    TEST_CASE("Every durable reference form round trips without recursive resolution", "[runtime][save][reference]") {
        CHECK(std::holds_alternative<std::monostate>(DecodeSaveReference(EncodedBytes(std::monostate{})).Value()));
        CHECK(RoundTrip(SaveAssetReference{Id<SaveAssetId>(1)}).asset == Id<SaveAssetId>(1));
        const auto scene = RoundTrip(SaveSceneReference{Id<SaveWorldId>(2), Id<SaveBaseSceneId>(3)});
        CHECK(scene.world == Id<SaveWorldId>(2));
        CHECK(scene.scene == Id<SaveBaseSceneId>(3));
        const auto entity = RoundTrip(SaveEntityReference{Id<SaveWorldId>(4), Id<PersistentEntityId>(5)});
        CHECK(entity.world == Id<SaveWorldId>(4));
        CHECK(entity.entity == Id<PersistentEntityId>(5));
        const auto prefab = RoundTrip(SavePrefabProvenance{Id<SaveAssetId>(6), Id<SavePrefabInstanceId>(7)});
        CHECK(prefab.prefab == Id<SaveAssetId>(6));
        CHECK(prefab.instance == Id<SavePrefabInstanceId>(7));
        const auto participant = SaveParticipantId::Parse("project.gameplay.v1").Value();
        CHECK(RoundTrip(SaveParticipantReference{participant}).participant == participant);
        const auto record = RoundTrip(SaveRecordReference{participant, Id<SaveRecordId>(8)});
        CHECK(record.participant == participant);
        CHECK(record.record == Id<SaveRecordId>(8));
    }

    TEST_CASE("Malformed truncated oversized and unknown reference wire is rejected", "[runtime][save][reference]") {
        std::vector<std::byte> invalidIdentity{std::byte{0x01}};
        invalidIdentity.insert(invalidIdentity.end(), 16, std::byte{});
        const auto malformed = DecodeSaveReference(invalidIdentity);
        REQUIRE(malformed.HasError());
        CHECK(malformed.ErrorValue().code.Value() == SaveErrors::ReferenceCorrupt.code.Value());
        REQUIRE(malformed.ErrorValue().diagnostics.size() == 1);
        CHECK(malformed.ErrorValue().diagnostics[0].location.column == 1);

        std::vector<std::byte> truncated{std::byte{0x01}};
        truncated.insert(truncated.end(), 15, std::byte{});
        const auto shortValue = DecodeSaveReference(truncated);
        REQUIRE(shortValue.HasError());
        CHECK(shortValue.ErrorValue().code.Value() == SaveErrors::CanonicalCodecCorrupt.code.Value());

        const std::array unknown{std::byte{0xff}};
        const auto unknownValue = DecodeSaveReference(unknown);
        REQUIRE(unknownValue.HasError());
        CHECK(unknownValue.ErrorValue().code.Value() == SaveErrors::ReferenceCorrupt.code.Value());
        CHECK(unknownValue.ErrorValue().diagnostics[0].location.column == 0);

        const std::array trailingNull{std::byte{0x00}, std::byte{0x00}};
        CHECK(DecodeSaveReference(trailingNull).HasError());

        CanonicalCodecLimits limits;
        limits.maximumStringBytes = 4;
        const std::array oversized{std::byte{0x05}, std::byte{0x05}, std::byte{},    std::byte{},    std::byte{},
                                   std::byte{'a'},  std::byte{'b'},  std::byte{'c'}, std::byte{'d'}, std::byte{'e'}};
        const auto oversizedValue = DecodeSaveReference(oversized, limits);
        REQUIRE(oversizedValue.HasError());
        CHECK(oversizedValue.ErrorValue().code.Value() == SaveErrors::CanonicalCodecLimitExceeded.code.Value());

        const std::array malformedParticipant{std::byte{0x05}, std::byte{0x02}, std::byte{},    std::byte{},
                                              std::byte{},     std::byte{0xc0}, std::byte{0x80}};
        const auto malformedText = DecodeSaveReference(malformedParticipant);
        REQUIRE(malformedText.HasError());
        CHECK(malformedText.ErrorValue().code.Value() == SaveErrors::ReferenceCorrupt.code.Value());
        CHECK(malformedText.ErrorValue().diagnostics[0].location.column == 1);
    }

    TEST_CASE("Pair decoders stop after the first invalid identity", "[runtime][save][reference]") {
        std::vector<std::byte> malformedScene{std::byte{0x02}};
        malformedScene.insert(malformedScene.end(), 16, std::byte{});
        const auto decoded = DecodeSaveReference(malformedScene);
        REQUIRE(decoded.HasError());
        CHECK(decoded.ErrorValue().code.Value() == SaveErrors::ReferenceCorrupt.code.Value());
        REQUIRE(decoded.ErrorValue().diagnostics.size() == 1);
        CHECK(decoded.ErrorValue().diagnostics[0].location.column == 1);

        std::vector<std::byte> invalidSecond{std::byte{0x02}};
        AppendIdentity(invalidSecond, 1);
        invalidSecond.insert(invalidSecond.end(), 16, std::byte{});
        const auto second = DecodeSaveReference(invalidSecond);
        REQUIRE(second.HasError());
        CHECK(second.ErrorValue().code.Value() == SaveErrors::ReferenceCorrupt.code.Value());
        REQUIRE(second.ErrorValue().diagnostics.size() == 1);
        CHECK(second.ErrorValue().diagnostics[0].location.column == 17);
    }

    TEST_CASE("Reference encoding preserves codec bounds and invalid caller classification", "[runtime][save][reference]") {
        CHECK(ValidateSaveReference(SaveAssetReference{}).HasError());
        CanonicalCodecLimits limits;
        limits.maximumBytes = 1;
        limits.maximumStringBytes = 1;
        const auto oversized = EncodeSaveReference(SaveAssetReference{Id<SaveAssetId>(1)}, limits);
        REQUIRE(oversized.HasError());
        CHECK(oversized.ErrorValue().code.Value() == SaveErrors::CanonicalCodecLimitExceeded.code.Value());
    }

    TEST_CASE("Reconciliation requires context and changed same-kind remaps", "[runtime][save][reference]") {
        const SaveReferenceTarget original = SaveEntityReference{Id<SaveWorldId>(1), Id<PersistentEntityId>(2)};
        const SaveReferenceTarget replacement = SaveEntityReference{Id<SaveWorldId>(1), Id<PersistentEntityId>(3)};
        const SaveReferenceTarget otherKind = SaveAssetReference{Id<SaveAssetId>(3)};
        CHECK(ValidateSaveReferenceResolution({original, SaveReferenceDisposition::Resolved, std::nullopt}).HasValue());
        CHECK(ValidateSaveReferenceResolution({original, SaveReferenceDisposition::Missing, std::nullopt}).HasValue());
        CHECK(ValidateSaveReferenceResolution({original, SaveReferenceDisposition::Deferred, std::nullopt}).HasValue());
        CHECK(ValidateSaveReferenceResolution({original, SaveReferenceDisposition::Remapped, replacement}).HasValue());
        CHECK(ValidateSaveReferenceResolution({std::monostate{}, SaveReferenceDisposition::Missing, std::nullopt}).HasError());
        CHECK(ValidateSaveReferenceResolution({std::monostate{}, SaveReferenceDisposition::Deferred, std::nullopt}).HasError());
        CHECK(ValidateSaveReferenceResolution({std::monostate{}, SaveReferenceDisposition::Remapped, replacement}).HasError());
        CHECK(ValidateSaveReferenceResolution({original, SaveReferenceDisposition::Remapped, original}).HasError());
        CHECK(ValidateSaveReferenceResolution({original, SaveReferenceDisposition::Remapped, otherKind}).HasError());
        CHECK(ValidateSaveReferenceResolution({original, SaveReferenceDisposition::Missing, replacement}).HasError());
        CHECK(ValidateSaveReferenceResolution({original, SaveReferenceDisposition::Remapped, std::nullopt}).HasError());
    }
}  // namespace
