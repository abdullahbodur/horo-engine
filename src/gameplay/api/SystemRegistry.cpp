#include "Horo/Gameplay/SystemRegistry.h"

#include "Horo/Gameplay/GameplayErrors.h"
#include "SystemRegistryDetail.h"

#include <algorithm>

namespace Horo::Gameplay {
    /** @copydoc SystemRegistry::SystemRegistry */
    SystemRegistry::SystemRegistry(std::string moduleId) : moduleId_(std::move(moduleId)) {}

    /** @copydoc SystemRegistry::Register */
    Result<void> SystemRegistry::Register(GameplaySystemRegistration registration) {
        if (frozen_)
            return Result<void>::Failure(MakeError(GameplayErrors::RegistrationRegistryFrozen));
        if (registrations_.size() >= MaximumGameplaySystems)
            return Result<void>::Failure(MakeError(GameplayErrors::InvalidSystemDescriptor));
        if (const Result<void> valid = Detail::ValidateSystemRegistration(registration, moduleId_); valid.HasError())
            return valid;
        if (Find(registration.descriptor.id) != nullptr)
            return Result<void>::Failure(MakeError(GameplayErrors::DuplicateSystem));
        registrations_.push_back(std::move(registration));
        return Result<void>::Success();
    }

    /** @copydoc SystemRegistry::Freeze */
    Result<void> SystemRegistry::Freeze(const std::span<const GameplayServiceId> availableServices,
                                        const std::span<const GameplayCapabilityId> availableCapabilities) {
        if (frozen_)
            return Result<void>::Success();
        auto edges = Detail::BuildSystemEdges(registrations_, availableServices, availableCapabilities);
        if (edges.HasError())
            return Result<void>::Failure(edges.ErrorValue());
        if (const Result<void> access = Detail::ValidateSystemAccess(registrations_, edges.Value()); access.HasError())
            return access;
        auto ordered = Detail::BuildSystemOrder(registrations_, edges.Value());
        if (ordered.HasError())
            return Result<void>::Failure(ordered.ErrorValue());
        std::vector<GameplaySystemRegistration> sorted;
        sorted.reserve(registrations_.size());
        for (const std::size_t index : ordered.Value())
            sorted.push_back(std::move(registrations_[index]));
        registrations_ = std::move(sorted);
        frozen_ = true;
        return Result<void>::Success();
    }

    /** @copydoc SystemRegistry::IsFrozen */
    bool SystemRegistry::IsFrozen() const noexcept {
        return frozen_;
    }

    /** @copydoc SystemRegistry::Registrations */
    std::span<const GameplaySystemRegistration> SystemRegistry::Registrations() const noexcept {
        return registrations_;
    }

    /** @copydoc SystemRegistry::Find */
    const GameplaySystemRegistration *SystemRegistry::Find(const GameplaySystemId &id) const noexcept {
        const auto found = std::ranges::find(registrations_, id, [](const GameplaySystemRegistration &registration) {
            return registration.descriptor.id;
        });
        return found == registrations_.end() ? nullptr : std::to_address(found);
    }
}  // namespace Horo::Gameplay
