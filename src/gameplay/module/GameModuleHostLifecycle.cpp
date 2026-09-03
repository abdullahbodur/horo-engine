#include "GameModuleHostDetail.h"
#include "Horo/Gameplay/GameplayErrors.h"

#include <algorithm>
#include <system_error>

namespace Horo::Gameplay {
    namespace {
        [[nodiscard]] std::vector<GameplayServiceId> ServiceIds(const GameServiceRegistry &registry) {
            std::vector<GameplayServiceId> ids;
            ids.reserve(registry.Registrations().size());
            for (const GameplayServiceRegistration &registration : registry.Registrations())
                ids.push_back(registration.descriptor.id);
            return ids;
        }

        [[nodiscard]] std::vector<GameplayCapabilityId> CombinedCapabilities(const std::span<const GameplayCapabilityId> hostCapabilities,
                                                                             const GameServiceRegistry &registry) {
            std::vector<GameplayCapabilityId> capabilities(hostCapabilities.begin(), hostCapabilities.end());
            capabilities.insert(capabilities.end(), registry.ProvidedCapabilities().begin(), registry.ProvidedCapabilities().end());
            std::ranges::sort(capabilities);
            return capabilities;
        }

        [[nodiscard]] Result<void> InvokeRegister(IGameModule &gameModule, GameRegistrationContext &context) noexcept {
            try {
                return gameModule.Register(context);
            } catch (...) {
                return Result<void>::Failure(
                    MakeError(GameplayErrors::GameplayFactoryFailed, "Gameplay module registration threw an exception."));
            }
        }

        [[nodiscard]] Result<void> InvokeStart(IGameModule &gameModule, GameRuntimeContext &context) noexcept {
            try {
                return gameModule.Start(context);
            } catch (...) {
                return Result<void>::Failure(
                    MakeError(GameplayErrors::GameplayFactoryFailed, "Gameplay module startup threw an exception."));
            }
        }
    }  // namespace

    LoadedGameModule::Impl::~Impl() {
        Shutdown();
    }

    Result<void> LoadedGameModule::Impl::RegisterAndStart(const std::span<const GameplayCapabilityId> hostCapabilities) {
        components = std::make_unique<ComponentRegistry>();
        services = std::make_unique<GameServiceRegistry>(moduleId);
        systems = std::make_unique<SystemRegistry>(moduleId);
        GameRegistrationContext registration{moduleId, *components, *systems, *services};
        if (Result<void> registered = InvokeRegister(*gameplayModule, registration); registered.HasError())
            return registered;
        if (Result<void> frozen = components->Freeze(); frozen.HasError())
            return frozen;
        if (Result<void> frozen = services->Freeze(hostCapabilities); frozen.HasError())
            return frozen;
        const std::vector<GameplayServiceId> serviceIds = ServiceIds(*services);
        const std::vector<GameplayCapabilityId> capabilities = CombinedCapabilities(hostCapabilities, *services);
        if (Result<void> frozen = systems->Freeze(serviceIds, capabilities); frozen.HasError())
            return frozen;

        auto activated = GameplayServiceRuntime::Create(*services, GameplayServiceScope::Project, {{}, hostCapabilities});
        if (activated.HasError())
            return Result<void>::Failure(activated.ErrorValue());
        projectServices = std::move(activated).Value();
        runtimeContext = {projectServices->Cancellation(), projectServices->ActiveServices(), projectServices->Capabilities()};
        startAttempted = true;
        return InvokeStart(*gameplayModule, runtimeContext);
    }

    void LoadedGameModule::Impl::Shutdown() noexcept {
        if (shutdown)
            return;
        shutdown = true;
        if (projectServices)
            projectServices->RequestCancellation();
        if (gameplayModule != nullptr && startAttempted)
            gameplayModule->Stop(runtimeContext);  // NOSONAR: exact-generation module boundary; path analysis is unrelated.
        projectServices.reset();
        if (gameplayModule != nullptr) {
            destroy(gameplayModule);
            gameplayModule = nullptr;
        }
        registry.reset();
        systems.reset();
        services.reset();
        components.reset();
        library.reset();
        if (removeArtifactOnUnload) {
            std::error_code ignored;
            std::filesystem::remove(loadedArtifactPath, ignored);  // NOSONAR: canonical host-created shadow artifact only.
        }
    }
}  // namespace Horo::Gameplay
