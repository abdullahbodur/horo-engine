#include "Horo/Gameplay/ComponentRegistry.h"
#include "Horo/Gameplay/GameModule.h"
#include "Horo/Gameplay/NativeBehavior.h"

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
            return context.components.Register(std::move(descriptor));
        }

        Result<void> Start(GameRuntimeContext &) override {
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
