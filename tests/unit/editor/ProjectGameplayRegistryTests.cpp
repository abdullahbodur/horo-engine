#include "Horo/Gameplay/BehaviorRuntime.h"
#include "editor/gameplay/ProjectGameplayRegistry.h"

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>

namespace {
    using namespace Horo;

    struct TemporaryProject {
        std::filesystem::path root =
            std::filesystem::temp_directory_path() /
            ("horo-gameplay-registry-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));

        TemporaryProject() {
            std::filesystem::create_directories(root / "assets" / "scripts");
        }

        ~TemporaryProject() {
            std::error_code error;
            std::filesystem::remove_all(root, error);
        }
    };

    void Write(const std::filesystem::path &path, const std::string &value) {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream stream{path, std::ios::binary | std::ios::trunc};
        REQUIRE(stream.good());
        stream << value;
        stream.close();
        REQUIRE(stream.good());
    }

    std::string Source(const int amount) {
        return "return horo.behavior { type_id='game.tests.watched', display_name='Watched', "
               "on_fixed_update=function(ctx, dt) local x,y,z=ctx.transform.position(); "
               "ctx.transform.set_position(x+" +
               std::to_string(amount) + ",y,z) end }";
    }
}  // namespace

TEST_CASE("project gameplay registry discovers and safely reloads compatible Lua source") {
    TemporaryProject project;
    const std::filesystem::path source = project.root / "assets" / "scripts" / "Watched.horo_script";
    Write(source, Source(1));
    Write(source.string() + ".meta", R"({"schemaVersion":1,"runtime":"lua","behaviorTypeId":"game.tests.watched"})");

    auto registry = Editor::ProjectGameplayRegistry::Discover(project.root);
    REQUIRE_FALSE(registry->HasBlockingDiagnostics());
    auto type = Gameplay::BehaviorTypeId::Parse("game.tests.watched");
    REQUIRE(type.HasValue());

    Runtime::RuntimeComponentSet components;
    components.behaviors.push_back({Gameplay::BehaviorInstanceId{1}, type.Value(), 1, true, {}});
    Runtime::SceneDefinitionBuilder builder{Runtime::SceneDefinitionId{1}, Runtime::SceneDefinitionRevision{1}};
    builder.Add({Runtime::SceneObjectId{1}, std::nullopt, {}, std::nullopt, std::move(components)});
    auto definition = std::move(builder).Build();
    REQUIRE(definition.HasValue());
    auto scene = Runtime::RuntimeScene::Create(definition.Value(), Runtime::SceneRuntimeId{1});
    REQUIRE(scene.HasValue());
    auto runtime = Gameplay::BehaviorRuntime::Create(*scene.Value(), registry->Registry());
    REQUIRE(runtime.HasValue());
    REQUIRE(runtime.Value()->FixedUpdate({}, Gameplay::FixedDeltaTime{1.0 / 60.0}).HasValue());

    Write(source, Source(3));
    std::error_code timeError;
    const auto newer = std::filesystem::last_write_time(source, timeError) + std::chrono::seconds(2);
    REQUIRE_FALSE(timeError);
    std::filesystem::last_write_time(source, newer, timeError);
    REQUIRE_FALSE(timeError);
    REQUIRE(registry->ReloadChangedLuaSources().empty());
    REQUIRE(runtime.Value()->FixedUpdate({}, Gameplay::FixedDeltaTime{1.0 / 60.0}).HasValue());

    const auto entity = scene.Value()->View().Find(Runtime::SceneObjectId{1});
    REQUIRE(entity.has_value());
    REQUIRE(scene.Value()->View().Get(*entity).Value().localTransform->translation.x == 4.0F);
}

TEST_CASE("project gameplay registry merges a fingerprinted native module with Lua behaviors") {
    TemporaryProject project;
    const std::filesystem::path source = project.root / "assets" / "scripts" / "Watched.horo_script";
    Write(source, Source(1));
    Write(source.string() + ".meta", R"({"schemaVersion":1,"runtime":"lua","behaviorTypeId":"game.tests.watched"})");
    const std::string manifest = "{\n  \"schemaVersion\": 1,\n  \"moduleId\": \"game.tests\",\n  \"buildFingerprint\": \"" +
                                 std::string{Gameplay::CurrentGameplayBuildFingerprint()} +
                                 "\",\n  \"descriptorRevision\": 1,\n  \"artifactPath\": \"" +
                                 std::filesystem::path{HORO_TEST_GAME_MODULE_PATH}.string() + "\"\n}\n";
    Write(project.root / ".horo" / "local" / "gameplay_module.json", manifest);

    auto registry = Editor::ProjectGameplayRegistry::Discover(project.root);
    REQUIRE_FALSE(registry->HasBlockingDiagnostics());
    REQUIRE(registry->Registry().Find(Gameplay::BehaviorTypeId::Parse("game.tests.dynamic_mover").Value()) != nullptr);
    REQUIRE(registry->Registry().Find(Gameplay::BehaviorTypeId::Parse("game.tests.watched").Value()) != nullptr);
    REQUIRE_FALSE(registry->ConsumeNativeArtifactChange());
    const std::filesystem::path manifestPath = project.root / ".horo" / "local" / "gameplay_module.json";
    std::error_code timeError;
    std::filesystem::last_write_time(manifestPath, std::filesystem::last_write_time(manifestPath, timeError) + std::chrono::seconds(2),
                                     timeError);
    REQUIRE_FALSE(timeError);
    REQUIRE(registry->ConsumeNativeArtifactChange());
    REQUIRE_FALSE(registry->ConsumeNativeArtifactChange());
}

TEST_CASE("project gameplay registry reports native sources without a published successful artifact") {
    TemporaryProject project;
    Write(project.root / "source" / "gameplay" / "Player.cpp", "// native behavior source\n");
    auto registry = Editor::ProjectGameplayRegistry::Discover(project.root);
    REQUIRE(registry->HasBlockingDiagnostics());
    REQUIRE(registry->Diagnostics().front().source == project.root / ".horo" / "local" / "gameplay_module.json");
}
