#pragma once

/**
 * @file ModuleHost.h
 * @brief Host-owned composition-time module registration, activation, and rollback.
 */

#include "Horo/Foundation/ModuleDescriptor.h"
#include "Horo/Foundation/Result.h"

#include <memory>
#include <vector>

namespace Horo {
    class ModuleActivationContext;

    /** @brief A module instance created by an activation callback and owned by the host. */
    class IModuleInstance {
    public:
        virtual ~IModuleInstance() = default;

        IModuleInstance(const IModuleInstance &) = delete;
        IModuleInstance &operator=(const IModuleInstance &) = delete;

    protected:
        IModuleInstance() = default;
        IModuleInstance(IModuleInstance &&) = default;
        IModuleInstance &operator=(IModuleInstance &&) = default;
    };

    /**
     * @brief Capabilities a composition root grants to one activating module.
     *
     * The context is constructed by the host for exactly one activation call. It is
     * not a service locator: the composition root decides which approved dependency
     * bindings each module receives, and modules never discover ambient state.
     */
    class ModuleActivationContext {
    public:
        /** @brief Opaque carrier for host-approved bindings; owned by the composition root. */
        using DependencyBindings = const void *;

        ModuleActivationContext(ModuleId module, DependencyBindings bindings) noexcept;
        ~ModuleActivationContext() = default;

        ModuleActivationContext(const ModuleActivationContext &) = delete;
        ModuleActivationContext &operator=(const ModuleActivationContext &) = delete;

        [[nodiscard]] const ModuleId &Module() const noexcept;

        /**
         * @brief Stores an instance the activated module owns until deactivation.
         * @param instance Non-null module-owned instance.
         * @return Failure when @p instance is null; success otherwise.
         */
        [[nodiscard]] Result<void> AttachInstance(std::unique_ptr<IModuleInstance> instance);

    private:
        friend class ModuleHost;

        ModuleId m_module;
        DependencyBindings m_bindings;
        std::unique_ptr<IModuleInstance> m_instance;
    };

    /** @brief One activated module: its identity, lifecycle entry points, and attached instance. */
    struct ActiveModule {
        ModuleId id;                                      /**< Activated module identity. */
        ModuleDeactivateCallback deactivate{nullptr};     /**< Matching deactivation entry point. */
        std::unique_ptr<ModuleActivationContext> context; /**< Activation context owning the attached instance. */
    };

    /**
     * @brief Registers inert descriptors explicitly at composition time, validates them,
     *        activates modules in deterministic order, and rolls back on failure.
     *
     * A failed activation deactivates every already-activated module in reverse order,
     * leaving no partially active set. Registration never invokes callbacks; only
     * ActivateRegistered does. Single-threaded by contract: use it from the composition
     * root before any worker threads start.
     */
    class ModuleHost {
    public:
        ModuleHost() = default;
        ~ModuleHost() = default;

        ModuleHost(const ModuleHost &) = delete;
        ModuleHost &operator=(const ModuleHost &) = delete;

        /**
         * @brief Registers one inert descriptor without invoking any callback.
         * @param descriptor Descriptor copied into the host's pending registration set.
         * @return Failure when identity or metadata is malformed locally, or already registered.
         */
        [[nodiscard]] Result<void> Register(const ModuleDescriptor &descriptor);

        /**
         * @brief Validates the full registration graph and activates modules in validated order.
         * @param bindings Host-approved dependency bindings handed to every activation callback.
         * @return Success with the activated count, or a typed validation/activation failure
         *         with all previously activated modules deactivated in reverse order.
         */
        [[nodiscard]] Result<std::size_t> ActivateRegistered(ModuleActivationContext::DependencyBindings bindings);

        /** @brief Deactivates every active module in reverse activation order and clears registrations. */
        void DeactivateAll() noexcept;

        /** @brief Returns whether any module is currently active. */
        [[nodiscard]] bool HasActiveModules() const noexcept;

    private:
        std::vector<ModuleDescriptor> m_registered;
        std::vector<ActiveModule> m_active;
    };
}  // namespace Horo
