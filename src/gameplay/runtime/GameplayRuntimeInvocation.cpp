#include "GameplayRuntimeInvocation.h"

#include "Horo/Gameplay/GameplayErrors.h"

namespace Horo::Gameplay::Detail {
    Result<IGameplayService *> CreateService(const GameplayServiceRegistration &registration) noexcept {
        try {
            IGameplayService *service = registration.factory.create(registration.factory.userData);
            if (service == nullptr)
                return Result<IGameplayService *>::Failure(MakeError(GameplayErrors::GameplayFactoryFailed));
            return Result<IGameplayService *>::Success(service);
        } catch (...) {
            return Result<IGameplayService *>::Failure(
                MakeError(GameplayErrors::GameplayFactoryFailed, "Gameplay service factory threw an exception."));
        }
    }

    Result<void> StartService(IGameplayService &service, const GameplayServiceContext &context) noexcept {
        try {
            return service.Start(context);
        } catch (...) {
            return Result<void>::Failure(MakeError(GameplayErrors::GameplayFactoryFailed, "Gameplay service startup threw an exception."));
        }
    }

    Result<IGameplaySystem *> CreateSystem(const GameplaySystemRegistration &registration) noexcept {
        try {
            IGameplaySystem *system = registration.factory.create(registration.factory.userData);
            if (system == nullptr)
                return Result<IGameplaySystem *>::Failure(MakeError(GameplayErrors::GameplayFactoryFailed));
            return Result<IGameplaySystem *>::Success(system);
        } catch (...) {
            return Result<IGameplaySystem *>::Failure(
                MakeError(GameplayErrors::GameplayFactoryFailed, "Gameplay system factory threw an exception."));
        }
    }

    Result<void> StartSystem(IGameplaySystem &system, const GameplaySystemContext &context) noexcept {
        try {
            return system.Start(context);
        } catch (...) {
            return Result<void>::Failure(MakeError(GameplayErrors::GameplayFactoryFailed, "Gameplay system startup threw an exception."));
        }
    }

    Result<void> ExecuteSystem(IGameplaySystem &system, const GameplaySystemContext &context) noexcept {
        try {
            return system.Execute(context);
        } catch (...) {
            return Result<void>::Failure(MakeError(GameplayErrors::GameplayFactoryFailed, "Gameplay system execution threw an exception."));
        }
    }
}  // namespace Horo::Gameplay::Detail
