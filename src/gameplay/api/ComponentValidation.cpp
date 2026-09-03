#include "ComponentValidation.h"

#include "Horo/Gameplay/GameplayErrors.h"

#include <unordered_set>

namespace Horo::Gameplay::Detail {
    namespace {
        [[nodiscard]] Result<void> ValidateProperties(const ComponentDescriptor &descriptor) {
            if (descriptor.properties.size() > MaximumComponentProperties)
                return Result<void>::Failure(MakeError(GameplayErrors::InvalidComponentDescriptor));

            std::unordered_set<std::string_view> propertyIds;
            propertyIds.reserve(descriptor.properties.size());
            for (const ComponentPropertyDescriptor &property : descriptor.properties) {
                if (!property.id.IsValid() || property.displayName.empty() || !propertyIds.emplace(property.id.Value()).second)
                    return Result<void>::Failure(MakeError(GameplayErrors::InvalidComponentDescriptor));
            }
            return Result<void>::Success();
        }

        [[nodiscard]] Result<void> ValidateMigrations(const ComponentDescriptor &descriptor) {
            std::unordered_set<std::uint32_t> sourceVersions;
            sourceVersions.reserve(descriptor.migrations.size());
            for (const ComponentMigrationDescriptor &migration : descriptor.migrations) {
                if (migration.fromSchemaVersion == 0 || migration.fromSchemaVersion >= migration.toSchemaVersion ||
                    migration.toSchemaVersion > descriptor.schemaVersion || !sourceVersions.emplace(migration.fromSchemaVersion).second)
                    return Result<void>::Failure(MakeError(GameplayErrors::InvalidComponentMigration));
            }
            return Result<void>::Success();
        }
    }  // namespace

    Result<void> ValidateComponentDescriptor(const ComponentDescriptor &descriptor) {
        if (!descriptor.typeId.IsValid() || descriptor.schemaVersion == 0 || descriptor.displayName.empty())
            return Result<void>::Failure(MakeError(GameplayErrors::InvalidComponentDescriptor));
        if (const Result<void> properties = ValidateProperties(descriptor); properties.HasError())
            return properties;
        return ValidateMigrations(descriptor);
    }
}  // namespace Horo::Gameplay::Detail
