#include "Horo/Cinematic/CinematicErrors.h"
#include "Horo/Cinematic/TransformTrack.h"
#include "support/AllocationProbe.h"

#include <array>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <utility>
#include <vector>

namespace Horo::Cinematic {
    namespace {
        struct CurveStorage final {
            std::array<std::array<ScalarCurveKey, 2>, 10> channels;
        };

        [[nodiscard]] constexpr TransformBindingId Binding(const std::uint64_t value, const std::uint32_t generation = 1) noexcept {
            return {value, generation};
        }

        [[nodiscard]] constexpr TrackId Track(const std::uint64_t value) noexcept {
            return {value, 1};
        }

        [[nodiscard]] std::array<float, 10> Components(const Math::Transform &value) {
            return {value.translation.x, value.translation.y, value.translation.z, value.rotation.x, value.rotation.y,
                    value.rotation.z,    value.rotation.w,    value.scale.x,       value.scale.y,    value.scale.z};
        }

        [[nodiscard]] CurveStorage Curves(const Math::Transform &from, const Math::Transform &to) {
            CurveStorage storage{};
            const auto first = Components(from);
            const auto second = Components(to);
            for (std::size_t channel = 0; channel < storage.channels.size(); ++channel) {
                storage.channels[channel][0] = {.time = 0, .value = first[channel]};
                storage.channels[channel][1] = {.time = 10, .value = second[channel]};
            }
            return storage;
        }

        [[nodiscard]] ScalarCurveView View(const std::array<ScalarCurveKey, 2> &keys) {
            auto curve = ScalarCurveView::Create(keys);
            REQUIRE(curve.HasValue());
            return curve.Value();
        }

        [[nodiscard]] TransformCurveSet Views(const CurveStorage &storage) {
            return {{{View(storage.channels[0]), View(storage.channels[1]), View(storage.channels[2])}},
                    {{View(storage.channels[3]), View(storage.channels[4]), View(storage.channels[5]), View(storage.channels[6])}},
                    {{View(storage.channels[7]), View(storage.channels[8]), View(storage.channels[9])}}};
        }

        [[nodiscard]] TransformTrackDescriptor Descriptor(const TrackId track, const TransformBindingId binding,
                                                          const std::optional<TransformBindingId> parent,
                                                          const std::optional<Math::WorldCoordinate64> anchor,
                                                          const CurveStorage &storage) {
            return {.track = track, .binding = binding, .parent = parent, .rootAnchor = anchor, .curves = Views(storage)};
        }

        template <typename T> void RequireError(const Result<T> &result, const ErrorCodeDescriptor &expected) {
            REQUIRE(result.HasError());
            CHECK(result.ErrorValue().code.Value() == expected.code.Value());
            CHECK(result.ErrorValue().domain.Value() == expected.domain.Value());
        }

        [[nodiscard]] TransformEvaluationContext Context(const TransformSceneVersion scene, const Math::WorldCoordinate64 origin = {},
                                                         const std::uint64_t originEpoch = 1) {
            return {scene, origin, originEpoch};
        }

        [[nodiscard]] TransformEvaluationPlan RootPlan(const CurveStorage &storage, const TransformSceneVersion scene) {
            const auto root = Binding(1);
            const std::array bindings{TransformBindingSnapshot{root, std::nullopt}};
            const std::array tracks{Descriptor(Track(1), root, std::nullopt, Math::WorldCoordinate64{}, storage)};
            auto plan = TransformEvaluationPlan::Create(scene, tracks, bindings);
            REQUIRE(plan.HasValue());
            return std::move(plan).Value();
        }
    }  // namespace

