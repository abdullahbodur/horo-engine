#include "GameplayModuleTestSupport.h"
#include "Horo/Gameplay/BehaviorRuntime.h"
#include "Horo/Gameplay/GameModuleHost.h"
#include "Horo/Gameplay/GameplayErrors.h"

#include <catch2/catch_test_macros.hpp>
#include <filesystem>

namespace {
    using namespace Horo;
    using namespace Horo::Gameplay;
    using namespace Horo::Runtime;

    GameModuleLoadExpectation Expectation() {
        return {
            .moduleId = "game.tests",
            .buildFingerprint = CurrentGameplayBuildFingerprint(),
            .descriptorRevision = Tests::ReadDescriptorRevision(HORO_TEST_GAME_MODULE_REVISION_PATH),
        };
    }

    IGameModule *CreateTestModule() noexcept {
        return nullptr;
    }

    void DestroyTestModule(IGameModule *) noexcept {}

    IBehaviorInstance *CreateTestBehavior(void *) {
        return nullptr;
    }

    void DestroyTestBehavior(void *, IBehaviorInstance *) noexcept {}

    struct ValidBundleStorage {
        BehaviorDescriptor behavior;
        GeneratedBehaviorFactoryBinding binding;
        GeneratedGameplayDescriptorBundle bundle;

        ValidBundleStorage() {
            behavior.typeId = BehaviorTypeId::Parse("game.tests.valid").Value();
            binding = {
                .typeId = behavior.typeId,
                .factory = {.userData = nullptr, .create = &CreateTestBehavior, .destroy = &DestroyTestBehavior},
            };
            bundle = {
                .structSize = sizeof(GeneratedGameplayDescriptorBundle),
                .schemaVersion = GameplayDescriptorBundleSchemaVersion,
                .sdkBoundaryVersion = GameplaySdkBoundaryVersion,
                .moduleId = "game.tests",
                .buildFingerprint = CurrentGameplayBuildFingerprint().data(),
                .descriptorRevision = 7,
                .behaviors = &behavior,
                .behaviorCount = 1,
                .nativeFactoryBindings = &binding,
                .nativeFactoryBindingCount = 1,
                .diagnostics = nullptr,
                .diagnosticCount = 0,
                .lifecycle = {.create = &CreateTestModule, .destroy = &DestroyTestModule},
            };
        }
    };

    RuntimeSceneDefinition Definition() {
        RuntimeComponentSet components;
        components.behaviors.push_back({BehaviorInstanceId{1}, BehaviorTypeId::Parse("game.tests.dynamic_mover").Value(), 1, true, {}});
        SceneDefinitionBuilder builder{SceneDefinitionId{3}, SceneDefinitionRevision{1}};
        builder.Add({SceneObjectId{1}, std::nullopt, {}, std::nullopt, std::move(components)});
        auto built = std::move(builder).Build();
        REQUIRE(built.HasValue());
        return std::move(built).Value();
    }
}  // namespace

TEST_CASE("game module host validates fingerprint and keeps factories alive through behavior shutdown") {
    GameModuleHost host;
    auto relative = host.Load(std::filesystem::path{HORO_TEST_GAME_MODULE_PATH}.filename(), Expectation());
    REQUIRE(relative.HasError());
    GameModuleLoadExpectation mismatchExpectation = Expectation();
    mismatchExpectation.buildFingerprint = "wrong-fingerprint";
    auto mismatch = host.Load(HORO_TEST_GAME_MODULE_PATH, mismatchExpectation);
    REQUIRE(mismatch.HasError());
    REQUIRE(mismatch.ErrorValue().code.Value() == GameplayErrors::IncompatibleGameModule.code.Value());

    auto loaded = host.Load(HORO_TEST_GAME_MODULE_PATH, Expectation());
    REQUIRE(loaded.HasValue());
    REQUIRE(loaded.Value()->ModuleId() == "game.tests");
    REQUIRE(loaded.Value()->Registry().IsFrozen());

    auto scene = RuntimeScene::Create(Definition(), SceneRuntimeId{7});
    REQUIRE(scene.HasValue());
    auto runtime = BehaviorRuntime::Create(*scene.Value(), loaded.Value()->Registry());
    REQUIRE(runtime.HasValue());
    const GameplayInputAction move{GameplayActionId{"game.tests.move"}, 3.0F, 0.0F, true, true, false};
    REQUIRE(runtime.Value()->FixedUpdate({&move, 1}, FixedDeltaTime{1.0 / 60.0}).HasValue());
    const auto entity = scene.Value()->View().Find(SceneObjectId{1});
    REQUIRE(entity.has_value());
    const auto view = scene.Value()->View().Get(*entity);
    REQUIRE(view.HasValue());
    REQUIRE(view.Value().localTransform->translation.x == 3.0F);

    runtime.Value()->Shutdown();
}

