#include "Horo/Gameplay/BehaviorRuntime.h"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <string>
#include <vector>

namespace {
    using namespace Horo;
    using namespace Horo::Gameplay;
    using namespace Horo::Runtime;

    BehaviorTypeId Type(const std::string_view value = "game.tests.mover") {
        auto parsed = BehaviorTypeId::Parse(value);
        REQUIRE(parsed.HasValue());
        return std::move(parsed).Value();
    }

    struct Recorder {
        std::vector<std::string> calls;
        std::size_t destroyed{};
    };

    class RecordingBehavior final : public IBehaviorInstance {
    public:
        explicit RecordingBehavior(Recorder &recorder) : recorder_(&recorder) {}

        void OnCreate(BehaviorContext &) override {
            recorder_->calls.emplace_back("create");
        }

        void OnEnable(BehaviorContext &) override {
            recorder_->calls.emplace_back("enable");
        }

        void OnStart(BehaviorContext &) override {
            recorder_->calls.emplace_back("start");
        }

        void OnInputAction(BehaviorContext &, const GameplayInputAction &) override {
            recorder_->calls.emplace_back("input");
        }

        void OnEvent(BehaviorContext &, const GameplayEvent &) override {
            recorder_->calls.emplace_back("event");
        }

        void OnFixedUpdate(BehaviorContext &context, FixedDeltaTime) override {
            recorder_->calls.emplace_back("fixed");
            const auto input = context.InputActions();
            if (!input.empty() && input.front().down) {
                auto transform = context.LocalTransform();
                REQUIRE(transform.HasValue());
                Math::Transform moved = transform.Value();
                moved.translation.x += input.front().x;
                REQUIRE(context.SetLocalTransform(moved).HasValue());
            }
            if (!published_) {
                REQUIRE(context.Publish(GameplayEvent{GameplayEventTypeId{"game.tests.moved"}, 1, context.Entity(), {}}).HasValue());
                published_ = true;
            }
        }

        void OnPresentationUpdate(BehaviorContext &, FrameDeltaTime) override {
            recorder_->calls.emplace_back("presentation");
        }

        void OnDisable(BehaviorContext &) override {
            recorder_->calls.emplace_back("disable");
        }

        void OnDestroy(BehaviorContext &) override {
            recorder_->calls.emplace_back("destroy");
        }

    private:
        Recorder *recorder_{};
        bool published_{};
    };

    IBehaviorInstance *Create(void *userData) {
        return new RecordingBehavior{*static_cast<Recorder *>(userData)};
    }

    void Destroy(void *userData, IBehaviorInstance *instance) noexcept {
        ++static_cast<Recorder *>(userData)->destroyed;
        delete instance;
    }

    BehaviorRegistry Registry(Recorder &recorder, const bool allowMultiple = false) {
        BehaviorRegistry registry;
        BehaviorDescriptor descriptor;
        descriptor.typeId = Type();
        descriptor.displayName = "Mover";
        descriptor.allowMultiple = allowMultiple;
        descriptor.phases.push_back({BehaviorPhase::Gameplay, "game.tests.mover", {}, {}, {}});
        REQUIRE(registry.Register({std::move(descriptor), {&recorder, &Create, &Destroy}}).HasValue());
        REQUIRE(registry.Freeze().HasValue());
        return registry;
    }

    RuntimeSceneDefinition Definition(const std::size_t attachmentCount = 1) {
        RuntimeComponentSet components;
        for (std::size_t index = 0; index < attachmentCount; ++index)
            components.behaviors.push_back({BehaviorInstanceId{index + 1}, Type(), 1, true, {}});
        SceneDefinitionBuilder builder{SceneDefinitionId{4}, SceneDefinitionRevision{1}};
        builder.Add({SceneObjectId{9}, std::nullopt, {}, std::nullopt, std::move(components)});
        auto definition = std::move(builder).Build();
        REQUIRE(definition.HasValue());
        return std::move(definition).Value();
    }
}  // namespace

TEST_CASE("behavior runtime owns deterministic lifecycle input events and deferred transform mutation") {
    Recorder recorder;
    BehaviorRegistry registry = Registry(recorder);
    auto sceneResult = RuntimeScene::Create(Definition(), SceneRuntimeId{12});
    REQUIRE(sceneResult.HasValue());
    std::unique_ptr<RuntimeScene> scene = std::move(sceneResult).Value();

    auto runtimeResult = BehaviorRuntime::Create(*scene, registry);
    REQUIRE(runtimeResult.HasValue());
    std::unique_ptr<BehaviorRuntime> runtime = std::move(runtimeResult).Value();
    REQUIRE((recorder.calls == std::vector<std::string>{"create", "enable"}));

    const GameplayInputAction move{GameplayActionId{"game.tests.move"}, 2.0F, 0.0F, true, true, false};
    REQUIRE(runtime->FixedUpdate({&move, 1}, FixedDeltaTime{1.0 / 60.0}).HasValue());
    REQUIRE((recorder.calls == std::vector<std::string>{"create", "enable", "start", "input", "fixed"}));
    auto entity = scene->View().Find(SceneObjectId{9});
    REQUIRE(entity.has_value());
    auto view = scene->View().Get(*entity);
    REQUIRE(view.HasValue());
    REQUIRE(view.Value().localTransform->translation.x == 2.0F);

    REQUIRE(runtime->FixedUpdate({}, FixedDeltaTime{1.0 / 60.0}).HasValue());
    REQUIRE(recorder.calls.at(5) == "event");
    REQUIRE(recorder.calls.at(6) == "fixed");
    runtime->PresentationUpdate(FrameDeltaTime{1.0 / 120.0});
    REQUIRE(recorder.calls.back() == "presentation");

    REQUIRE(runtime->SetEnabled(BehaviorInstanceId{1}, false).HasValue());
    REQUIRE(recorder.calls.back() == "disable");
    REQUIRE(runtime->SetEnabled(BehaviorInstanceId{1}, true).HasValue());
    REQUIRE(recorder.calls.back() == "enable");
    REQUIRE(runtime->FixedUpdate({}, FixedDeltaTime{1.0 / 60.0}).HasValue());
    REQUIRE(std::ranges::count(recorder.calls, "start") == 1);

    runtime->Shutdown();
    REQUIRE(recorder.calls.at(recorder.calls.size() - 2) == "disable");
    REQUIRE(recorder.calls.back() == "destroy");
    REQUIRE(recorder.destroyed == 1);
}

TEST_CASE("behavior runtime rejects duplicate attachments unless the descriptor allows them") {
    Recorder recorder;
    BehaviorRegistry registry = Registry(recorder);
    auto scene = RuntimeScene::Create(Definition(2), SceneRuntimeId{12});
    REQUIRE(scene.HasValue());
    auto runtime = BehaviorRuntime::Create(*scene.Value(), registry);
    REQUIRE(runtime.HasError());
    REQUIRE(recorder.destroyed == 1);
}
