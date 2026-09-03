#include "Horo/Gameplay/ComponentRegistry.h"
#include "Horo/Gameplay/GameModule.h"
#include "Horo/Gameplay/GameServiceRegistry.h"
#include "Horo/Gameplay/GameplayErrors.h"
#include "Horo/Gameplay/NativeBehavior.h"
#include "Horo/Gameplay/SystemRegistry.h"

#include <algorithm>

using namespace Horo;
using namespace Horo::Gameplay;

class MoveBehavior final : public IBehaviorInstance {
public:
    static BehaviorDescriptor DescribeBehavior() {
        BehaviorDescriptor descriptor;
        descriptor.displayName = "Dynamic Mover";
        descriptor.phases.push_back({BehaviorPhase::Gameplay, "game.tests.dynamic_mover", {}, {}, {}});
        return descriptor;
    }

    void OnFixedUpdate(BehaviorContext &context, FixedDeltaTime) override {
        const auto actions = context.InputActions();
        if (actions.empty() || !actions.front().down)
            return;
        auto transform = context.LocalTransform();
        if (transform.HasError())
            return;
        Math::Transform moved = transform.Value();
        moved.translation.x += actions.front().x;
        static_cast<void>(context.SetLocalTransform(moved));
    }
};

HORO_BEHAVIOR(MoveBehavior, "game.tests.dynamic_mover")

namespace {
    class TestProjectService final : public IGameplayService {
    public:
        Result<void> Start(const GameplayServiceContext &context) override {
            return context.cancellation.IsCancellationRequested() ? Result<void>::Failure(MakeError(GameplayErrors::GameplayCancelled))
                                                                  : Result<void>::Success();
        }

        void Stop(const GameplayServiceContext &) noexcept override {}
    };

    class TestGameplaySystem final : public IGameplaySystem {
    public:
        Result<void> Start(const GameplaySystemContext &) override {
            return Result<void>::Success();
        }

        Result<void> Execute(const GameplaySystemContext &context) override {
            return context.cancellation.IsCancellationRequested() ? Result<void>::Failure(MakeError(GameplayErrors::GameplayCancelled))
                                                                  : Result<void>::Success();
        }

        void Stop(const GameplaySystemContext &) noexcept override {}
    };

    IGameplayService *CreateTestProjectService(void *) {
        return new TestProjectService{};
    }

    void DestroyTestProjectService(void *, IGameplayService *service) noexcept {
        delete service;
    }

    IGameplaySystem *CreateTestGameplaySystem(void *) {
        return new TestGameplaySystem{};
    }

    void DestroyTestGameplaySystem(void *, IGameplaySystem *system) noexcept {
        delete system;
    }

    class Module final : public IGameModule {
    public:
        Result<void> Register(GameRegistrationContext &context) override {
            ComponentDescriptor descriptor{
                .typeId = ComponentTypeId::Parse("game.tests.movement_settings").Value(),
                .schemaVersion = 2,
                .displayName = "Movement Settings",
                .category = "Gameplay/Movement",
                .properties = {{ComponentPropertyId::Parse("speed").Value(), "Speed", ComponentPropertyKind::Number, true}},
                .migrations = {{1, 2}},
            };
            if (Result<void> component = context.components.Register(std::move(descriptor)); component.HasError())
                return component;

            GameplayServiceRegistration service{
                .descriptor =
                    {
                        .id = GameplayServiceId::Parse("game.tests.session_service").Value(),
                        .scope = GameplayServiceScope::Project,
                        .affinity = GameplayThreadAffinity::RuntimeOwner,
                        .sceneReplacement = GameplaySceneReplacementPolicy::Preserve,
                        .providedCapabilities = {GameplayCapabilityId::Parse("game.tests.session.read").Value()},
                        .observabilityCategory = "game.tests.gameplay",
                    },
                .factory = {.create = &CreateTestProjectService, .destroy = &DestroyTestProjectService},
            };
            if (Result<void> registered = context.services.Register(std::move(service)); registered.HasError())
                return registered;

            GameplaySystemRegistration system{
                .descriptor =
                    {
                        .id = GameplaySystemId::Parse("game.tests.session_system").Value(),
                        .phase = GameplaySystemPhase::Gameplay,
                        .affinity = GameplayThreadAffinity::RuntimeOwner,
                        .requiredServices = {GameplayServiceId::Parse("game.tests.session_service").Value()},
                        .requiredCapabilities = {GameplayCapabilityId::Parse("game.tests.session.read").Value()},
                    },
                .factory = {.create = &CreateTestGameplaySystem, .destroy = &DestroyTestGameplaySystem},
            };
            return context.systems.Register(std::move(system));
        }

        Result<void> Start(GameRuntimeContext &context) override {
            const GameplayServiceId service = GameplayServiceId::Parse("game.tests.session_service").Value();
            const GameplayCapabilityId capability = GameplayCapabilityId::Parse("game.tests.session.read").Value();
            if (context.cancellation.IsCancellationRequested() ||
                std::ranges::find(context.activeServices, service) == context.activeServices.end() ||
                std::ranges::find(context.capabilities, capability) == context.capabilities.end())
                return Result<void>::Failure(MakeError(GameplayErrors::CapabilityMissing));
            return Result<void>::Success();
        }

        void Stop(GameRuntimeContext &) noexcept override {}
    };

}  // namespace

extern "C" HORO_GAME_EXPORT IGameModule *CreateGameModule() noexcept {
    return new Module{};
}

extern "C" HORO_GAME_EXPORT void DestroyGameModule(IGameModule *module) noexcept {
    delete module;
}
