#include "Horo/Gameplay/GameAssetTypeRegistry.h"
#include "Horo/Gameplay/GameplayErrors.h"
#include "gameplay/GameAssetTestSupport.h"

#include <catch2/catch_test_macros.hpp>
#include <stdexcept>
#include <utility>

namespace {
    using namespace Horo;
    using namespace Horo::Gameplay;

    GameAssetTypeId QuestType() {
        return GameAssetTypeId::Parse("game.tests.quest_definition").Value();
    }

    AssetCookTargetId Target(const std::string_view value) {
        return AssetCookTargetId::Parse(value).Value();
    }

    SerializedGameAsset Payload(const GameAssetTypeId &typeId, const std::uint32_t schemaVersion = 2) {
        return {
            .typeId = typeId,
            .schemaVersion = schemaVersion,
            .encoding = GameAssetPayloadEncoding::CanonicalJson,
            .payload = {std::byte{'{'}, std::byte{'}'}},
        };
    }

    Result<SerializedGameAsset> Import(void *userData, const GameAssetImportInput &input, const CancellationToken &) {
        ++*static_cast<int *>(userData);
        SerializedGameAsset asset = Payload(QuestType());
        asset.payload.assign(input.sourceBytes.begin(), input.sourceBytes.end());
        return Result<SerializedGameAsset>::Success(std::move(asset));
    }

    Result<SerializedGameAsset> Serialize(void *userData, const GameAssetSerializationInput &input, const CancellationToken &) {
        ++*static_cast<int *>(userData);
        SerializedGameAsset asset = Payload(QuestType());
        asset.encoding = input.encoding;
        asset.payload.assign(input.editorPayload.begin(), input.editorPayload.end());
        return Result<SerializedGameAsset>::Success(std::move(asset));
    }

    Result<std::vector<std::byte>> Cook(void *userData, const GameAssetCookInput &input, const CancellationToken &) {
        ++*static_cast<int *>(userData);
        return Result<std::vector<std::byte>>::Success(input.asset.payload);
    }

    Result<std::vector<std::byte>> CookEmpty(void *userData, const GameAssetCookInput &, const CancellationToken &) {
        ++*static_cast<int *>(userData);
        return Result<std::vector<std::byte>>::Success({});
    }

    Result<SerializedGameAsset> ImportWrongSchema(void *, const GameAssetImportInput &, const CancellationToken &) {
        return Result<SerializedGameAsset>::Success(Payload(QuestType(), 1));
    }

    Result<SerializedGameAsset> ImportWrongType(void *, const GameAssetImportInput &, const CancellationToken &) {
        return Result<SerializedGameAsset>::Success(Payload(GameAssetTypeId::Parse("game.tests.other_asset").Value()));
    }

    Result<SerializedGameAsset> ThrowAsset(void *, const GameAssetImportInput &, const CancellationToken &) {
        throw std::runtime_error{"project callback failure"};
    }

    Result<SerializedGameAsset> ThrowSerialization(void *, const GameAssetSerializationInput &, const CancellationToken &) {
        throw std::runtime_error{"project callback failure"};
    }

    Result<std::vector<std::byte>> ThrowCook(void *, const GameAssetCookInput &, const CancellationToken &) {
        throw std::runtime_error{"project callback failure"};
    }

    Result<std::vector<std::byte>> OversizedCook(void *, const GameAssetCookInput &, const CancellationToken &) {
        return Result<std::vector<std::byte>>::Success({std::byte{0x01}, std::byte{0x02}, std::byte{0x03}});
    }

    GameAssetTypeRegistration Registration(int &invocations) {
        return {
            .descriptor = Tests::QuestGameAssetDescriptor(2, {"quest", "json"}, {Target("headless-null"), Target("desktop-vulkan")}),
            .handler = {.userData = &invocations, .importAsset = &Import, .serializeAsset = &Serialize, .cookAsset = &Cook},
        };
    }
}  // namespace