    TEST_CASE("Transform evaluation is parent-first and independent of seek direction", "[unit][cinematic][transform][hierarchy]") {
        const Math::Transform rootStart{{1.0F, 2.0F, 3.0F}, {}, {1.0F, 1.0F, 1.0F}};
        const Math::Transform rootEnd{{11.0F, 2.0F, 3.0F}, {}, {2.0F, 2.0F, 2.0F}};
        const Math::Transform childStart{{0.0F, 4.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}};
        const Math::Transform childEnd{{0.0F, 8.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}};
        const CurveStorage rootCurves = Curves(rootStart, rootEnd);
        const CurveStorage childCurves = Curves(childStart, childEnd);
        const auto root = Binding(1);
        const auto child = Binding(2);
        const std::array bindings{TransformBindingSnapshot{root, std::nullopt}, TransformBindingSnapshot{child, root}};
        const std::array tracks{Descriptor(Track(20), child, root, std::nullopt, childCurves),
                                Descriptor(Track(10), root, std::nullopt, Math::WorldCoordinate64{}, rootCurves)};
        constexpr TransformSceneVersion scene{7, 11};
        auto plan = TransformEvaluationPlan::Create(scene, tracks, bindings);
        REQUIRE(plan.HasValue());
        std::array<TransformEvaluationValue, 2> output{};

        REQUIRE(plan.Value().Evaluate(10, Context(scene), output).HasValue());
        CHECK(output[0].binding == root);
        CHECK(output[1].binding == child);
        CHECK(output[0].localTransform.translation.x == 11.0F);
        CHECK(output[1].localTransform.translation.y == 8.0F);
        REQUIRE(plan.Value().Evaluate(0, Context(scene), output).HasValue());
        CHECK(output[0].localTransform.translation.x == 1.0F);
        CHECK(output[1].localTransform.translation.y == 4.0F);

        std::array<TransformEvaluationValue, 2> editorPreview{};
        REQUIRE(plan.Value().Evaluate(6, Context(scene), output).HasValue());
        REQUIRE(plan.Value().Evaluate(6, Context(scene), editorPreview).HasValue());
        CHECK(Components(output[0].localTransform) == Components(editorPreview[0].localTransform));
        CHECK(Components(output[1].localTransform) == Components(editorPreview[1].localTransform));
    }

    TEST_CASE("Transform channels preserve authored interpolation and tangents", "[unit][cinematic][transform][tangent]") {
        CurveStorage storage = Curves({{0.0F, 0.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}}, {{10.0F, 0.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}});
        storage.channels[0][0].interpolation = CurveInterpolation::HermiteSpline;
        storage.channels[0][0].tangentOut = {10, 0.0F};
        storage.channels[0][1].tangentIn = {-10, 0.0F};
        constexpr TransformSceneVersion scene{1, 1};
        auto plan = RootPlan(storage, scene);
        std::array<TransformEvaluationValue, 1> output{};

        REQUIRE(plan.Evaluate(2, Context(scene), output).HasValue());
        CHECK(output[0].localTransform.translation.x == Catch::Approx(1.04F));
    }

