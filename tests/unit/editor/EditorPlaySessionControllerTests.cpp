#include "editor/gameplay/EditorPlaySessionController.h"

#include <catch2/catch_test_macros.hpp>

namespace {
    using namespace Horo;

    class MovingBehavior final : public Gameplay::IBehaviorInstance {
    public:
        void OnFixedUpdate(Gameplay::BehaviorContext &context, Gameplay::FixedDeltaTime) override {
            auto transform = context.LocalTransform();
            REQUIRE(transform.HasValue());
            Math::Transform moved = transform.Value();
            moved.translation.x += 1.0F;
            REQUIRE(context.SetLocalTransform(moved).HasValue());
        }
    };

    class FastMovingBehavior final : public Gameplay::IBehaviorInstance {
    public:
        void OnFixedUpdate(Gameplay::BehaviorContext &context, Gameplay::FixedDeltaTime) override {
            auto transform = context.LocalTransform();
            REQUIRE(transform.HasValue());
            Math::Transform moved = transform.Value();
            moved.translation.x += 2.0F;
            REQUIRE(context.SetLocalTransform(moved).HasValue());
        }
    };

    Gameplay::BehaviorTypeId BehaviorType() {
        auto parsed = Gameplay::BehaviorTypeId::Parse("game.tests.play_mover");
        REQUIRE(parsed.HasValue());
        return std::move(parsed).Value();
    }

    Gameplay::BehaviorRegistry Registry() {
        Gameplay::BehaviorRegistry registry;
        Gameplay::BehaviorDescriptor descriptor;
        descriptor.typeId = BehaviorType();
        descriptor.displayName = "Play Mover";
        descriptor.phases.push_back({Gameplay::BehaviorPhase::Gameplay, "game.tests.play_mover", {}, {}, {}});
        REQUIRE(registry
                    .Register({std::move(descriptor), []() -> std::unique_ptr<Gameplay::IBehaviorInstance> {
            return std::make_unique<MovingBehavior>();
        }}).HasValue());
        REQUIRE(registry.Freeze().HasValue());
        return registry;
    }

    Gameplay::BehaviorRegistry FastRegistry() {
        Gameplay::BehaviorRegistry registry;
        Gameplay::BehaviorDescriptor descriptor;
        descriptor.typeId = BehaviorType();
        descriptor.displayName = "Fast Play Mover";
        descriptor.phases.push_back({Gameplay::BehaviorPhase::Gameplay, "game.tests.play_mover", {}, {}, {}});
        REQUIRE(registry
                    .Register({std::move(descriptor), []() -> std::unique_ptr<Gameplay::IBehaviorInstance> {
            return std::make_unique<FastMovingBehavior>();
        }}).HasValue());
        REQUIRE(registry.Freeze().HasValue());
        return registry;
    }

    Editor::SceneDocumentSnapshot AuthoringScene() {
        Editor::SceneObjectComponentSet camera;
        camera.camera = Runtime::CameraComponent{};
        Editor::SceneObjectComponentSet actor;
        actor.behaviors.push_back({Gameplay::BehaviorInstanceId{1}, BehaviorType(), 1, true, {}});
        return {Editor::DocumentRevision{7},
                Editor::DocumentStateId{11},
                {{Editor::SceneObjectId{1}, std::nullopt, "Camera", {}, std::nullopt, std::move(camera)},
                 {Editor::SceneObjectId{2}, std::nullopt, "Actor", {}, std::nullopt, std::move(actor)}}};
    }
}  // namespace

TEST_CASE("play session pause step stop keeps authoring snapshot isolated") {
    Gameplay::BehaviorRegistry registry = Registry();
    const Editor::SceneDocumentSnapshot authoring = AuthoringScene();
    Editor::EditorPlaySessionController play;

    REQUIRE(play.Start(authoring, registry).HasValue());
    REQUIRE(play.State() == Editor::EditorPlaySessionState::Playing);
    REQUIRE(play.AuthoringRevision() == authoring.revision);
    REQUIRE(play.FixedUpdate({}, Gameplay::FixedDeltaTime{1.0 / 60.0}).HasValue());

    REQUIRE(play.Pause().HasValue());
    REQUIRE(play.FixedUpdate({}, Gameplay::FixedDeltaTime{1.0 / 60.0}).HasValue());
    auto actor = play.Scene()->View().Find(Runtime::SceneObjectId{2});
    REQUIRE(actor.has_value());
    REQUIRE(play.Scene()->View().Get(*actor).Value().localTransform->translation.x == 1.0F);

    REQUIRE(play.Step().HasValue());
    REQUIRE(play.FixedUpdate({}, Gameplay::FixedDeltaTime{1.0 / 60.0}).HasValue());
    REQUIRE(play.Scene()->View().Get(*actor).Value().localTransform->translation.x == 2.0F);
    REQUIRE(authoring.objects[1].localTransform.translation.x == 0.0F);

    play.Stop();
    REQUIRE(play.State() == Editor::EditorPlaySessionState::Idle);
    REQUIRE(play.Scene() == nullptr);
    REQUIRE(authoring.revision == Editor::DocumentRevision{7});
}

TEST_CASE("play session rejects a scene without a runtime camera") {
    Gameplay::BehaviorRegistry registry = Registry();
    Editor::SceneDocumentSnapshot authoring = AuthoringScene();
    authoring.objects.erase(authoring.objects.begin());
    Editor::EditorPlaySessionController play;
    REQUIRE(play.Start(authoring, registry).HasError());
    REQUIRE(play.State() == Editor::EditorPlaySessionState::Failed);
    REQUIRE(play.LastError().has_value());
}

TEST_CASE("play session reloads behavior factories without replacing current runtime scene state") {
    Gameplay::BehaviorRegistry original = Registry();
    Gameplay::BehaviorRegistry candidate = FastRegistry();
    Editor::EditorPlaySessionController play;
    REQUIRE(play.Start(AuthoringScene(), original).HasValue());
    const Runtime::SceneRuntimeId runtimeId = play.Scene()->View().RuntimeId();
    REQUIRE(play.FixedUpdate({}, Gameplay::FixedDeltaTime{1.0 / 60.0}).HasValue());

    REQUIRE(play.ReloadBehaviors(candidate, original).HasValue());
    REQUIRE(play.Scene()->View().RuntimeId() == runtimeId);
    REQUIRE(play.FixedUpdate({}, Gameplay::FixedDeltaTime{1.0 / 60.0}).HasValue());
    const auto actor = play.Scene()->View().Find(Runtime::SceneObjectId{2});
    REQUIRE(actor.has_value());
    REQUIRE(play.Scene()->View().Get(*actor).Value().localTransform->translation.x == 3.0F);
}
