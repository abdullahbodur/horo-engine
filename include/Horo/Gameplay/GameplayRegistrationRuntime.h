#pragma once

/**
 * @file GameplayRegistrationRuntime.h
 * @brief Cancellation-aware owned runtime instances for registered gameplay services and systems.
 */

#include "Horo/Gameplay/GameServiceRegistry.h"
#include "Horo/Gameplay/SystemRegistry.h"

#include <memory>
#include <span>

namespace Horo::Gameplay {
    /** @brief Explicit inherited dependencies supplied when activating one service scope. */
    struct GameplayServiceRuntimeDependencies {
        std::span<const GameplayServiceId> activeServices;
        std::span<const GameplayCapabilityId> capabilities;
    };

    /** @brief Owns one scope of module service instances in provider-first lifecycle order. */
    class GameplayServiceRuntime final {
    public:
        /**
         * @brief Creates and starts one service scope with cooperative child cancellation.
         * @param registry Frozen registry that outlives this runtime and its module factories.
         * @param scope Service scope to instantiate.
         * @param dependencies Already-active wider-scope services and capabilities.
         * @param parentCancellation Optional parent generation cancellation.
         * @param thread Actual execution domain used for service lifecycle callbacks.
         * @return Active runtime or a typed error after complete reverse-order rollback.
         */
        [[nodiscard]] static Result<std::unique_ptr<GameplayServiceRuntime>> Create(
            const GameServiceRegistry &registry, GameplayServiceScope scope, const GameplayServiceRuntimeDependencies &dependencies = {},
            CancellationToken parentCancellation = {}, GameplayThreadAffinity thread = GameplayThreadAffinity::RuntimeOwner);
        ~GameplayServiceRuntime();
        GameplayServiceRuntime(const GameplayServiceRuntime &) = delete;
        GameplayServiceRuntime &operator=(const GameplayServiceRuntime &) = delete;

        /** @brief Requests cancellation without releasing service instances. */
        void RequestCancellation() const noexcept;
        /** @brief Stops and destroys active services in reverse dependency order. */
        void Shutdown() noexcept;
        /** @brief Returns the generation token observed by service callbacks. */
        [[nodiscard]] CancellationToken Cancellation() const noexcept;
        /** @brief Returns inherited and scope-owned active services in activation order. */
        [[nodiscard]] std::span<const GameplayServiceId> ActiveServices() const noexcept;
        /** @brief Returns inherited and scope-owned capabilities in deterministic activation order. */
        [[nodiscard]] std::span<const GameplayCapabilityId> Capabilities() const noexcept;
        /** @brief Reports the number of constructed services owned by this runtime. */
        [[nodiscard]] std::size_t InstanceCount() const noexcept;

    private:
        struct Impl;
        explicit GameplayServiceRuntime(std::unique_ptr<Impl> impl) noexcept;
        std::unique_ptr<Impl> impl_;
    };

    /** @brief Owns module system instances and dispatches their validated deterministic schedule. */
    class GameplaySystemRuntime final {
    public:
        /**
         * @brief Creates and starts every system in registry execution order.
         * @param registry Frozen registry that outlives this runtime and its module factories.
         * @param activeServices Services active for this scene generation.
         * @param capabilities Capabilities granted to the system runtime.
         * @param parentCancellation Optional parent scene or module cancellation.
         * @return Active runtime or a typed error after complete reverse-order rollback.
         */
        [[nodiscard]] static Result<std::unique_ptr<GameplaySystemRuntime>> Create(const SystemRegistry &registry,
                                                                                   std::span<const GameplayServiceId> activeServices,
                                                                                   std::span<const GameplayCapabilityId> capabilities,
                                                                                   CancellationToken parentCancellation = {});
        ~GameplaySystemRuntime();
        GameplaySystemRuntime(const GameplaySystemRuntime &) = delete;
        GameplaySystemRuntime &operator=(const GameplaySystemRuntime &) = delete;

        /**
         * @brief Executes every system assigned to one phase in validated order.
         * @param phase Runtime phase selected by the scene scheduler.
         * @param thread Actual execution domain used for this dispatch.
         * @param deltaSeconds Fixed or presentation delta owned by the selected phase.
         * @return Success or the first typed callback, cancellation, or affinity failure.
         */
        [[nodiscard]] Result<void> Execute(GameplaySystemPhase phase, GameplayThreadAffinity thread, double deltaSeconds);
        /** @brief Requests cancellation without releasing system instances. */
        void RequestCancellation() const noexcept;
        /** @brief Stops and destroys every system in reverse execution order. */
        void Shutdown() noexcept;
        /** @brief Returns the child cancellation token observed by system callbacks. */
        [[nodiscard]] CancellationToken Cancellation() const noexcept;
        /** @brief Reports the number of constructed system instances. */
        [[nodiscard]] std::size_t InstanceCount() const noexcept;

    private:
        struct Impl;
        explicit GameplaySystemRuntime(std::unique_ptr<Impl> impl) noexcept;
        std::unique_ptr<Impl> impl_;
    };
}  // namespace Horo::Gameplay
