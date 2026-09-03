#pragma once

/**
 * @file SystemRegistry.h
 * @brief Transactional project gameplay system registry and deterministic schedule.
 */

#include "Horo/Gameplay/GameplayRegistration.h"

#include <span>
#include <string>
#include <vector>

namespace Horo::Gameplay {
    /** @brief Host-owned system registry frozen before any runtime scene activates. */
    class SystemRegistry final {
    public:
        /** @brief Creates an open registry restricted to one project module namespace. */
        explicit SystemRegistry(std::string moduleId);

        /**
         * @brief Copies one system descriptor and factory into the open transaction.
         * @param registration Complete scheduling metadata and exact-generation factory binding.
         * @return Success or a typed validation, duplicate, or lifecycle error.
         */
        [[nodiscard]] Result<void> Register(GameplaySystemRegistration registration);
        /**
         * @brief Validates service/capability requirements and produces a deterministic conflict-safe schedule.
         * @param availableServices Services present in the frozen registration transaction.
         * @param availableCapabilities Capabilities supplied by host composition or registered services.
         * @return Success or a typed dependency, access, lifecycle, or cycle error.
         */
        [[nodiscard]] Result<void> Freeze(std::span<const GameplayServiceId> availableServices,
                                          std::span<const GameplayCapabilityId> availableCapabilities);
        /** @brief Reports whether registration is closed. */
        [[nodiscard]] bool IsFrozen() const noexcept;
        /** @brief Returns registrations in deterministic execution order. */
        [[nodiscard]] std::span<const GameplaySystemRegistration> Registrations() const noexcept;
        /** @brief Finds one system registration by stable identity. */
        [[nodiscard]] const GameplaySystemRegistration *Find(const GameplaySystemId &id) const noexcept;

    private:
        std::string moduleId_;
        std::vector<GameplaySystemRegistration> registrations_;
        bool frozen_{false};
    };
}  // namespace Horo::Gameplay
