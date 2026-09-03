#include "Horo/Gameplay/ComponentRegistry.h"

#include "ComponentValidation.h"
#include "Horo/Gameplay/GameplayErrors.h"

#include <algorithm>

namespace Horo::Gameplay {
    namespace {
        [[nodiscard]] const ComponentMigrationDescriptor *FindMigration(const ComponentDescriptor &descriptor,
                                                                        const std::uint32_t sourceVersion) noexcept {
            const auto found = std::ranges::find(descriptor.migrations, sourceVersion, &ComponentMigrationDescriptor::fromSchemaVersion);
            return found == descriptor.migrations.end() ? nullptr : std::to_address(found);
        }

        [[nodiscard]] ComponentInspection InspectOlderSchema(const SerializedComponent &component, const ComponentDescriptor &descriptor) {
            using enum ComponentInspectionStatus;
            ComponentInspection inspection{.status = MigrationRequired, .descriptor = &descriptor};
            std::uint32_t version = component.schemaVersion;
            while (version < descriptor.schemaVersion) {
                const ComponentMigrationDescriptor *migration = FindMigration(descriptor, version);
                if (migration == nullptr) {
                    inspection.status = UnsupportedOlderSchema;
                    inspection.migrationPath.clear();
                    return inspection;
                }
                inspection.migrationPath.push_back(*migration);
                version = migration->toSchemaVersion;
            }
            if (version != descriptor.schemaVersion) {
                inspection.status = UnsupportedOlderSchema;
                inspection.migrationPath.clear();
            }
            return inspection;
        }
    }  // namespace

    /** @copydoc ComponentRegistry::Register */
    Result<void> ComponentRegistry::Register(ComponentDescriptor descriptor) {
        if (frozen_)
            return Result<void>::Failure(MakeError(GameplayErrors::ComponentRegistryFrozen));
        if (const Result<void> valid = Detail::ValidateComponentDescriptor(descriptor); valid.HasError())
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
        if (!frozen_) {
            const auto found = std::ranges::find(descriptors_, typeId, &ComponentDescriptor::typeId);
            return found == descriptors_.end() ? nullptr : std::to_address(found);
        }
        const auto found = std::ranges::lower_bound(descriptors_, typeId, {}, &ComponentDescriptor::typeId);
        if (found == descriptors_.end() || found->typeId != typeId)
            return nullptr;
        return std::to_address(found);
    }

    /** @copydoc ComponentRegistry::Inspect */
    Result<ComponentInspection> ComponentRegistry::Inspect(const SerializedComponent &component) const {
        using enum ComponentInspectionStatus;
        if (const Result<void> valid = ValidateSerializedComponent(component); valid.HasError())
            return Result<ComponentInspection>::Failure(valid.ErrorValue());
        const ComponentDescriptor *descriptor = Find(component.typeId);
        if (descriptor == nullptr)
            return Result<ComponentInspection>::Success({.status = MissingDescriptor});
        if (component.schemaVersion == descriptor->schemaVersion)
            return Result<ComponentInspection>::Success({.status = Current, .descriptor = descriptor});
        if (component.schemaVersion > descriptor->schemaVersion)
            return Result<ComponentInspection>::Success({.status = NewerSchema, .descriptor = descriptor});
        return Result<ComponentInspection>::Success(InspectOlderSchema(component, *descriptor));
    }
}  // namespace Horo::Gameplay
