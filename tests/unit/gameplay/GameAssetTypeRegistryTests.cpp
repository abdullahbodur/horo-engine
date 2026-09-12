#include "Horo/Gameplay/GameAssetTypeRegistry.h"
#include "Horo/Gameplay/GameplayErrors.h"

#include <catch2/catch_test_macros.hpp>
#include <utility>

namespace {
    using namespace Horo;
    using namespace Horo::Gameplay;

    GameAssetTypeId QuestType() {
        return GameAssetTypeId::Parse("game.tests.quest_definition").Value();
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

    Result<SerializedGameAsset> ImportWrongSchema(void *, const GameAssetImportInput &, const CancellationToken &) {
        return Result<SerializedGameAsset>::Success(Payload(QuestType(), 1));
    }

    GameAssetTypeRegistration Registration(int &invocations) {
        return {
            .descriptor =
                {
                    .typeId = QuestType(),
                    .schemaVersion = 2,
                    .sourceExtensions = {"quest", "json"},
                    .cookTargets = {"headless-null", "desktop-vulkan"},
                    .editor =
                        {
                            .displayName = "Quest Definition",
                            .category = "Gameplay/Quests",
                            .iconName = "asset-quest",
                            .fields = {{GameAssetFieldId::Parse("title").Value(), "Title", GameAssetFieldKind::String, true}},
                        },
                },
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

    const auto cooked = registry.Cook({serialized.Value(), "headless-null"}, cancellation);
    REQUIRE(cooked.HasValue());
    CHECK(cooked.Value() == bytes);
    CHECK(invocations == 3);

    CHECK(registry.Import(typeId, {{bytes}, "png"}, cancellation).HasError());
    CHECK(registry.Cook({serialized.Value(), "desktop-metal"}, cancellation).HasError());
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
    CHECK(fallback.Value().payloadBytes == original.payload.size());
    CHECK(asset == original);
    CHECK(missing.Cook({asset, "headless-null"}, {}).ErrorValue().code.Value() == GameplayErrors::GameAssetHandlerUnavailable.code.Value());

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

TEST_CASE("game asset inspection reports schema skew without invoking project code") {
    int invocations = 0;
    GameAssetTypeRegistry registry{"game.tests"};
    REQUIRE(registry.Register(Registration(invocations)).HasValue());
    REQUIRE(registry.Freeze().HasValue());

    CHECK(registry.Inspect(Payload(QuestType(), 1)).Value().status == GameAssetInspectionStatus::OlderSchema);
    CHECK(registry.Inspect(Payload(QuestType(), 3)).Value().status == GameAssetInspectionStatus::NewerSchema);
    CHECK(invocations == 0);
}