TEST_CASE("game asset registration validates stable identities metadata and callback bindings") {
    CHECK(GameAssetTypeId::Parse("game.tests.quest_definition").HasValue());
    CHECK(GameAssetTypeId::Parse("core.quest").HasError());
    CHECK(GameAssetTypeId::Parse("game.tests.Quest").HasError());
    CHECK(GameAssetFieldId::Parse("quest_title").HasValue());
    CHECK(GameAssetFieldId::Parse("QuestTitle").HasError());

    int invocations = 0;
    GameAssetTypeRegistry registry{"game.tests"};
    CHECK(registry.Import(QuestType(), {{}, "quest"}, {}).HasError());
    REQUIRE(registry.Register(Registration(invocations)).HasValue());
    const auto duplicate = registry.Register(Registration(invocations));
    REQUIRE(duplicate.HasError());
    CHECK(duplicate.ErrorValue().code.Value() == GameplayErrors::DuplicateGameAssetType.code.Value());

    GameAssetTypeRegistration foreign = Registration(invocations);
    foreign.descriptor.typeId = GameAssetTypeId::Parse("game.other.quest_definition").Value();
    CHECK(registry.Register(std::move(foreign)).HasError());

    GameAssetTypeRegistration invalid = Registration(invocations);
    invalid.handler.cookAsset = nullptr;
    CHECK(registry.Register(std::move(invalid)).HasError());

    GameAssetTypeRegistration invalidTarget = Registration(invocations);
    invalidTarget.descriptor.cookTargets.front() = {};
    CHECK(registry.Register(std::move(invalidTarget)).HasError());

    GameAssetTypeRegistration duplicateTarget = Registration(invocations);
    duplicateTarget.descriptor.cookTargets.push_back(Target("headless-null"));
    CHECK(registry.Register(std::move(duplicateTarget)).HasError());

    GameAssetTypeRegistration maximumTarget = Registration(invocations);
    maximumTarget.descriptor.cookTargets = {Target("a-" + std::string(MaximumGameAssetCookTargetBytes - 2, 'a'))};
    GameAssetTypeRegistry maximumTargetRegistry{"game.tests"};
    CHECK(maximumTargetRegistry.Register(std::move(maximumTarget)).HasValue());

    GameAssetTypeRegistration oversizedTarget = Registration(invocations);
    oversizedTarget.descriptor.cookTargets = {Target("a-" + std::string(MaximumGameAssetCookTargetBytes - 1, 'a'))};
    GameAssetTypeRegistry oversizedTargetRegistry{"game.tests"};
    CHECK(oversizedTargetRegistry.Register(std::move(oversizedTarget)).HasError());

    REQUIRE(registry.Freeze().HasValue());
    CHECK(registry.IsFrozen());
    CHECK(registry.Registrations().size() == 1);
    CHECK(registry.Register(Registration(invocations)).ErrorValue().code.Value() == GameplayErrors::GameAssetRegistryFrozen.code.Value());
}

TEST_CASE("game asset processing invokes exact type callbacks and validates their output") {
    int invocations = 0;
    GameAssetTypeRegistry registry{"game.tests"};
    REQUIRE(registry.Register(Registration(invocations)).HasValue());
    REQUIRE(registry.Freeze().HasValue());
    const GameAssetTypeId typeId = QuestType();
    const std::vector<std::byte> bytes{std::byte{'{'}, std::byte{'1'}, std::byte{'}'}};
    const CancellationToken cancellation;

    const auto imported = registry.Import(typeId, {{bytes}, "quest"}, cancellation);
    REQUIRE(imported.HasValue());
    CHECK(imported.Value().payload == bytes);

    const auto serialized = registry.Serialize(typeId, {{bytes}, GameAssetPayloadEncoding::CanonicalJson}, cancellation);
    REQUIRE(serialized.HasValue());
    CHECK(serialized.Value().schemaVersion == 2);

    auto cooked = registry.Cook({serialized.Value(), Target("headless-null")}, cancellation);
    REQUIRE(cooked.HasValue());
    std::vector<std::byte> cookedBytes = std::move(cooked).Value();
    CHECK(cookedBytes == bytes);
    cookedBytes.front() = std::byte{0x7f};
    CHECK(serialized.Value().payload == bytes);
    CHECK(invocations == 3);

    CHECK(registry.Import(typeId, {{bytes}, "png"}, cancellation).HasError());
    CHECK(registry.Cook({serialized.Value(), Target("desktop-metal")}, cancellation).HasError());
    CHECK(invocations == 3);

    int invalidInvocations = 0;
    GameAssetTypeRegistry invalidOutput{"game.tests"};
    GameAssetTypeRegistration invalidRegistration = Registration(invalidInvocations);
    invalidRegistration.handler.importAsset = &ImportWrongSchema;
    REQUIRE(invalidOutput.Register(std::move(invalidRegistration)).HasValue());
    REQUIRE(invalidOutput.Freeze().HasValue());
    const auto rejectedOutput = invalidOutput.Import(typeId, {{bytes}, "quest"}, cancellation);
    REQUIRE(rejectedOutput.HasError());
    CHECK(rejectedOutput.ErrorValue().code.Value() == GameplayErrors::GameAssetProcessingFailed.code.Value());

    int wrongTypeInvocations = 0;
    GameAssetTypeRegistry wrongTypeOutput{"game.tests"};
    GameAssetTypeRegistration wrongTypeRegistration = Registration(wrongTypeInvocations);
    wrongTypeRegistration.handler.importAsset = &ImportWrongType;
    REQUIRE(wrongTypeOutput.Register(std::move(wrongTypeRegistration)).HasValue());
    REQUIRE(wrongTypeOutput.Freeze().HasValue());
    CHECK(wrongTypeOutput.Import(typeId, {{bytes}, "quest"}, cancellation).ErrorValue().code.Value() ==
          GameplayErrors::GameAssetProcessingFailed.code.Value());
}