    TEST_CASE("Transform root rebasing preserves canonical world position and child local values", "[unit][cinematic][transform][origin]") {
        const Math::Transform rootValue{{2.0F, 0.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}};
        const Math::Transform childValue{{3.0F, 0.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}};
        const CurveStorage rootCurves = Curves(rootValue, rootValue);
        const CurveStorage childCurves = Curves(childValue, childValue);
        const auto root = Binding(1);
        const auto child = Binding(2);
        const auto anchor = Math::WorldCoordinate64::FromMillimeters(1'500'000, 0, 0);
        const std::array bindings{TransformBindingSnapshot{root, std::nullopt}, TransformBindingSnapshot{child, root}};
        const std::array tracks{Descriptor(Track(1), root, std::nullopt, anchor, rootCurves),
                                Descriptor(Track(2), child, root, std::nullopt, childCurves)};
        constexpr TransformSceneVersion scene{1, 1};
        auto plan = TransformEvaluationPlan::Create(scene, tracks, bindings);
        REQUIRE(plan.HasValue());
        std::array<TransformEvaluationValue, 2> output{};

        REQUIRE(plan.Value().Evaluate(5, Context(scene, Math::WorldCoordinate64::FromMillimeters(1'000'000, 0, 0), 4), output).HasValue());
        const float reconstructedA = 1'000.0F + output[0].localTransform.translation.x;
        CHECK(output[0].localTransform.translation.x == 502.0F);
        CHECK(output[1].localTransform.translation.x == 3.0F);
        REQUIRE(plan.Value().Evaluate(5, Context(scene, Math::WorldCoordinate64::FromMillimeters(1'400'000, 0, 0), 5), output).HasValue());
        CHECK(output[0].localTransform.translation.x == 102.0F);
        CHECK(reconstructedA == 1'400.0F + output[0].localTransform.translation.x);
        CHECK(output[1].localTransform.translation.x == 3.0F);
    }

    TEST_CASE("Transform activation rejects missing stale cyclic versioned and over-capacity input",
              "[unit][cinematic][transform][validation]") {
        const CurveStorage storage = Curves({}, {});
        constexpr TransformSceneVersion scene{1, 1};
        const auto root = Binding(1);
        auto track = Descriptor(Track(1), root, std::nullopt, Math::WorldCoordinate64{}, storage);
        RequireError(TransformEvaluationPlan::Create(scene, std::span{&track, 1}, {}), CinematicErrors::TransformBindingMissing);
        const std::array staleBindings{TransformBindingSnapshot{Binding(1, 2), std::nullopt}};
        RequireError(TransformEvaluationPlan::Create(scene, std::span{&track, 1}, staleBindings), CinematicErrors::TransformBindingStale);
        track.version = {2, 0};
        const std::array rootBinding{TransformBindingSnapshot{root, std::nullopt}};
        RequireError(TransformEvaluationPlan::Create(scene, std::span{&track, 1}, rootBinding),
                     CinematicErrors::TransformVersionUnsupported);

        const auto second = Binding(2);
        const std::array cycle{TransformBindingSnapshot{root, second}, TransformBindingSnapshot{second, root}};
        track = Descriptor(Track(1), root, second, std::nullopt, storage);
        RequireError(TransformEvaluationPlan::Create(scene, std::span{&track, 1}, cycle), CinematicErrors::TransformHierarchyCycle);

        std::vector<TransformTrackDescriptor> tooMany;
        std::vector<TransformBindingSnapshot> manyBindings;
        tooMany.reserve(MaximumTransformTracks + 1);
        manyBindings.reserve(MaximumTransformTracks + 1);
        for (std::size_t index = 0; index <= MaximumTransformTracks; ++index) {
            const auto binding = Binding(index + 1);
            tooMany.push_back(Descriptor(Track(index + 1), binding, std::nullopt, Math::WorldCoordinate64{}, storage));
            manyBindings.push_back({binding, std::nullopt});
        }
        RequireError(TransformEvaluationPlan::Create(scene, tooMany, manyBindings), CinematicErrors::TransformLimitExceeded);
    }

    TEST_CASE("Transform activation rejects malformed identities topology anchors and duplicates",
              "[unit][cinematic][transform][validation]") {
        const CurveStorage storage = Curves({}, {});
        constexpr TransformSceneVersion scene{1, 1};
        const auto root = Binding(1);
        const auto child = Binding(2);
        const std::array validBindings{TransformBindingSnapshot{root, std::nullopt}, TransformBindingSnapshot{child, root}};

        auto rootTrack = Descriptor(Track(1), root, std::nullopt, Math::WorldCoordinate64{}, storage);
        RequireError(TransformEvaluationPlan::Create({}, std::span{&rootTrack, 1}, validBindings), CinematicErrors::TransformMalformed);

        auto invalidTrack = rootTrack;
        invalidTrack.track = {};
        RequireError(TransformEvaluationPlan::Create(scene, std::span{&invalidTrack, 1}, validBindings),
                     CinematicErrors::TransformMalformed);
        invalidTrack = rootTrack;
        invalidTrack.binding = {};
        RequireError(TransformEvaluationPlan::Create(scene, std::span{&invalidTrack, 1}, validBindings),
                     CinematicErrors::TransformMalformed);
        invalidTrack = rootTrack;
        invalidTrack.rootAnchor.reset();
        RequireError(TransformEvaluationPlan::Create(scene, std::span{&invalidTrack, 1}, validBindings),
                     CinematicErrors::TransformMalformed);

        const std::array duplicateBindings{TransformBindingSnapshot{root, std::nullopt}, TransformBindingSnapshot{root, std::nullopt}};
        RequireError(TransformEvaluationPlan::Create(scene, std::span{&rootTrack, 1}, duplicateBindings),
                     CinematicErrors::TransformMalformed);
        const std::array invalidBindings{TransformBindingSnapshot{{}, std::nullopt}};
        RequireError(TransformEvaluationPlan::Create(scene, std::span{&rootTrack, 1}, invalidBindings),
                     CinematicErrors::TransformMalformed);
        const std::array invalidParent{TransformBindingSnapshot{root, TransformBindingId{}}};
        RequireError(TransformEvaluationPlan::Create(scene, std::span{&rootTrack, 1}, invalidParent), CinematicErrors::TransformMalformed);

        auto childTrack = Descriptor(Track(2), child, Binding(9), std::nullopt, storage);
        RequireError(TransformEvaluationPlan::Create(scene, std::span{&childTrack, 1}, validBindings), CinematicErrors::TransformMalformed);
        childTrack.parent = Binding(1, 2);
        RequireError(TransformEvaluationPlan::Create(scene, std::span{&childTrack, 1}, validBindings),
                     CinematicErrors::TransformBindingStale);
        childTrack = Descriptor(Track(2), child, root, Math::WorldCoordinate64{}, storage);
        RequireError(TransformEvaluationPlan::Create(scene, std::span{&childTrack, 1}, validBindings), CinematicErrors::TransformMalformed);

        childTrack = Descriptor(rootTrack.track, child, root, std::nullopt, storage);
        const std::array duplicateTrackIds{rootTrack, childTrack};
        RequireError(TransformEvaluationPlan::Create(scene, duplicateTrackIds, validBindings), CinematicErrors::TransformMalformed);
        childTrack.track = Track(2);
        childTrack.binding = root;
        childTrack.parent.reset();
        childTrack.rootAnchor = Math::WorldCoordinate64{};
        const std::array duplicateTrackBindings{rootTrack, childTrack};
        RequireError(TransformEvaluationPlan::Create(scene, duplicateTrackBindings, validBindings), CinematicErrors::TransformMalformed);
    }

    TEST_CASE("Transform evaluation fences scene replacement bounds work and allocates nothing",
              "[unit][cinematic][transform][lifecycle][allocation]") {
        const CurveStorage storage = Curves({}, {});
        const auto root = Binding(1);
        const std::array bindings{TransformBindingSnapshot{root, std::nullopt}};
        const std::array tracks{Descriptor(Track(1), root, std::nullopt, Math::WorldCoordinate64{}, storage)};
        constexpr TransformSceneVersion original{5, 8};
        auto plan = TransformEvaluationPlan::Create(original, tracks, bindings);
        REQUIRE(plan.HasValue());
        std::array<TransformEvaluationValue, 1> output{};

        RequireError(plan.Value().Evaluate(5, Context({6, 1}), output), CinematicErrors::TransformBindingStale);
        RequireError(plan.Value().Evaluate(5, Context(original), {}), CinematicErrors::TransformLimitExceeded);
        RequireError(plan.Value().Evaluate(5, Context(original, {}, 0), output), CinematicErrors::TransformMalformed);
        REQUIRE(plan.Value().Evaluate(5, Context(original), output).HasValue());
        const std::size_t before = Tests::AllocationProbe::Count();
        for (std::size_t sample = 0; sample < 10'000; ++sample) {
            auto evaluated = plan.Value().Evaluate(static_cast<CurveTime>(sample % 11), Context(original), output);
            REQUIRE(evaluated.HasValue());
        }
        CHECK(Tests::AllocationProbe::Count() == before);

        const auto replacementRoot = Binding(1, 2);
        const auto replacementChild = Binding(2, 2);
        const CurveStorage childStorage = Curves({}, {});
        const std::array replacementBindings{TransformBindingSnapshot{replacementChild, replacementRoot},
                                             TransformBindingSnapshot{replacementRoot, std::nullopt}};
        const std::array replacementTracks{Descriptor(Track(2), replacementChild, replacementRoot, std::nullopt, childStorage),
                                           Descriptor(Track(1), replacementRoot, std::nullopt, Math::WorldCoordinate64{}, storage)};
        constexpr TransformSceneVersion replacement{6, 1};
        auto replacementPlan = TransformEvaluationPlan::Create(replacement, replacementTracks, replacementBindings);
        REQUIRE(replacementPlan.HasValue());
        std::array<TransformEvaluationValue, 2> replacementOutput{};
        REQUIRE(replacementPlan.Value().Evaluate(5, Context(replacement), replacementOutput).HasValue());
        CHECK(replacementOutput[0].binding == replacementRoot);
        CHECK(replacementOutput[1].binding == replacementChild);
    }

    TEST_CASE("Transform sampling rejects a degenerate quaternion and keeps stable error identities",
              "[unit][cinematic][transform][errors]") {
        Math::Transform invalid{};
        invalid.rotation = {0.0F, 0.0F, 0.0F, 0.0F};
        const CurveStorage storage = Curves(invalid, invalid);
        constexpr TransformSceneVersion scene{1, 1};
        auto plan = RootPlan(storage, scene);
        std::array<TransformEvaluationValue, 1> output{};
        RequireError(plan.Evaluate(0, Context(scene), output), CinematicErrors::TransformSampleInvalid);

        CHECK(CinematicErrors::TransformVersionUnsupported.code.Value() == "cinematic.transform.version_unsupported");
        CHECK(CinematicErrors::TransformMalformed.code.Value() == "cinematic.transform.malformed");
        CHECK(CinematicErrors::TransformBindingMissing.code.Value() == "cinematic.transform.binding_missing");
        CHECK(CinematicErrors::TransformBindingStale.code.Value() == "cinematic.transform.binding_stale");
        CHECK(CinematicErrors::TransformHierarchyCycle.code.Value() == "cinematic.transform.hierarchy_cycle");
        CHECK(CinematicErrors::TransformLimitExceeded.code.Value() == "cinematic.transform.limit_exceeded");
        CHECK(CinematicErrors::TransformSampleInvalid.code.Value() == "cinematic.transform.sample_invalid");
    }
}  // namespace Horo::Cinematic
