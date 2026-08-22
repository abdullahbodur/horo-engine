#include "Horo/Gameplay/BehaviorRuntime.h"
#include "Horo/Gameplay/LuaBehavior.h"

#include <catch2/catch_test_macros.hpp>

namespace {
    using namespace Horo;
    using namespace Horo::Gameplay;
    using namespace Horo::Runtime;

    BehaviorTypeId Type() {
        auto parsed = BehaviorTypeId::Parse("game.tests.lua_mover");
        REQUIRE(parsed.HasValue());
        return std::move(parsed).Value();
    }

    std::string Source(const float multiplier) {
        return R"(
return horo.behavior {
    type_id = "game.tests.lua_mover",
    display_name = "Lua Mover",
    category = "Tests",
    schema_version = 1,
    fields = {
        { name = "speed", default = 1.0 }
    },
    on_fixed_update = function(ctx, dt)
        local x, y, z = ctx.transform.position()
        local move_x, move_y, down = ctx.input.action("game.tests.move")
        local speed = ctx.fields.get("speed") or 1.0
        if down then
            ctx.transform.set_position(x + move_x * speed * )" +
               std::to_string(multiplier) + R"(, y, z)
            ctx.events.publish("game.tests.lua_moved")
        end
    end
}
)";
    }

    RuntimeSceneDefinition Definition() {
        RuntimeComponentSet components;
        components.behaviors.push_back({
            BehaviorInstanceId{8},
            Type(),
            1,
            true,
            {BehaviorField{"speed", 2.0}},
        });
        SceneDefinitionBuilder builder{SceneDefinitionId{5}, SceneDefinitionRevision{1}};
        builder.Add({SceneObjectId{2}, std::nullopt, {}, std::nullopt, std::move(components)});
        auto built = std::move(builder).Build();
        REQUIRE(built.HasValue());
        return std::move(built).Value();
    }
}  // namespace

TEST_CASE("Lua behavior uses the shared lifecycle input fields and deferred transform boundary") {
    auto program = LuaBehaviorProgram::Compile(Source(1.0F), Type(), "lua_mover.horo_script");
    REQUIRE(program.HasValue());
    REQUIRE(program.Value()->Descriptor().displayName == "Lua Mover");

    BehaviorRegistry registry;
    REQUIRE(registry.Register(program.Value()->Registration()).HasValue());
    REQUIRE(registry.Freeze().HasValue());
    auto scene = RuntimeScene::Create(Definition(), SceneRuntimeId{15});
    REQUIRE(scene.HasValue());
    auto runtime = BehaviorRuntime::Create(*scene.Value(), registry);
    REQUIRE(runtime.HasValue());

    const GameplayInputAction move{GameplayActionId{"game.tests.move"}, 3.0F, 0.0F, true, true, false};
    REQUIRE(runtime.Value()->FixedUpdate({&move, 1}, FixedDeltaTime{1.0 / 60.0}).HasValue());
    const auto entity = scene.Value()->View().Find(SceneObjectId{2});
    REQUIRE(entity.has_value());
    auto view = scene.Value()->View().Get(*entity);
    REQUIRE(view.HasValue());
    REQUIRE(view.Value().localTransform->translation.x == 6.0F);

    auto candidate = LuaBehaviorProgram::Compile(Source(2.0F), Type(), "lua_mover.horo_script");
    REQUIRE(candidate.HasValue());
    REQUIRE(program.Value()->ReplaceCompatible(std::move(candidate).Value()).HasValue());
    REQUIRE(program.Value()->Revision() == 2);
    REQUIRE(runtime.Value()->FixedUpdate({&move, 1}, FixedDeltaTime{1.0 / 60.0}).HasValue());
    view = scene.Value()->View().Get(*entity);
    REQUIRE(view.HasValue());
    REQUIRE(view.Value().localTransform->translation.x == 18.0F);

    runtime.Value()->Shutdown();
}

TEST_CASE("Lua behavior rejects sidecar identity mismatch and unavailable OS libraries") {
    const std::string mismatch = "return horo.behavior { type_id='game.tests.other', display_name='Wrong' }";
    REQUIRE(LuaBehaviorProgram::Compile(mismatch, Type(), "mismatch.horo_script").HasError());
    const std::string unsafe = "os.execute('echo unsafe'); return horo.behavior { display_name='Unsafe' }";
    REQUIRE(LuaBehaviorProgram::Compile(unsafe, Type(), "unsafe.horo_script").HasError());
}

TEST_CASE("Lua behavior enforces its memory budget while compiling source") {
    const std::string oversizedAllocation = R"(
local payload = string.rep("x", 256 * 1024)
return horo.behavior {
    type_id = "game.tests.lua_mover",
    display_name = payload
}
)";
    const LuaBehaviorLimits limits{.maximumMemoryBytes = 64U * 1024U, .maximumInstructionsPerCallback = 100'000};
    REQUIRE(LuaBehaviorProgram::Compile(oversizedAllocation, Type(), "memory_budget.horo_script", limits).HasError());
}

TEST_CASE("Lua lifecycle receives typed input callback values") {
    const std::string source = R"(
return horo.behavior {
    type_id = "game.tests.lua_mover",
    display_name = "Input Callback",
    on_input_action = function(ctx, action)
        if action.id == "game.tests.move" and action.pressed then
            local x, y, z = ctx.transform.position()
            ctx.transform.set_position(x + action.x, y + action.y, z)
        end
    end
}
)";
    auto program = LuaBehaviorProgram::Compile(source, Type(), "input_callback.horo_script");
    REQUIRE(program.HasValue());
    BehaviorRegistry registry;
    REQUIRE(registry.Register(program.Value()->Registration()).HasValue());
    REQUIRE(registry.Freeze().HasValue());
    auto scene = RuntimeScene::Create(Definition(), SceneRuntimeId{16});
    REQUIRE(scene.HasValue());
    auto runtime = BehaviorRuntime::Create(*scene.Value(), registry);
    REQUIRE(runtime.HasValue());
    const GameplayInputAction move{GameplayActionId{"game.tests.move"}, 4.0F, 2.0F, true, true, false};
    REQUIRE(runtime.Value()->FixedUpdate({&move, 1}, FixedDeltaTime{1.0 / 60.0}).HasValue());
    const auto entity = scene.Value()->View().Find(SceneObjectId{2});
    REQUIRE(entity.has_value());
    REQUIRE(scene.Value()->View().Get(*entity).Value().localTransform->translation.x == 4.0F);
}
