#include "AnimationTestFixtures.h"
#include "Horo/Animation/AnimationCompression.h"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <limits>
#include <utility>

namespace Horo::Animation {
    namespace {
        using Test::Asset;
        using Test::Id;

        SkeletonAsset CompressionSkeleton() {
            SkeletonAssetData data{.skeleton = Asset<SkeletonId>(51),
                                   .joints = {{.id = Id<JointId>(1), .name = "root"},
                                              {.id = Id<JointId>(2), .parent = Id<JointId>(1), .name = "child"}}};
            auto created = SkeletonAsset::Create(std::move(data));
            REQUIRE(created.HasValue());
            return std::move(created).Value();
        }

        AnimationTransformKey CompressionKey(const std::int64_t time, const float translation,
                                             const AnimationInterpolation interpolation = AnimationInterpolation::Linear) {
            Math::Transform transform{};
            transform.translation.x = translation;
            return {.time = {time}, .transform = transform, .interpolation = interpolation};
        }

        AnimationClipAsset SourceClip(const SkeletonAsset &skeleton,
                                      const AnimationCompressionScheme scheme = AnimationCompressionScheme::None) {
            AnimationClipData data{.descriptor = {.id = Asset<AnimationClipId>(52),
                                                  .generation = Id<AnimationClipGeneration>(53),
                                                  .skeleton = Asset<SkeletonId>(51),
                                                  .skeletonGeneration = Id<SkeletonAssetGeneration>(54),
                                                  .duration = {100},
                                                  .sampleRate = {30, 1},
                                                  .wrapMode = AnimationWrapMode::Loop,
                                                  .compression = scheme},
                                   .tracks = {{.joint = Id<JointId>(2),
                                               .keys = {CompressionKey(0, 0.0F), CompressionKey(25, 2.5F), CompressionKey(50, 5.0F),
                                                        CompressionKey(75, 7.5F), CompressionKey(100, 10.0F)}}}};
            auto created = AnimationClipAsset::Create(std::move(data), skeleton);
            REQUIRE(created.HasValue());
            return std::move(created).Value();
        }

        AnimationCompressionCookContext CookContext() {
            return {.clipGeneration = Id<AnimationClipGeneration>(53), .skeletonGeneration = Id<SkeletonAssetGeneration>(54)};
        }

        AnimationCompressionCookProfile Profile(const AnimationCompressionCookTier tier) {
            auto profile = GetAnimationCompressionCookProfile(tier);
            REQUIRE(profile.HasValue());
            return profile.Value();
        }

        template <typename Value> void RequireCompressionError(const Result<Value> &result, const ErrorCodeDescriptor &expected) {
            REQUIRE_FALSE(result.HasValue());
            CHECK(result.ErrorValue().domain.Value() == "horo.animation");
            CHECK(result.ErrorValue().code.Value() == expected.code.Value());
        }

        AnimationDecompressionContext DecompressionContext(const CompressedAnimationClipAsset &compressed,
                                                           const std::span<Math::Transform> output) {
            return {.compatibility = compressed.Compatibility(),
                    .sample = {.skeleton = Asset<SkeletonId>(51),
                               .skeletonGeneration = Id<SkeletonAssetGeneration>(54),
                               .clipGeneration = Id<AnimationClipGeneration>(53),
                               .outputPose = output}};
        }

        CompressedAnimationClipAsset BalancedCompressedClip() {
            const auto skeleton = CompressionSkeleton();
            const auto source = SourceClip(skeleton);
            auto cooked = CookAnimationClip(source, skeleton, Profile(AnimationCompressionCookTier::Balanced), CookContext());
            REQUIRE(cooked.HasValue());
            return std::move(cooked).Value();
        }

        CompressedAnimationClipAsset CookAggressive(AnimationClipData data, const SkeletonAsset &skeleton) {
            auto source = AnimationClipAsset::Create(std::move(data), skeleton);
            REQUIRE(source.HasValue());
            auto compressed = CookAnimationClip(source.Value(), skeleton, Profile(AnimationCompressionCookTier::Aggressive), CookContext());
            REQUIRE(compressed.HasValue());
            return std::move(compressed).Value();
        }
    }  // namespace

    TEST_CASE("Compression cook profiles are explicit canonical typed values", "[unit][animation][compression][profile]") {
        const auto lossless = Profile(AnimationCompressionCookTier::Lossless);
        const auto balanced = Profile(AnimationCompressionCookTier::Balanced);
        const auto aggressive = Profile(AnimationCompressionCookTier::Aggressive);
        CHECK(lossless.scheme == AnimationCompressionScheme::None);
        CHECK(balanced.scheme == AnimationCompressionScheme::Linear);
        CHECK(aggressive.scheme == AnimationCompressionScheme::Adaptive);
        CHECK(lossless.id != balanced.id);
        CHECK(balanced.id != aggressive.id);
        RequireCompressionError(GetAnimationCompressionCookProfile(AnimationCompressionCookTier::Count),
                                AnimationErrors::CompressionUnsupported);
    }

