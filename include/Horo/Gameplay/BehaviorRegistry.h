#pragma once

/**
 * @file BehaviorRegistry.h
 * @brief Transactional registry for complete generated behavior descriptor snapshots.
 */

#include "Horo/Gameplay/Behavior.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace Horo::Gameplay::Detail {
    struct GenerationLeaseBinding;
}

namespace Horo::Gameplay {
    class BehaviorRuntime;

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
        friend class BehaviorRuntime;
        friend struct Detail::GenerationLeaseBinding;

        /** @brief Pins an owning native module generation for an external runtime, when applicable. */
        [[nodiscard]] Result<std::shared_ptr<void>> AcquireGenerationLease() const;

        std::vector<BehaviorRegistration> registrations_;
        std::weak_ptr<void> generationLease_;
        const std::atomic_bool *generationLeaseAdmission_{};
        bool frozen_{false};
    };
}  // namespace Horo::Gameplay
