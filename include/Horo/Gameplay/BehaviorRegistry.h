#pragma once

/**
 * @file BehaviorRegistry.h
 * @brief Transactional registry for complete generated behavior descriptor snapshots.
 */

#include "Horo/Gameplay/Behavior.h"

#include <cstdint>
#include <span>
#include <vector>

namespace Horo::Gameplay {
    /** @brief One validated descriptor and its implementation factory. */
    struct BehaviorRegistration {
        BehaviorDescriptor descriptor;
        BehaviorFactoryBinding factory;
    };

    /** @brief Host-owned registry frozen before any runtime scene activates. */
    class BehaviorRegistry final {
    public:
        BehaviorRegistry() = default;

        /** @brief Adds one descriptor before freeze, rejecting invalid or duplicate identities. */
        [[nodiscard]] Result<void> Register(BehaviorRegistration registration);
        /** @brief Validates the complete snapshot and prevents further registration. */
        [[nodiscard]] Result<void> Freeze();
        /** @brief Reports whether registration is closed. */
        [[nodiscard]] bool IsFrozen() const noexcept;
        /** @brief Returns registrations in deterministic type-ID order after freeze. */
        [[nodiscard]] std::span<const BehaviorRegistration> Registrations() const noexcept;
        /** @brief Finds one behavior registration by stable identity. */
        [[nodiscard]] const BehaviorRegistration *Find(const BehaviorTypeId &typeId) const noexcept;

    private:
        std::vector<BehaviorRegistration> registrations_;
        bool frozen_{false};
    };
}  // namespace Horo::Gameplay