TEST_CASE("game asset processing contains callback exceptions and enforces host bounds") {
    int invocations = 0;
    GameAssetTypeRegistration throwing = Registration(invocations);
    throwing.handler.importAsset = &ThrowAsset;
    throwing.handler.serializeAsset = &ThrowSerialization;
    throwing.handler.cookAsset = &ThrowCook;
    GameAssetTypeRegistry registry{"game.tests", {.maximumInputBytes = 2, .maximumCookedBytes = 2}};
    REQUIRE(registry.Register(std::move(throwing)).HasValue());
    REQUIRE(registry.Freeze().HasValue());

    const std::vector<std::byte> small{std::byte{0x01}, std::byte{0x02}};
    const std::vector<std::byte> oversized{std::byte{0x01}, std::byte{0x02}, std::byte{0x03}};
    CHECK(registry.Import(QuestType(), {{small}, "quest"}, {}).ErrorValue().code.Value() ==
          GameplayErrors::GameAssetProcessingFailed.code.Value());
    CHECK(registry.Serialize(QuestType(), {{small}, GameAssetPayloadEncoding::Binary}, {}).ErrorValue().code.Value() ==
          GameplayErrors::GameAssetProcessingFailed.code.Value());
    SerializedGameAsset asset = Payload(QuestType());
    CHECK(registry.Cook({asset, Target("headless-null")}, {}).ErrorValue().code.Value() ==
          GameplayErrors::GameAssetProcessingFailed.code.Value());
    CHECK(registry.Import(QuestType(), {{oversized}, "quest"}, {}).ErrorValue().code.Value() ==
          GameplayErrors::InvalidGameAssetProcessingInput.code.Value());
    CHECK(registry.Serialize(QuestType(), {{oversized}, GameAssetPayloadEncoding::Binary}, {}).ErrorValue().code.Value() ==
          GameplayErrors::InvalidGameAssetProcessingInput.code.Value());

    int oversizedInvocations = 0;
    GameAssetTypeRegistration oversizedOutput = Registration(oversizedInvocations);
    oversizedOutput.handler.cookAsset = &OversizedCook;
    GameAssetTypeRegistry bounded{"game.tests", {.maximumCookedBytes = 2}};
    REQUIRE(bounded.Register(std::move(oversizedOutput)).HasValue());
    REQUIRE(bounded.Freeze().HasValue());
    CHECK(bounded.Cook({asset, Target("headless-null")}, {}).ErrorValue().code.Value() ==
          GameplayErrors::GameAssetProcessingFailed.code.Value());
}

