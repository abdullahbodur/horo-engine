#include "Horo/Gameplay/ComponentRegistry.h"
#include "Horo/Gameplay/GameplayErrors.h"

#include <catch2/catch_test_macros.hpp>

namespace {
    using namespace Horo;
    using namespace Horo::Gameplay;

    ComponentDescriptor Descriptor(const std::string_view typeId, const std::uint32_t schemaVersion = 1) {
        return {
            .typeId = ComponentTypeId::Parse(typeId).Value(),
            .schemaVersion = schemaVersion,
            .displayName = "Movement Settings",
            .category = "Gameplay/Movement",
            .properties = {{
                .id = ComponentPropertyId::Parse("speed").Value(),
                .displayName = "Speed",
                .kind = ComponentPropertyKind::Number,
                .required = true,
            }},
        };
    }

    SerializedComponent Payload(const ComponentTypeId &typeId, const std::uint32_t schemaVersion) {
        return {
            .typeId = typeId,
            .schemaVersion = schemaVersion,
            .encoding = ComponentPayloadEncoding::CanonicalJson,
            .payload = {std::byte{'{'}, std::byte{'}'}},
        };
    }
}  // namespace

TEST_CASE("component identities and descriptor metadata reject ambiguous registration") {
    REQUIRE(ComponentTypeId::Parse("game.tests.movement_settings").HasValue());
    REQUIRE(ComponentTypeId::Parse("MovementSettings").HasError());
    REQUIRE(ComponentTypeId::Parse("game.tests.mövëment").HasError());
    REQUIRE(ComponentPropertyId::Parse("move_speed").HasValue());
    REQUIRE(ComponentPropertyId::Parse("MoveSpeed").HasError());

    ComponentRegistry registry;
    ComponentDescriptor descriptor = Descriptor("game.tests.movement_settings");
    REQUIRE(registry.Register(descriptor).HasValue());
    const auto duplicate = registry.Register(std::move(descriptor));
    REQUIRE(duplicate.HasError());
    REQUIRE(duplicate.ErrorValue().code.Value() == GameplayErrors::DuplicateComponentType.code.Value());

    ComponentDescriptor invalid = Descriptor("game.tests.invalid_properties");
    invalid.properties.push_back(invalid.properties.front());
    REQUIRE(registry.Register(std::move(invalid)).HasError());

    REQUIRE(registry.Freeze().HasValue());
    REQUIRE(registry.IsFrozen());
    REQUIRE(registry.Descriptors().size() == 1);
    REQUIRE(registry.Register(Descriptor("game.tests.late")).ErrorValue().code.Value() ==
            GameplayErrors::ComponentRegistryFrozen.code.Value());
}

TEST_CASE("component inspection distinguishes current future and migratable schemas") {
    ComponentDescriptor descriptor = Descriptor("game.tests.movement_settings", 3);
    descriptor.migrations = {{.fromSchemaVersion = 1, .toSchemaVersion = 2}, {.fromSchemaVersion = 2, .toSchemaVersion = 3}};
    const ComponentTypeId typeId = descriptor.typeId;

    ComponentRegistry registry;
    REQUIRE(registry.Register(std::move(descriptor)).HasValue());
    REQUIRE(registry.Freeze().HasValue());

    const auto current = registry.Inspect(Payload(typeId, 3));
    REQUIRE(current.HasValue());
    REQUIRE(current.Value().status == ComponentInspectionStatus::Current);
    REQUIRE(current.Value().descriptor->properties.front().id.Value() == "speed");

    const auto migration = registry.Inspect(Payload(typeId, 1));
    REQUIRE(migration.HasValue());
    REQUIRE(migration.Value().status == ComponentInspectionStatus::MigrationRequired);
    REQUIRE(migration.Value().migrationPath.size() == 2);
    REQUIRE((migration.Value().migrationPath[0] == ComponentMigrationDescriptor{1, 2}));
    REQUIRE((migration.Value().migrationPath[1] == ComponentMigrationDescriptor{2, 3}));

    const auto future = registry.Inspect(Payload(typeId, 4));
    REQUIRE(future.HasValue());
    REQUIRE(future.Value().status == ComponentInspectionStatus::NewerSchema);
}

TEST_CASE("missing and replaced component code never mutates persistent payload bytes") {
    const ComponentTypeId typeId = ComponentTypeId::Parse("game.tests.removed_component").Value();
    SerializedComponent payload = Payload(typeId, 1);
    payload.payload = {std::byte{0x00}, std::byte{0x7f}, std::byte{0xff}};
    const SerializedComponent original = payload;

    ComponentRegistry missingRegistry;
    REQUIRE(missingRegistry.Freeze().HasValue());
    const auto missing = missingRegistry.Inspect(payload);
    REQUIRE(missing.HasValue());
    REQUIRE(missing.Value().status == ComponentInspectionStatus::MissingDescriptor);
    REQUIRE(payload == original);

    ComponentRegistry replacementRegistry;
    REQUIRE(replacementRegistry.Register(Descriptor(typeId.Value())).HasValue());
    REQUIRE(replacementRegistry.Freeze().HasValue());
    const auto restored = replacementRegistry.Inspect(payload);
    REQUIRE(restored.HasValue());
    REQUIRE(restored.Value().status == ComponentInspectionStatus::Current);
    REQUIRE(restored.Value().descriptor->typeId == typeId);
    REQUIRE(payload == original);
}

TEST_CASE("component migrations are forward-only deterministic metadata") {
    ComponentRegistry registry;
    ComponentDescriptor ambiguous = Descriptor("game.tests.ambiguous", 3);
    ambiguous.migrations = {{.fromSchemaVersion = 1, .toSchemaVersion = 2}, {.fromSchemaVersion = 1, .toSchemaVersion = 3}};
    const auto duplicateSource = registry.Register(std::move(ambiguous));
    REQUIRE(duplicateSource.HasError());
    REQUIRE(duplicateSource.ErrorValue().code.Value() == GameplayErrors::InvalidComponentMigration.code.Value());

    ComponentDescriptor backward = Descriptor("game.tests.backward", 3);
    backward.migrations = {{.fromSchemaVersion = 2, .toSchemaVersion = 1}};
    REQUIRE(registry.Register(std::move(backward)).HasError());

    ComponentDescriptor incomplete = Descriptor("game.tests.incomplete", 3);
    incomplete.migrations = {{.fromSchemaVersion = 1, .toSchemaVersion = 2}};
    const ComponentTypeId typeId = incomplete.typeId;
    REQUIRE(registry.Register(std::move(incomplete)).HasValue());
    REQUIRE(registry.Freeze().HasValue());
    const auto inspection = registry.Inspect(Payload(typeId, 1));
    REQUIRE(inspection.HasValue());
    REQUIRE(inspection.Value().status == ComponentInspectionStatus::UnsupportedOlderSchema);
    REQUIRE(inspection.Value().migrationPath.empty());
}