    TEST_CASE("Lossless and reduced cooks publish exact compatibility and deterministic statistics",
              "[unit][animation][compression][cook][determinism]") {
        const auto skeleton = CompressionSkeleton();
        const auto source = SourceClip(skeleton);
        auto lossless = CookAnimationClip(source, skeleton, Profile(AnimationCompressionCookTier::Lossless), CookContext());
        REQUIRE(lossless.HasValue());
        CHECK(lossless.Value().Statistics().sourceKeys == 5);
        CHECK(lossless.Value().Statistics().outputKeys == 5);
        CHECK(lossless.Value().Statistics().removedKeys == 0);
        CHECK(lossless.Value().Compatibility().scheme == AnimationCompressionScheme::None);

        const auto balancedProfile = Profile(AnimationCompressionCookTier::Balanced);
        auto first = CookAnimationClip(source, skeleton, balancedProfile, CookContext());
        auto second = CookAnimationClip(source, skeleton, balancedProfile, CookContext());
        REQUIRE(first.HasValue());
        REQUIRE(second.HasValue());
        CHECK(first.Value().Compatibility() == second.Value().Compatibility());
        CHECK(first.Value().Statistics() == second.Value().Statistics());
        CHECK(first.Value().Clip().Data() == second.Value().Clip().Data());
        CHECK(first.Value().Statistics().sourceKeys == 5);
        CHECK(first.Value().Statistics().outputKeys == 3);
        CHECK(first.Value().Statistics().removedKeys == 2);
        CHECK(first.Value().Clip().Data().descriptor.compression == AnimationCompressionScheme::Linear);
    }

    TEST_CASE("Adaptive key reduction respects transform error and interpolation semantics", "[unit][animation][compression][threshold]") {
        const auto skeleton = CompressionSkeleton();
        auto data = SourceClip(skeleton).Data();
        data.tracks[0].keys[2].transform.translation.x = 6.0F;
        data.tracks[0].keys[1].interpolation = AnimationInterpolation::Step;
        const auto compressed = CookAggressive(std::move(data), skeleton);
        const auto &keys = compressed.Clip().Data().tracks[0].keys;
        CHECK(keys.front().time == AnimationTime{0});
        CHECK(keys.back().time == AnimationTime{100});
        CHECK(std::ranges::find(keys, AnimationTime{25}, &AnimationTransformKey::time) != keys.end());
        CHECK(std::ranges::find(keys, AnimationTime{50}, &AnimationTransformKey::time) != keys.end());
    }

    TEST_CASE("Adaptive key reduction subdivides at the greatest normalized error", "[unit][animation][compression][adaptive]") {
        const auto skeleton = CompressionSkeleton();
        auto data = SourceClip(skeleton).Data();
        data.tracks[0].keys = {CompressionKey(0, 0.0F), CompressionKey(25, 5.0F), CompressionKey(50, 10.0F), CompressionKey(100, 0.0F)};
        const auto compressed = CookAggressive(std::move(data), skeleton);
        const auto &keys = compressed.Clip().Data().tracks[0].keys;
        REQUIRE(keys.size() == 3);
        CHECK(keys[0].time == AnimationTime{0});
        CHECK(keys[1].time == AnimationTime{50});
        CHECK(keys[2].time == AnimationTime{100});
    }

    TEST_CASE("Compression cook fails closed for lifecycle version profile budget and stale bindings",
              "[unit][animation][compression][failure][lifecycle]") {
        const auto skeleton = CompressionSkeleton();
        const auto source = SourceClip(skeleton);
        auto profile = Profile(AnimationCompressionCookTier::Balanced);

        auto cancelled = CookContext();
        cancelled.admission = AnimationClipAdmissionState::CancellationRequested;
        RequireCompressionError(CookAnimationClip(source, skeleton, profile, cancelled), AnimationErrors::CompressionOperationCancelled);
        auto shutdown = CookContext();
        shutdown.admission = AnimationClipAdmissionState::ShuttingDown;
        RequireCompressionError(CookAnimationClip(source, skeleton, profile, shutdown), AnimationErrors::CompressionAdmissionRejected);

        profile.contractVersion.major = 9;
        RequireCompressionError(CookAnimationClip(source, skeleton, profile, CookContext()),
                                AnimationErrors::CompressionVersionUnsupported);
        profile = Profile(AnimationCompressionCookTier::Balanced);
        profile.thresholds.translation = std::numeric_limits<float>::infinity();
        RequireCompressionError(CookAnimationClip(source, skeleton, profile, CookContext()), AnimationErrors::CompressionProfileMalformed);

        profile = Profile(AnimationCompressionCookTier::Balanced);
        profile.limits.maximumSourceKeys = 4;
        RequireCompressionError(CookAnimationClip(source, skeleton, profile, CookContext()), AnimationErrors::CompressionBudgetExceeded);
        auto stale = CookContext();
        stale.clipGeneration = Id<AnimationClipGeneration>(99);
        RequireCompressionError(CookAnimationClip(source, skeleton, Profile(AnimationCompressionCookTier::Balanced), stale),
                                AnimationErrors::CompressionBindingStale);
        const auto compressedSource = SourceClip(skeleton, AnimationCompressionScheme::Linear);
        RequireCompressionError(CookAnimationClip(compressedSource, skeleton, Profile(AnimationCompressionCookTier::Balanced),
                                                  CookContext()),
                                AnimationErrors::CompressionUnsupported);
    }

