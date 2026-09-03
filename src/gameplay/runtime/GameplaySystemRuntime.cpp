#include "GameplayRegistrationRuntimeDetail.h"
#include "GameplayRuntimeInvocation.h"
#include "GameplayRuntimeValidation.h"
#include "Horo/Gameplay/GameplayErrors.h"

namespace Horo::Gameplay {
    Result<void> GameplaySystemRuntime::Impl::Build() {
        if (!registry.IsFrozen())
            return Result<void>::Failure(MakeError(GameplayErrors::RegistrationRegistryFrozen));
        instances.reserve(registry.Registrations().size());
        for (const GameplaySystemRegistration &registration : registry.Registrations()) {
            if (const Result<void> dependencies =
                    Detail::ValidateSystemRuntimeDependencies(registration.descriptor, activeServices, capabilities);
                dependencies.HasError())
                return dependencies;
            auto created = Detail::CreateSystem(registration);
            if (created.HasError())
                return Result<void>::Failure(created.ErrorValue());
            IGameplaySystem *implementation = created.Value();
            instances.push_back({&registration, implementation});
            instancesByPhase[static_cast<std::size_t>(registration.descriptor.phase)].push_back(instances.size() - 1);
            const GameplaySystemContext context{cancellation.Token(),
                                                activeServices,
                                                capabilities,
                                                registration.descriptor.phase,
                                                GameplayThreadAffinity::RuntimeOwner,
                                                0.0};
            Result<void> started = Detail::StartSystem(*implementation, context);
            if (started.HasError())
                return started;
        }
        return Result<void>::Success();
    }

    void GameplaySystemRuntime::Impl::Rollback() noexcept {
        cancellation.RequestCancellation();
        for (auto instance = instances.rbegin(); instance != instances.rend(); ++instance) {
            const GameplaySystemContext context{cancellation.Token(),
                                                activeServices,
                                                capabilities,
                                                instance->registration->descriptor.phase,
                                                GameplayThreadAffinity::RuntimeOwner,
                                                0.0};
            instance->implementation->Stop(context);
            instance->registration->factory.destroy(instance->registration->factory.userData, instance->implementation);
        }
        instances.clear();
        for (std::vector<std::size_t> &phaseInstances : instancesByPhase)
            phaseInstances.clear();
    }

    GameplaySystemRuntime::GameplaySystemRuntime(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl)) {}

    /** @copydoc GameplaySystemRuntime::Create */
    Result<std::unique_ptr<GameplaySystemRuntime>> GameplaySystemRuntime::Create(const SystemRegistry &registry,
                                                                                 const std::span<const GameplayServiceId> activeServices,
                                                                                 const std::span<const GameplayCapabilityId> capabilities,
                                                                                 const CancellationToken parentCancellation) {
        auto impl = std::make_unique<Impl>(registry, activeServices, capabilities, parentCancellation);
        if (Result<void> built = impl->Build(); built.HasError()) {
            impl->Rollback();
            return Result<std::unique_ptr<GameplaySystemRuntime>>::Failure(built.ErrorValue());
        }
        return Result<std::unique_ptr<GameplaySystemRuntime>>::Success(
            std::unique_ptr<GameplaySystemRuntime>{new GameplaySystemRuntime{std::move(impl)}});  // NOSONAR(cpp:S5950)
    }

    GameplaySystemRuntime::~GameplaySystemRuntime() {
        Shutdown();
    }

    /** @copydoc GameplaySystemRuntime::RequestCancellation */
    void GameplaySystemRuntime::RequestCancellation() const noexcept {
        impl_->cancellation.RequestCancellation();
    }

    /** @copydoc GameplaySystemRuntime::Shutdown */
    void GameplaySystemRuntime::Shutdown() noexcept {
        if (!impl_ || impl_->shutdown)
            return;
        impl_->shutdown = true;
        impl_->Rollback();
    }

    /** @copydoc GameplaySystemRuntime::Cancellation */
    CancellationToken GameplaySystemRuntime::Cancellation() const noexcept {
        return impl_->cancellation.Token();
    }

    /** @copydoc GameplaySystemRuntime::InstanceCount */
    std::size_t GameplaySystemRuntime::InstanceCount() const noexcept {
        return impl_->instances.size();
    }
}  // namespace Horo::Gameplay
