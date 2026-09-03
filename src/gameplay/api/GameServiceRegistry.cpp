#include "Horo/Gameplay/GameServiceRegistry.h"

#include "GameServiceRegistryDetail.h"
#include "Horo/Gameplay/GameplayErrors.h"

#include <algorithm>

namespace Horo::Gameplay {
    /** @copydoc GameServiceRegistry::GameServiceRegistry */
    GameServiceRegistry::GameServiceRegistry(std::string moduleId) : moduleId_(std::move(moduleId)) {}

    /** @copydoc GameServiceRegistry::Register */
    Result<void> GameServiceRegistry::Register(GameplayServiceRegistration registration) {
        if (frozen_)
            return Result<void>::Failure(MakeError(GameplayErrors::RegistrationRegistryFrozen));
        if (registrations_.size() >= MaximumGameplayServices)
            return Result<void>::Failure(MakeError(GameplayErrors::InvalidServiceDescriptor));
        if (const Result<void> valid = Detail::ValidateServiceRegistration(registration, moduleId_); valid.HasError())
            return valid;
        if (Find(registration.descriptor.id) != nullptr)
            return Result<void>::Failure(MakeError(GameplayErrors::DuplicateService));
        registrations_.push_back(std::move(registration));
        return Result<void>::Success();
    }

    /** @copydoc GameServiceRegistry::Freeze */
    Result<void> GameServiceRegistry::Freeze(const std::span<const GameplayCapabilityId> hostCapabilities) {
        if (frozen_)
            return Result<void>::Success();
        if (const Result<void> valid = Detail::ValidateServiceGraph(registrations_, hostCapabilities); valid.HasError())
            return valid;
        auto ordered = Detail::BuildServiceOrder(registrations_, hostCapabilities);
        if (ordered.HasError())
            return Result<void>::Failure(ordered.ErrorValue());
        std::vector<GameplayServiceRegistration> sorted;
        sorted.reserve(registrations_.size());
        for (const std::size_t index : ordered.Value())
            sorted.push_back(std::move(registrations_[index]));
        registrations_ = std::move(sorted);
        providedCapabilities_ = Detail::CollectProvidedCapabilities(registrations_);
        frozen_ = true;
        return Result<void>::Success();
    }

    /** @copydoc GameServiceRegistry::IsFrozen */
    bool GameServiceRegistry::IsFrozen() const noexcept {
        return frozen_;
    }

    /** @copydoc GameServiceRegistry::Registrations */
    std::span<const GameplayServiceRegistration> GameServiceRegistry::Registrations() const noexcept {
        return registrations_;
    }

    /** @copydoc GameServiceRegistry::ProvidedCapabilities */
    std::span<const GameplayCapabilityId> GameServiceRegistry::ProvidedCapabilities() const noexcept {
        return providedCapabilities_;
    }

    /** @copydoc GameServiceRegistry::Find */
    const GameplayServiceRegistration *GameServiceRegistry::Find(const GameplayServiceId &id) const noexcept {
        const auto found = std::ranges::find(registrations_, id, [](const GameplayServiceRegistration &registration) {
            return registration.descriptor.id;
        });
        return found == registrations_.end() ? nullptr : std::to_address(found);
    }
}  // namespace Horo::Gameplay
