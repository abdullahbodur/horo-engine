#pragma once

/**
 * @file BehaviorRegistry.h
 * @brief Transactional registry for complete generated behavior descriptor snapshots.
 */

#include "Horo/Gameplay/Behavior.h"

#include <memory>
#include <span>

namespace Horo::Gameplay {
    /** @brief One validated descriptor and its implementation factory. */
    struct BehaviorRegistration {
        BehaviorDescriptor descriptor;
        BehaviorFactoryBinding factory;
    };

    /** @brief Host-owned registry frozen before any runtime scene activates. */
    class BehaviorRegistry final {  // NOSONAR(cpp:S3624) Pimpl type with custom destructor and move operations
    public:
        BehaviorRegistry();
        ~BehaviorRegistry();
        BehaviorRegistry(const BehaviorRegistry &) = delete;
        BehaviorRegistry &operator=(const BehaviorRegistry &) = delete;
        BehaviorRegistry(BehaviorRegistry &&) noexcept;
        BehaviorRegistry &operator=(BehaviorRegistry &&) noexcept;

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
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}  // namespace Horo::Gameplay