TEST_CASE("missing and replaced game asset code preserves payload and exposes an editor fallback") {
    const GameAssetTypeId typeId = QuestType();
    SerializedGameAsset asset = Payload(typeId);
    asset.payload = {std::byte{0x00}, std::byte{0x7f}, std::byte{0xff}};
    const SerializedGameAsset original = asset;

    GameAssetTypeRegistry missing{"game.tests"};
    REQUIRE(missing.Freeze().HasValue());
    const auto missingInspection = missing.Inspect(asset);
    REQUIRE(missingInspection.HasValue());
    CHECK(missingInspection.Value().status == GameAssetInspectionStatus::MissingDescriptor);
    const auto fallback = missing.DescribeForEditor(asset);
    REQUIRE(fallback.HasValue());
    CHECK(fallback.Value().readOnly);
    CHECK(fallback.Value().displayName == typeId.Value());
    CHECK(fallback.Value().category.empty());
    CHECK(fallback.Value().fallback == GameAssetEditorFallback::MissingDescriptor);
    CHECK(fallback.Value().payloadBytes == original.payload.size());
    CHECK(asset == original);
    CHECK(missing.Cook({asset, Target("headless-null")}, {}).ErrorValue().code.Value() ==
          GameplayErrors::GameAssetHandlerUnavailable.code.Value());

    int invocations = 0;
    GameAssetTypeRegistry replacement{"game.tests"};
    REQUIRE(replacement.Register(Registration(invocations)).HasValue());
    REQUIRE(replacement.Freeze().HasValue());
    const auto restored = replacement.DescribeForEditor(asset);
    REQUIRE(restored.HasValue());
    CHECK(restored.Value().status == GameAssetInspectionStatus::Current);
    CHECK_FALSE(restored.Value().readOnly);
    CHECK(restored.Value().displayName == "Quest Definition");
    CHECK(restored.Value().fields.size() == 1);
    CHECK(asset == original);
}

TEST_CASE("game asset inspection and editor projections own their descriptor data") {
    GameAssetInspection inspection;
    GameAssetEditorModel model;
    {
        int invocations = 0;
        GameAssetTypeRegistry registry{"game.tests"};
        REQUIRE(registry.Register(Registration(invocations)).HasValue());
        REQUIRE(registry.Freeze().HasValue());
        inspection = registry.Inspect(Payload(QuestType())).Value();
        model = registry.DescribeForEditor(Payload(QuestType())).Value();
    }

    REQUIRE(inspection.descriptor.has_value());
    CHECK(inspection.descriptor->editor.displayName == "Quest Definition");
    REQUIRE(model.fields.size() == 1);
    CHECK(model.fields.front().displayName == "Title");
    CHECK(model.fallback == GameAssetEditorFallback::None);
}

TEST_CASE("game asset validation rejects malformed envelopes without changing authored input") {
    int invocations = 0;
    GameAssetTypeRegistry registry{"game.tests"};
    REQUIRE(registry.Register(Registration(invocations)).HasValue());
    REQUIRE(registry.Freeze().HasValue());

    SerializedGameAsset malformed = Payload(QuestType());
    malformed.schemaVersion = 0;
    const SerializedGameAsset original = malformed;
    CHECK(registry.Inspect(malformed).HasError());
    CHECK(registry.DescribeForEditor(malformed).HasError());
    CHECK(registry.Cook({malformed, Target("headless-null")}, {}).HasError());
    CHECK(malformed == original);
    CHECK(invocations == 0);
}

TEST_CASE("game asset envelope payload bound is enforced before cook callbacks") {
    int invocations = 0;
    GameAssetTypeRegistration registration = Registration(invocations);
    registration.handler.cookAsset = &CookEmpty;
    GameAssetTypeRegistry registry{"game.tests"};
    REQUIRE(registry.Register(std::move(registration)).HasValue());
    REQUIRE(registry.Freeze().HasValue());

    SerializedGameAsset asset = Payload(QuestType());
    asset.payload.resize(MaximumGameAssetPayloadBytes);
    CHECK(ValidateSerializedGameAsset(asset).HasValue());
    CHECK(registry.Inspect(asset).HasValue());
    CHECK(registry.Cook({asset, Target("headless-null")}, {}).HasValue());
    CHECK(invocations == 1);

    asset.payload.push_back(std::byte{0x00});
    CHECK(ValidateSerializedGameAsset(asset).HasError());
    CHECK(registry.Inspect(asset).HasError());
    CHECK(registry.Cook({asset, Target("headless-null")}, {}).HasError());
    CHECK(invocations == 1);
}

TEST_CASE("game asset inspection reports schema skew without invoking project code") {
    int invocations = 0;
    GameAssetTypeRegistry registry{"game.tests"};
    REQUIRE(registry.Register(Registration(invocations)).HasValue());
    REQUIRE(registry.Freeze().HasValue());

    CHECK(registry.Inspect(Payload(QuestType(), 1)).Value().status == GameAssetInspectionStatus::OlderSchema);
    CHECK(registry.Inspect(Payload(QuestType(), 3)).Value().status == GameAssetInspectionStatus::NewerSchema);
    CHECK(invocations == 0);
}
