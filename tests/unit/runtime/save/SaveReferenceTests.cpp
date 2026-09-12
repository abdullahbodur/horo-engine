#include "Horo/Runtime/Save/SaveReference.h"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
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

    template <typename Target> Target RoundTrip(Target target) {
        auto encoded = EncodeSaveReference(SaveReferenceTarget{target});
        REQUIRE(encoded.HasValue());
        auto decoded = DecodeSaveReference(encoded.Value());
        REQUIRE(decoded.HasValue());
        REQUIRE(std::holds_alternative<Target>(decoded.Value()));
        return std::get<Target>(std::move(decoded).Value());
    }

    TEST_CASE("Durable asset scene entity and prefab references survive byte round trips", "[runtime][save][reference]") {
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
    }

    TEST_CASE("Null participant and cross-record references decode without recursive resolution", "[runtime][save][reference]") {
        auto nullBytes = EncodeSaveReference(std::monostate{});
        REQUIRE(nullBytes.HasValue());
        CHECK(std::holds_alternative<std::monostate>(DecodeSaveReference(nullBytes.Value()).Value()));

        const auto participantId = SaveParticipantId::Parse("project.gameplay.v1").Value();
        CHECK(RoundTrip(SaveParticipantReference{participantId}).participant == participantId);
        const auto record = RoundTrip(SaveRecordReference{participantId, Id<SaveRecordId>(8)});
        CHECK(record.participant == participantId);
        CHECK(record.record == Id<SaveRecordId>(8));
    }

    TEST_CASE("Reference reconciliation retains original context for missing remapped and deferred targets", "[runtime][save][reference]") {
        const SaveReferenceTarget original = SaveEntityReference{Id<SaveWorldId>(1), Id<PersistentEntityId>(2)};
        CHECK(ValidateSaveReferenceResolution({original, SaveReferenceDisposition::Missing, std::nullopt}).HasValue());
        CHECK(ValidateSaveReferenceResolution({original, SaveReferenceDisposition::Deferred, std::nullopt}).HasValue());
        const SaveReferenceTarget replacement = SaveEntityReference{Id<SaveWorldId>(1), Id<PersistentEntityId>(3)};
        CHECK(ValidateSaveReferenceResolution({original, SaveReferenceDisposition::Remapped, replacement}).HasValue());
        CHECK(ValidateSaveReferenceResolution({original, SaveReferenceDisposition::Missing, replacement}).HasError());
        CHECK(ValidateSaveReferenceResolution({original, SaveReferenceDisposition::Remapped, std::nullopt}).HasError());
    }

    TEST_CASE("Reference codec rejects runtime-invalid identities unknown forms and trailing bytes", "[runtime][save][reference]") {
        CHECK(ValidateSaveReference(SaveAssetReference{}).HasError());
        const std::array unknown{std::byte{99}};
        CHECK(DecodeSaveReference(unknown).HasError());
        auto encoded = EncodeSaveReference(SaveAssetReference{Id<SaveAssetId>(1)}).Value();
        encoded.push_back(std::byte{});
        CHECK(DecodeSaveReference(encoded).HasError());
    }
}  // namespace