TEST_CASE("game module host validates an independent shadow artifact and removes it after unload") {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "horo_game_module_host_shadow_copy_test";
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);

    GameModuleHost host;
    auto loaded = host.LoadShadowCopy(std::filesystem::path{HORO_TEST_GAME_MODULE_PATH}, root, Expectation());
    REQUIRE(loaded.HasValue());
    std::unique_ptr<LoadedGameModule> module = std::move(loaded).Value();
    const std::filesystem::path shadowPath = module->LoadedArtifactPath();
    REQUIRE(std::filesystem::equivalent(shadowPath.parent_path(), root));
    REQUIRE(shadowPath != std::filesystem::path{HORO_TEST_GAME_MODULE_PATH});
    REQUIRE(std::filesystem::is_regular_file(shadowPath));

    module.reset();
    REQUIRE_FALSE(std::filesystem::exists(shadowPath));
    std::filesystem::remove_all(root, ignored);
}

TEST_CASE("generated gameplay bundle validation rejects incompatible identity before lifecycle activation") {
    ValidBundleStorage storage;
    const GameModuleLoadExpectation expected{
        .moduleId = "game.tests",
        .buildFingerprint = CurrentGameplayBuildFingerprint(),
        .descriptorRevision = 7,
    };
    REQUIRE(ValidateGeneratedGameplayDescriptorBundle(storage.bundle, expected).HasValue());

    storage.bundle.descriptorRevision = 8;
    const auto mismatch = ValidateGeneratedGameplayDescriptorBundle(storage.bundle, expected);
    REQUIRE(mismatch.HasError());
    REQUIRE(mismatch.ErrorValue().code.Value() == GameplayErrors::IncompatibleGameModule.code.Value());
}

TEST_CASE("generated gameplay bundle validation rejects incomplete bindings and bounded diagnostics") {
    ValidBundleStorage storage;
    const GameModuleLoadExpectation expected{
        .moduleId = "game.tests",
        .buildFingerprint = CurrentGameplayBuildFingerprint(),
        .descriptorRevision = 7,
    };

    storage.binding.factory.create = nullptr;
    const auto invalidBinding = ValidateGeneratedGameplayDescriptorBundle(storage.bundle, expected);
    REQUIRE(invalidBinding.HasError());
    REQUIRE(invalidBinding.ErrorValue().code.Value() == GameplayErrors::InvalidGeneratedDescriptorBundle.code.Value());

    storage.binding.factory.create = &CreateTestBehavior;
    const GeneratedDescriptorDiagnostic diagnostic{.code = "gameplay.codegen.failure", .message = "annotation rejected"};
    storage.bundle.diagnostics = &diagnostic;
    storage.bundle.diagnosticCount = 1;
    const auto diagnostics = ValidateGeneratedGameplayDescriptorBundle(storage.bundle, expected);
    REQUIRE(diagnostics.HasError());
    REQUIRE(diagnostics.ErrorValue().code.Value() == GameplayErrors::GeneratedDescriptorDiagnosticsPresent.code.Value());
}
