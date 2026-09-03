#pragma once

/**
 * @file GameServiceRegistry.h
 * @brief Transactional project gameplay service registry and dependency order.
 */

#include "Horo/Gameplay/GameplayRegistration.h"

#include <span>
#include <string>
#include <vector>

namespace Horo::Gameplay {
    /** @brief Host-owned service registry frozen before module startup or scene activation. */
    class GameServiceRegistry final {
    public:
        /** @brief Creates an open registry restricted to one project module namespace. */
        explicit GameServiceRegistry(std::string moduleId);

        /**
         * @brief Copies one service descriptor and factory into the open transaction.
         * @param registration Complete inert metadata and exact-generation factory binding.
         * @return Success or a typed validation, duplicate, or lifecycle error.
         */
        [[nodiscard]] Result<void> Register(GameplayServiceRegistration registration);
        /**
         * @brief Validates dependencies and capabilities, computes provider-first order, and closes registration.
         * @param hostCapabilities Capabilities explicitly supplied by host composition.
         * @return Success or a typed missing dependency, ambiguity, scope, or cycle error.
         */
        [[nodiscard]] Result<void> Freeze(std::span<const GameplayCapabilityId> hostCapabilities = {});
        /** @brief Reports whether registration is closed. */
        [[nodiscard]] bool IsFrozen() const noexcept;
        /** @brief Returns provider-before-dependant registrations with stable identity tie-breaking. */
        [[nodiscard]] std::span<const GameplayServiceRegistration> Registrations() const noexcept;
        /** @brief Returns every provided capability in deterministic identity order. */
        [[nodiscard]] std::span<const GameplayCapabilityId> ProvidedCapabilities() const noexcept;
        /** @brief Finds one service registration by stable identity. */
        [[nodiscard]] const GameplayServiceRegistration *Find(const GameplayServiceId &id) const noexcept;

    private:
        std::string moduleId_;
        std::vector<GameplayServiceRegistration> registrations_;
        std::vector<GameplayCapabilityId> providedCapabilities_;
        bool frozen_{false};
    };
}  // namespace Horo::Gameplay
