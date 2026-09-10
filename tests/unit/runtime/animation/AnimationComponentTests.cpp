#include "AnimationTestFixtures.h"
#include "Horo/Animation/AnimationComponents.h"

#include <catch2/catch_test_macros.hpp>

namespace Horo::Animation {
    namespace {
        using Test::Asset;
        using Test::Id;

        AnimationInstanceHandle Instance(const std::uint32_t generation = 4) {
            return {Id<AnimationRuntimeId>(1), Id<AnimationComponentId>(2), {3, generation}};
        }

        PoseHandle Pose(const AnimationInstanceHandle &instance, const std::uint32_t slot, const std::uint64_t poseGeneration) {
            return {instance, {slot, 1}, Id<PoseGeneration>(poseGeneration)};
        }

        AnimationAuthoringComponent AuthoredClip() {
            return {.component = Id<AnimationComponentId>(2),
                    .skeleton = Asset<SkeletonId>(1),
                    .source = {.kind = AnimationSourceKind::Clip, .clip = Asset<AnimationClipId>(2)},
                    .retarget = Asset<RetargetProfileId>(3),
                    .rootMotion = RootMotionPolicy::RequestCharacterMovement,
                    .startsPlaying = true};
        }

        RuntimeAnimationComponent RuntimeFor(const AnimationAuthoringComponent &authoring) {
            const auto instance = Instance();
            return {.instance = instance,
                    .skeleton = authoring.skeleton,
                    .source = authoring.source,
                    .domain = AnimationEvaluationDomain::AuthoritativeSimulation,
                    .state = AnimationPlaybackState::Playing,
                    .previousPose = Pose(instance, 5, 10),
                    .currentPose = Pose(instance, 6, 11)};
        }

        template <typename Value> void RequireFailure(const Result<Value> &result, const ErrorCodeDescriptor &expected) {
            REQUIRE(result.HasError());
            CHECK(result.ErrorValue().code.Value() == expected.code.Value());
        }
    }  // namespace

    TEST_CASE("Animation authoring components are inert typed clip or graph bindings", "[unit][animation][component]") {
        const auto clip = AuthoredClip();
        REQUIRE(ValidateAnimationAuthoringComponent(clip).HasValue());

        auto graph = clip;
        graph.source = {.kind = AnimationSourceKind::Graph, .graph = Asset<AnimationGraphId>(4)};
        graph.retarget = {};
        graph.rootMotion = RootMotionPolicy::Ignore;
        REQUIRE(ValidateAnimationAuthoringComponent(graph).HasValue());
        CHECK(graph.component == clip.component);
        CHECK_FALSE(graph.retarget.IsValid());
    }

    TEST_CASE("Animation authoring validation rejects version identity and source skew", "[unit][animation][component]") {
        auto component = AuthoredClip();
        component.contractVersion.patch = 1;
        RequireFailure(ValidateAnimationAuthoringComponent(component), AnimationErrors::ContractVersionUnsupported);

        component = AuthoredClip();
        component.component = {};
        RequireFailure(ValidateAnimationAuthoringComponent(component), AnimationErrors::IdentityInvalid);
        component = AuthoredClip();
        component.skeleton = {};
        RequireFailure(ValidateAnimationAuthoringComponent(component), AnimationErrors::IdentityInvalid);

        component = AuthoredClip();
        component.source.graph = Asset<AnimationGraphId>(5);
        RequireFailure(ValidateAnimationAuthoringComponent(component), AnimationErrors::ComponentInvalid);
        component.source = {};
        RequireFailure(ValidateAnimationAuthoringComponent(component), AnimationErrors::ComponentInvalid);
        component = AuthoredClip();
        component.source.kind = static_cast<AnimationSourceKind>(255);
        RequireFailure(ValidateAnimationAuthoringComponent(component), AnimationErrors::ComponentInvalid);
        component = AuthoredClip();
        component.rootMotion = RootMotionPolicy::Count;
        RequireFailure(ValidateAnimationAuthoringComponent(component), AnimationErrors::ComponentInvalid);
    }

    TEST_CASE("Runtime animation component publishes coherent immutable pose generations", "[unit][animation][component]") {
        const auto authoring = AuthoredClip();
        const auto runtime = RuntimeFor(authoring);
        REQUIRE(ValidateRuntimeAnimationComponent(runtime, authoring).HasValue());
        CHECK(runtime.previousPose.generation == Id<PoseGeneration>(10));
        CHECK(runtime.currentPose.generation == Id<PoseGeneration>(11));
        CHECK(runtime.instance.component == authoring.component);
    }

    TEST_CASE("Runtime component rejects malformed unknown and version-skewed state", "[unit][animation][component]") {
        const auto authoring = AuthoredClip();
        auto runtime = RuntimeFor(authoring);
        runtime.contractVersion.major = 2;
        RequireFailure(ValidateRuntimeAnimationComponent(runtime, authoring), AnimationErrors::ContractVersionUnsupported);

        runtime = RuntimeFor(authoring);
        runtime.instance = {};
        RequireFailure(ValidateRuntimeAnimationComponent(runtime, authoring), AnimationErrors::HandleMalformed);
        runtime = RuntimeFor(authoring);
        runtime.previousPose = {};
        RequireFailure(ValidateRuntimeAnimationComponent(runtime, authoring), AnimationErrors::HandleMalformed);
        runtime = RuntimeFor(authoring);
        runtime.domain = AnimationEvaluationDomain::Count;
        RequireFailure(ValidateRuntimeAnimationComponent(runtime, authoring), AnimationErrors::ComponentInvalid);
        runtime = RuntimeFor(authoring);
        runtime.state = static_cast<AnimationPlaybackState>(255);
        RequireFailure(ValidateRuntimeAnimationComponent(runtime, authoring), AnimationErrors::ComponentInvalid);
    }

    TEST_CASE("Runtime component rejects foreign bindings instances and stale pose order", "[unit][animation][component]") {
        const auto authoring = AuthoredClip();
        auto runtime = RuntimeFor(authoring);
        runtime.skeleton = Asset<SkeletonId>(9);
        RequireFailure(ValidateRuntimeAnimationComponent(runtime, authoring), AnimationErrors::ComponentBindingMismatch);
        runtime = RuntimeFor(authoring);
        runtime.source = {.kind = AnimationSourceKind::Graph, .graph = Asset<AnimationGraphId>(9)};
        RequireFailure(ValidateRuntimeAnimationComponent(runtime, authoring), AnimationErrors::ComponentBindingMismatch);

        runtime = RuntimeFor(authoring);
        runtime.previousPose.instance = Instance(40);
        RequireFailure(ValidateRuntimeAnimationComponent(runtime, authoring), AnimationErrors::HandleOwnerMismatch);
        runtime = RuntimeFor(authoring);
        runtime.previousPose.generation = Id<PoseGeneration>(12);
        RequireFailure(ValidateRuntimeAnimationComponent(runtime, authoring), AnimationErrors::HandleStale);
    }

    TEST_CASE("Stopped and failed runtime projections retain only generation-checked immutable poses", "[unit][animation][component]") {
        const auto authoring = AuthoredClip();
        for (const auto state : {AnimationPlaybackState::Ready, AnimationPlaybackState::Paused, AnimationPlaybackState::Stopped,
                                 AnimationPlaybackState::Failed}) {
            auto runtime = RuntimeFor(authoring);
            runtime.state = state;
            CAPTURE(static_cast<int>(state));
            REQUIRE(ValidateRuntimeAnimationComponent(runtime, authoring).HasValue());
        }
    }
}  // namespace Horo::Animation
