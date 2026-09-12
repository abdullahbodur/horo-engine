#include "Horo/Physics/PhysicsSceneActivation.h"
#include "PhysicsTestUtils.h"

#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <utility>

namespace Horo::Physics {
    namespace {
        [[nodiscard]] Runtime::RuntimeSceneDefinition Definition(const std::uint64_t revision = 1) {
            Runtime::SceneDefinitionBuilder builder{Runtime::SceneDefinitionId{19}, Runtime::SceneDefinitionRevision{revision}};
            Runtime::RuntimeEntityDefinition entity;
            entity.object = Runtime::SceneObjectId{1};
            builder.Add(std::move(entity));
            auto definition = std::move(builder).Build();
            REQUIRE(definition.HasValue());
            return std::move(definition).Value();
        }

        [[nodiscard]] PhysicsSceneActivationSettings Settings() {
            auto physics = PhysicsWorldSettings::Capture({});
            auto character = Character::CharacterWorldSettings::Capture({});
            REQUIRE(physics.HasValue());
            REQUIRE(character.HasValue());
            return {physics.Value(), character.Value()};
        }

        [[nodiscard]] Runtime::FrameContext Context(const CancellationToken &cancellation) {
            return {1, {}, 0.0, 0, {}, false, cancellation};
        }

        /** @brief Creates the null runtime required by scene activation tests. */
        [[nodiscard]] std::unique_ptr<PhysicsRuntime> RequireRuntime() {
            auto runtime = PhysicsRuntime::Create(PhysicsRuntimeMode::Null);
            REQUIRE(runtime.HasValue());
            return std::move(runtime).Value();
        }

        /** @brief Instantiates one runtime scene from a validated test definition. */
        [[nodiscard]] std::unique_ptr<Runtime::RuntimeScene> RequireScene(const Runtime::RuntimeSceneDefinition &definition) {
            auto scene = Runtime::RuntimeScene::Create(definition, Runtime::SceneRuntimeId{1});
            REQUIRE(scene.HasValue());
            return std::move(scene).Value();
        }

        TEST_CASE("Physics scene participant recreation preserves process-runtime world identity monotonicity",
                  "[physics][scene][activation][identity]") {
            auto runtime = RequireRuntime();
            PhysicsSceneActivationAuthority authority;
            const auto definition = Definition();
            auto scene = RequireScene(definition);

            PhysicsSceneActivationParticipant first{*runtime, authority, Settings()};
            auto firstCandidate = first.Prepare(definition, scene->View());
            REQUIRE(firstCandidate.HasValue());
            PhysicsSceneActivationParticipant replacement{*runtime, authority, Settings()};
            auto replacementCandidate = replacement.Prepare(definition, scene->View());
            REQUIRE(replacementCandidate.HasValue());
            REQUIRE(firstCandidate.Value()->ValidatePublication().HasValue());
            REQUIRE(replacementCandidate.Value()->ValidatePublication().HasValue());

            replacementCandidate.Value()->Shutdown();
            firstCandidate.Value()->Shutdown();
        }

        TEST_CASE("Physics scene candidate rejects authoritative generation changes before publication",
                  "[physics][scene][activation][generation]") {
            auto runtime = RequireRuntime();
            PhysicsSceneActivationAuthority authority;
            const auto definition = Definition();
            auto scene = RequireScene(definition);
            PhysicsSceneActivationParticipant participant{*runtime, authority, Settings()};
            auto candidate = participant.Prepare(definition, scene->View());
            REQUIRE(candidate.HasValue());

            REQUIRE(authority.AdvanceOriginGeneration().HasValue());
            Test::RequireError(candidate.Value()->ValidatePublication(), PhysicsErrors::QuerySnapshotStale);
            candidate.Value()->Shutdown();
        }

        TEST_CASE("Runtime scene replaces and tears down real Physics aggregate candidates", "[physics][scene][activation][replacement]") {
            auto runtime = RequireRuntime();
            PhysicsSceneActivationAuthority authority;
            Runtime::RuntimeSceneService scenes;
            auto participant = std::make_unique<PhysicsSceneActivationParticipant>(*runtime, authority, Settings());
            REQUIRE(scenes.AddActivationParticipant(std::move(participant)).HasValue());
            CancellationSource cancellation;
            REQUIRE(scenes.Startup(cancellation.Token()).HasValue());

            REQUIRE(scenes.QueuePreparation(Definition()).HasValue());
            REQUIRE(scenes.OnPhase(Runtime::RuntimePhase::CommitDeferredLifecycleChanges, Context(cancellation.Token())).HasValue());
            REQUIRE(scenes.ActiveScene().has_value());
            const Runtime::SceneRuntimeId first = scenes.ActiveScene()->RuntimeId();

            REQUIRE(scenes.QueuePreparation(Definition(2)).HasValue());
            REQUIRE(authority.AdvanceCollisionFilterGeneration().HasValue());
            REQUIRE(scenes.OnPhase(Runtime::RuntimePhase::CommitDeferredLifecycleChanges, Context(cancellation.Token())).HasValue());
            REQUIRE(scenes.ActiveScene()->RuntimeId() == first);
            const auto stale = scenes.TakeOperationError();
            REQUIRE(stale.has_value());
            REQUIRE(stale->code.Value() == PhysicsErrors::QuerySnapshotStale.code.Value());

            REQUIRE(scenes.QueuePreparation(Definition(3)).HasValue());
            REQUIRE(scenes.OnPhase(Runtime::RuntimePhase::CommitDeferredLifecycleChanges, Context(cancellation.Token())).HasValue());
            REQUIRE(scenes.ActiveScene()->RuntimeId() != first);
            REQUIRE_FALSE(scenes.TakeOperationError().has_value());

            REQUIRE(scenes.QueueUnload().HasValue());
            REQUIRE(scenes.OnPhase(Runtime::RuntimePhase::CommitDeferredLifecycleChanges, Context(cancellation.Token())).HasValue());
            REQUIRE_FALSE(scenes.ActiveScene().has_value());
        }
    }  // namespace
}  // namespace Horo::Physics
