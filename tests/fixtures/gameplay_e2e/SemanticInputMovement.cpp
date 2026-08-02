#include <Horo/Gameplay/NativeBehavior.h>

class SemanticInputMovement final : public Horo::Gameplay::IBehaviorInstance {
public:
    static Horo::Gameplay::BehaviorDescriptor DescribeBehavior() {
        Horo::Gameplay::BehaviorDescriptor descriptor;
        descriptor.displayName = "Semantic Input Movement";
        descriptor.category = "Tests";
        return descriptor;
    }

    void OnFixedUpdate(Horo::Gameplay::BehaviorContext &context, Horo::Gameplay::FixedDeltaTime) override {
        for (const Horo::Gameplay::GameplayInputAction &action : context.InputActions()) {
            if (action.action.Value() != "gameplay.move" || !action.down)
                continue;
            Horo::Result<Horo::Math::Transform> transform = context.LocalTransform();
            if (transform.HasError())
                return;
            Horo::Math::Transform moved = transform.Value();
            moved.translation.x += action.x;
            static_cast<void>(context.SetLocalTransform(moved));
        }
    }
};

HORO_BEHAVIOR(SemanticInputMovement, "{{BEHAVIOR_TYPE_ID}}")