    TEST_CASE("Compression profiles reject malformed identities typed values thresholds and hard limits",
              "[unit][animation][compression][profile][failure]") {
        const auto skeleton = CompressionSkeleton();
        const auto source = SourceClip(skeleton);
        auto profile = Profile(AnimationCompressionCookTier::Balanced);
        profile.id = {};
        RequireCompressionError(CookAnimationClip(source, skeleton, profile, CookContext()), AnimationErrors::IdentityInvalid);

        profile = Profile(AnimationCompressionCookTier::Balanced);
        profile.tier = AnimationCompressionCookTier::Count;
        RequireCompressionError(CookAnimationClip(source, skeleton, profile, CookContext()), AnimationErrors::CompressionUnsupported);
        profile = Profile(AnimationCompressionCookTier::Balanced);
        profile.scheme = AnimationCompressionScheme::Count;
        RequireCompressionError(CookAnimationClip(source, skeleton, profile, CookContext()), AnimationErrors::CompressionUnsupported);

        profile = Profile(AnimationCompressionCookTier::Balanced);
        profile.thresholds.rotation = -0.1F;
        RequireCompressionError(CookAnimationClip(source, skeleton, profile, CookContext()), AnimationErrors::CompressionProfileMalformed);
        profile = Profile(AnimationCompressionCookTier::Lossless);
        profile.thresholds.scale = 0.1F;
        RequireCompressionError(CookAnimationClip(source, skeleton, profile, CookContext()), AnimationErrors::CompressionProfileMalformed);

        profile = Profile(AnimationCompressionCookTier::Balanced);
        profile.limits.maximumSourceKeys = 0;
        RequireCompressionError(CookAnimationClip(source, skeleton, profile, CookContext()), AnimationErrors::CompressionProfileMalformed);
        profile = Profile(AnimationCompressionCookTier::Balanced);
        profile.limits.maximumOutputKeys = AnimationCompressionHardLimits::OutputKeys + 1;
        RequireCompressionError(CookAnimationClip(source, skeleton, profile, CookContext()), AnimationErrors::CompressionProfileMalformed);
        profile = Profile(AnimationCompressionCookTier::Balanced);
        profile.limits.maximumErrorEvaluations = AnimationCompressionHardLimits::ErrorEvaluations + 1;
        RequireCompressionError(CookAnimationClip(source, skeleton, profile, CookContext()), AnimationErrors::CompressionProfileMalformed);
    }

    TEST_CASE("Compression cook enforces reduction work output and exact skeleton binding budgets",
              "[unit][animation][compression][budget][binding]") {
        const auto skeleton = CompressionSkeleton();
        const auto source = SourceClip(skeleton);
        auto profile = Profile(AnimationCompressionCookTier::Balanced);
        profile.limits.maximumErrorEvaluations = 1;
        RequireCompressionError(CookAnimationClip(source, skeleton, profile, CookContext()), AnimationErrors::CompressionBudgetExceeded);

        profile = Profile(AnimationCompressionCookTier::Balanced);
        profile.limits.maximumOutputKeys = 2;
        RequireCompressionError(CookAnimationClip(source, skeleton, profile, CookContext()), AnimationErrors::CompressionBudgetExceeded);

        auto invalidOwner = CookContext();
        invalidOwner.skeletonGeneration = {};
        RequireCompressionError(CookAnimationClip(source, skeleton, Profile(AnimationCompressionCookTier::Balanced), invalidOwner),
                                AnimationErrors::IdentityInvalid);

        SkeletonAssetData otherData{.skeleton = Asset<SkeletonId>(61), .joints = {{.id = Id<JointId>(1), .name = "root"}}};
        auto otherSkeleton = SkeletonAsset::Create(std::move(otherData));
        REQUIRE(otherSkeleton.HasValue());
        RequireCompressionError(CookAnimationClip(source, otherSkeleton.Value(), Profile(AnimationCompressionCookTier::Balanced),
                                                  CookContext()),
                                AnimationErrors::ClipSkeletonMismatch);
    }

