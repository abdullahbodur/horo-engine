#pragma once

#include "Horo/Gameplay/GameplayRegistrationRuntime.h"

#include <array>
#include <vector>

namespace Horo::Gameplay {
    struct GameplayServiceRuntime::Impl {
        struct Instance {
            const GameplayServiceRegistration *registration{};
            IGameplayService *implementation{};
        };

        Impl(const GameServiceRegistry &registry, const GameplayServiceScope scope, const GameplayServiceRuntimeDependencies &dependencies,
             const CancellationToken parentCancellation, const GameplayThreadAffinity thread)
            : registry(registry), scope(scope), cancellation(parentCancellation),
              activeServices(dependencies.activeServices.begin(), dependencies.activeServices.end()),
              capabilities(dependencies.capabilities.begin(), dependencies.capabilities.end()), thread(thread) {}

        [[nodiscard]] Result<void> Build();
        void Rollback() noexcept;

        const GameServiceRegistry &registry;
        GameplayServiceScope scope;
        CancellationSource cancellation;
        std::vector<GameplayServiceId> activeServices;
        std::vector<GameplayCapabilityId> capabilities;
        GameplayThreadAffinity thread;
        std::vector<Instance> instances;
        bool shutdown{};
    };

    struct GameplaySystemRuntime::Impl {
        static constexpr std::size_t PhaseCount = static_cast<std::size_t>(GameplaySystemPhase::RenderExtraction) + 1;

        struct Instance {
            const GameplaySystemRegistration *registration{};
            IGameplaySystem *implementation{};
        };

        Impl(const SystemRegistry &registry, const std::span<const GameplayServiceId> availableServices,
             const std::span<const GameplayCapabilityId> availableCapabilities, const CancellationToken parentCancellation)
            : registry(registry), cancellation(parentCancellation), activeServices(availableServices.begin(), availableServices.end()),
              capabilities(availableCapabilities.begin(), availableCapabilities.end()) {}

        [[nodiscard]] Result<void> Build();
        void Rollback() noexcept;

        const SystemRegistry &registry;
        CancellationSource cancellation;
        std::vector<GameplayServiceId> activeServices;
        std::vector<GameplayCapabilityId> capabilities;
        std::vector<Instance> instances;
        std::array<std::vector<std::size_t>, PhaseCount> instancesByPhase;
        bool shutdown{};
    };
}  // namespace Horo::Gameplay
