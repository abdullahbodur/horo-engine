#include "Horo/Gameplay/ComponentRegistry.h"

#include "Horo/Gameplay/GameplayErrors.h"

#include <algorithm>
#include <unordered_set>

namespace Horo::Gameplay {
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

        [[nodiscard]] Result<void> ValidateDescriptor(const ComponentDescriptor &descriptor) {
            if (!descriptor.typeId.IsValid() || descriptor.schemaVersion == 0 || descriptor.displayName.empty())
                return Result<void>::Failure(MakeError(GameplayErrors::InvalidComponentDescriptor));
            if (const Result<void> properties = ValidateProperties(descriptor); properties.HasError())
                return properties;
            return ValidateMigrations(descriptor);
        }

        [[nodiscard]] const ComponentMigrationDescriptor *FindMigration(const ComponentDescriptor &descriptor,
                                                                        const std::uint32_t sourceVersion) noexcept {
            const auto found = std::ranges::find(descriptor.migrations, sourceVersion, &ComponentMigrationDescriptor::fromSchemaVersion);
            return found == descriptor.migrations.end() ? nullptr : std::to_address(found);
        }

        [[nodiscard]] ComponentInspection InspectOlderSchema(const SerializedComponent &component, const ComponentDescriptor &descriptor) {
            ComponentInspection inspection{.status = ComponentInspectionStatus::MigrationRequired, .descriptor = &descriptor};
            std::uint32_t version = component.schemaVersion;
            while (version < descriptor.schemaVersion) {
                const ComponentMigrationDescriptor *migration = FindMigration(descriptor, version);
                if (migration == nullptr) {
                    inspection.status = ComponentInspectionStatus::UnsupportedOlderSchema;
                    inspection.migrationPath.clear();
                    return inspection;
                }
                inspection.migrationPath.push_back(*migration);
                version = migration->toSchemaVersion;
            }
            if (version != descriptor.schemaVersion) {
                inspection.status = ComponentInspectionStatus::UnsupportedOlderSchema;
                inspection.migrationPath.clear();
            }
            return inspection;
        }
    }  // namespace

    /** @copydoc ComponentRegistry::Register */
    Result<void> ComponentRegistry::Register(ComponentDescriptor descriptor) {
        if (frozen_)
            return Result<void>::Failure(MakeError(GameplayErrors::ComponentRegistryFrozen));
        if (const Result<void> valid = ValidateDescriptor(descriptor); valid.HasError())
            return valid;
        if (Find(descriptor.typeId) != nullptr)
            return Result<void>::Failure(MakeError(GameplayErrors::DuplicateComponentType));
        descriptors_.push_back(std::move(descriptor));
        return Result<void>::Success();
    }

    /** @copydoc ComponentRegistry::Freeze */
    Result<void> ComponentRegistry::Freeze() {
        if (frozen_)
            return Result<void>::Success();
        std::ranges::sort(descriptors_, {}, [](const ComponentDescriptor &descriptor) {
            return descriptor.typeId.Value();
        });
        frozen_ = true;
        return Result<void>::Success();
    }

    /** @copydoc ComponentRegistry::IsFrozen */
    bool ComponentRegistry::IsFrozen() const noexcept {
        return frozen_;
    }

    /** @copydoc ComponentRegistry::Descriptors */
    std::span<const ComponentDescriptor> ComponentRegistry::Descriptors() const noexcept {
        return descriptors_;
    }

    /** @copydoc ComponentRegistry::Find */
    const ComponentDescriptor *ComponentRegistry::Find(const ComponentTypeId &typeId) const noexcept {
        const auto found = std::ranges::find(descriptors_, typeId, &ComponentDescriptor::typeId);
        return found == descriptors_.end() ? nullptr : std::to_address(found);
    }

    /** @copydoc ComponentRegistry::Inspect */
    Result<ComponentInspection> ComponentRegistry::Inspect(const SerializedComponent &component) const {
        if (const Result<void> valid = ValidateSerializedComponent(component); valid.HasError())
            return Result<ComponentInspection>::Failure(valid.ErrorValue());
        const ComponentDescriptor *descriptor = Find(component.typeId);
        if (descriptor == nullptr)
            return Result<ComponentInspection>::Success({.status = ComponentInspectionStatus::MissingDescriptor});
        if (component.schemaVersion == descriptor->schemaVersion)
            return Result<ComponentInspection>::Success({.status = ComponentInspectionStatus::Current, .descriptor = descriptor});
        if (component.schemaVersion > descriptor->schemaVersion)
            return Result<ComponentInspection>::Success({.status = ComponentInspectionStatus::NewerSchema, .descriptor = descriptor});
        return Result<ComponentInspection>::Success(InspectOlderSchema(component, *descriptor));
    }
}  // namespace Horo::Gameplay