    TEST_CASE("Compression reload preserves source and profile domain while permitting new immutable generations",
              "[unit][animation][compression][reload]") {
        const auto skeleton = CompressionSkeleton();
        const auto source = SourceClip(skeleton);
        const auto profile = Profile(AnimationCompressionCookTier::Balanced);
        auto current = CookAnimationClip(source, skeleton, profile, CookContext());
        REQUIRE(current.HasValue());

        auto accepted = CookContext();
        accepted.replacing = current.Value().Compatibility();
        REQUIRE(CookAnimationClip(source, skeleton, profile, accepted).HasValue());

        auto rejected = CookContext();
        rejected.replacing = current.Value().Compatibility();
        rejected.replacing->profile = Profile(AnimationCompressionCookTier::Aggressive).id;
        RequireCompressionError(CookAnimationClip(source, skeleton, profile, rejected), AnimationErrors::CompressionReloadMismatch);
    }

    TEST_CASE("Frame-hot decompression is allocation-free bounded and rejects stale compatibility before writes",
              "[unit][animation][compression][decompress][allocation]") {
        const auto compressed = BalancedCompressedClip();
        std::array<Math::Transform, 2> output{};
        auto context = DecompressionContext(compressed, output);

        const auto allocationsBefore = HoroAnimationTestAllocationCount();
        const auto sampled = compressed.Sample({50}, context);
        const auto allocationsAfter = HoroAnimationTestAllocationCount();
        REQUIRE(sampled.HasValue());
        CHECK(allocationsAfter == allocationsBefore);
        CHECK(output[1].translation.x == 5.0F);

        output[1].translation.x = 77.0F;
        context.budget.maximumTracks = 0;
        RequireCompressionError(compressed.Sample({50}, context), AnimationErrors::CompressionBudgetExceeded);
        CHECK(output[1].translation.x == 77.0F);

        context = DecompressionContext(compressed, output);
        context.compatibility.clipGeneration = Id<AnimationClipGeneration>(88);
        RequireCompressionError(compressed.Sample({50}, context), AnimationErrors::CompressionBindingStale);
        CHECK(output[1].translation.x == 77.0F);

        context = DecompressionContext(compressed, output);
        context.sample.admission = AnimationClipAdmissionState::ShuttingDown;
        RequireCompressionError(compressed.Sample({50}, context), AnimationErrors::CompressionAdmissionRejected);
        CHECK(output[1].translation.x == 77.0F);
    }

    TEST_CASE("Decompression validates contract profile representation search budget and output transactionally",
              "[unit][animation][compression][decompress][failure]") {
        const auto compressed = BalancedCompressedClip();
        std::array<Math::Transform, 2> output{};

        auto context = DecompressionContext(compressed, output);
        context.compatibility.contractVersion.major = 2;
        RequireCompressionError(compressed.Sample({50}, context), AnimationErrors::CompressionVersionUnsupported);
        context = DecompressionContext(compressed, output);
        context.compatibility.profile = Profile(AnimationCompressionCookTier::Aggressive).id;
        RequireCompressionError(compressed.Sample({50}, context), AnimationErrors::CompressionUnsupported);
        context = DecompressionContext(compressed, output);
        context.compatibility.scheme = AnimationCompressionScheme::Adaptive;
        RequireCompressionError(compressed.Sample({50}, context), AnimationErrors::CompressionUnsupported);

        context = DecompressionContext(compressed, output);
        context.budget.maximumKeySearchSteps = 1;
        RequireCompressionError(compressed.Sample({50}, context), AnimationErrors::CompressionBudgetExceeded);
        context = DecompressionContext(compressed, output);
        context.budget.maximumKeySearchSteps = AnimationCompressionHardLimits::KeySearchSteps + 1;
        RequireCompressionError(compressed.Sample({50}, context), AnimationErrors::CompressionBudgetExceeded);

        std::array<Math::Transform, 1> shortOutput{};
        context = DecompressionContext(compressed, shortOutput);
        RequireCompressionError(compressed.Sample({50}, context), AnimationErrors::ClipLimitExceeded);
        context = DecompressionContext(compressed, output);
        context.sample.admission = AnimationClipAdmissionState::CancellationRequested;
        RequireCompressionError(compressed.Sample({50}, context), AnimationErrors::CompressionOperationCancelled);
    }
}  // namespace Horo::Animation
