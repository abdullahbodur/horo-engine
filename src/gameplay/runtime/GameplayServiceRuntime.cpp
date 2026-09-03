#include "GameplayRegistrationRuntimeDetail.h"
#include "GameplayRuntimeInvocation.h"
#include "GameplayRuntimeValidation.h"
#include "Horo/Gameplay/GameplayErrors.h"

namespace Horo::Gameplay {
    namespace {
        [[nodiscard]] bool AllowsThread(const GameplayThreadAffinity declared, const GameplayThreadAffinity actual) noexcept {
            return declared == GameplayThreadAffinity::Any || declared == actual;
        }
    }  // namespace

    Result<void> GameplayServiceRuntime::Impl::Build() {
        if (!registry.IsFrozen())
            return Result<void>::Failure(MakeError(GameplayErrors::RegistrationRegistryFrozen));
        for (const GameplayServiceRegistration &registration : registry.Registrations()) {
            if (registration.descriptor.scope != scope)
                continue;
            if (!AllowsThread(registration.descriptor.affinity, thread))
                return Result<void>::Failure(MakeError(GameplayErrors::GameplayThreadAccessViolation));
            if (const Result<void> dependencies =
                    Detail::ValidateServiceRuntimeDependencies(registration.descriptor, activeServices, capabilities);
                dependencies.HasError())
                return dependencies;
            auto created = Detail::CreateService(registration);
            if (created.HasError())
                return Result<void>::Failure(created.ErrorValue());
            IGameplayService *implementation = created.Value();
            instances.push_back({&registration, implementation});
            const GameplayServiceContext context{cancellation.Token(), activeServices, capabilities, thread};
            if (Result<void> started = Detail::StartService(*implementation, context); started.HasError())
                return started;
            activeServices.push_back(registration.descriptor.id);
            capabilities.insert(capabilities.end(), registration.descriptor.providedCapabilities.begin(),
                                registration.descriptor.providedCapabilities.end());
        }
        return Result<void>::Success();
    }

    void GameplayServiceRuntime::Impl::Rollback() noexcept {
        cancellation.RequestCancellation();
        const GameplayServiceContext context{cancellation.Token(), activeServices, capabilities, thread};
        for (auto instance = instances.rbegin(); instance != instances.rend(); ++instance) {
            instance->implementation->Stop(context);
            instance->registration->factory.destroy(instance->registration->factory.userData, instance->implementation);
        }
        instances.clear();
    }

    GameplayServiceRuntime::GameplayServiceRuntime(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl)) {}

    /** @copydoc GameplayServiceRuntime::Create */
    Result<std::unique_ptr<GameplayServiceRuntime>> GameplayServiceRuntime::Create(const GameServiceRegistry &registry,
                                                                                   const GameplayServiceScope scope,
                                                                                   const GameplayServiceRuntimeDependencies &dependencies,
                                                                                   const CancellationToken parentCancellation,
                                                                                   const GameplayThreadAffinity thread) {
        auto impl = std::make_unique<Impl>(registry, scope, dependencies, parentCancellation, thread);
        if (Result<void> built = impl->Build(); built.HasError()) {
            impl->Rollback();
            return Result<std::unique_ptr<GameplayServiceRuntime>>::Failure(built.ErrorValue());
        }
        return Result<std::unique_ptr<GameplayServiceRuntime>>::Success(
            std::unique_ptr<GameplayServiceRuntime>{new GameplayServiceRuntime{std::move(impl)}});  // NOSONAR(cpp:S5950)
    }

    GameplayServiceRuntime::~GameplayServiceRuntime() {
        Shutdown();
    }

    /** @copydoc GameplayServiceRuntime::RequestCancellation */
    void GameplayServiceRuntime::RequestCancellation() const noexcept {
        impl_->cancellation.RequestCancellation();
    }

    /** @copydoc GameplayServiceRuntime::Shutdown */
    void GameplayServiceRuntime::Shutdown() noexcept {
        if (!impl_ || impl_->shutdown)
            return;
        impl_->shutdown = true;
        impl_->Rollback();
    }

    /** @copydoc GameplayServiceRuntime::Cancellation */
    CancellationToken GameplayServiceRuntime::Cancellation() const noexcept {
        return impl_->cancellation.Token();
    }

    /** @copydoc GameplayServiceRuntime::ActiveServices */
    std::span<const GameplayServiceId> GameplayServiceRuntime::ActiveServices() const noexcept {
        return impl_->activeServices;
    }

    /** @copydoc GameplayServiceRuntime::Capabilities */
    std::span<const GameplayCapabilityId> GameplayServiceRuntime::Capabilities() const noexcept {
        return impl_->capabilities;
    }

    /** @copydoc GameplayServiceRuntime::InstanceCount */
    std::size_t GameplayServiceRuntime::InstanceCount() const noexcept {
        return impl_->instances.size();
    }
}  // namespace Horo::Gameplay
