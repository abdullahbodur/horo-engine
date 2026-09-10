#include "Horo/Cinematic/CinematicErrors.h"
#include "Horo/Cinematic/TransformTrack.h"

#include <array>
#include <atomic>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <limits>
#include <memory>
#include <new>

namespace {
    std::atomic<std::size_t> gTransformTrackTestAllocations{};
}

void *operator new(const std::size_t size) {
    gTransformTrackTestAllocations.fetch_add(1, std::memory_order_relaxed);
    if (void *const memory = std::malloc(size); memory != nullptr)
        return memory;
    throw std::bad_alloc{};
}

void operator delete(void *memory) noexcept {
    std::free(memory);
}

void operator delete(void *memory, std::size_t) noexcept {
    std::free(memory);
}

namespace Horo::Cinematic {
    namespace {
        using Catch::Approx;
        using ChannelKeys = std::array<ScalarCurveKey, 2>;
        using TrackKeyStorage = std::array<ChannelKeys, 10>;

        template <typename T> void RequireError(const Result<T> &result, const ErrorCodeDescriptor &descriptor) {
            REQUIRE(result.HasError());
            CHECK(result.ErrorValue().domain.Value() == descriptor.domain.Value());
            CHECK(result.ErrorValue().code.Value() == descriptor.code.Value());
        }

        [[nodiscard]] ScalarCurveView Curve(ChannelKeys &keys, const float from, const float to,
                                            const CurveInterpolation interpolation = CurveInterpolation::Linear) {
            keys = {{{.time = 0, .value = from, .interpolation = interpolation}, {.time = 10, .value = to}}};
            Result<ScalarCurveView> curve = ScalarCurveView::Create(keys);
            REQUIRE(curve.HasValue());
            return curve.Value();
        }

        [[nodiscard]] TransformTrackCurves Curves(TrackKeyStorage &storage, const Math::Vec3 fromPosition = {},
                                                  const Math::Vec3 toPosition = {},
                                                  const Math::Quaternion fromRotation = Math::Quaternion::Identity(),
                                                  const Math::Quaternion toRotation = Math::Quaternion::Identity(),
                                                  const Math::Vec3 fromScale = {1.0F, 1.0F, 1.0F},
                                                  const Math::Vec3 toScale = {1.0F, 1.0F, 1.0F}) {
            return {.position = {Curve(storage[0], fromPosition.x, toPosition.x), Curve(storage[1], fromPosition.y, toPosition.y),
                                 Curve(storage[2], fromPosition.z, toPosition.z)},
                    .rotation = {Curve(storage[3], fromRotation.x, toRotation.x), Curve(storage[4], fromRotation.y, toRotation.y),
                                 Curve(storage[5], fromRotation.z, toRotation.z), Curve(storage[6], fromRotation.w, toRotation.w)},
                    .scale = {Curve(storage[7], fromScale.x, toScale.x), Curve(storage[8], fromScale.y, toScale.y),
                              Curve(storage[9], fromScale.z, toScale.z)}};
        }

        [[nodiscard]] TransformTrackView RootTrack(TrackKeyStorage &storage, const std::uint64_t target,
                                                   const Math::WorldCoordinate64 anchor = {}) {
            Result<TransformTrackView> track =
                TransformTrackView::Create({.target = Runtime::SceneObjectId{target}, .rootAnchor = anchor}, Curves(storage));
            REQUIRE(track.HasValue());
            return track.Value();
        }

        [[nodiscard]] TransformTrackView ChildTrack(TrackKeyStorage &storage, const std::uint64_t target, const std::uint64_t parent,
                                                    const Math::Vec3 toPosition = {}) {
            Result<TransformTrackView> track =
                TransformTrackView::Create({.target = Runtime::SceneObjectId{target}, .parent = Runtime::SceneObjectId{parent}},
                                           Curves(storage, {}, toPosition));
            REQUIRE(track.HasValue());
            return track.Value();
        }

        [[nodiscard]] Runtime::RuntimeSceneDefinition Definition(const std::uint64_t revision = 1, const bool includeChild = true) {
            Runtime::SceneDefinitionBuilder builder{Runtime::SceneDefinitionId{42}, Runtime::SceneDefinitionRevision{revision}};
            builder.Add({.object = Runtime::SceneObjectId{1}});
            if (includeChild)
                builder.Add({.object = Runtime::SceneObjectId{2}, .parent = Runtime::SceneObjectId{1}});
            Result<Runtime::RuntimeSceneDefinition> built = std::move(builder).Build();
            REQUIRE(built.HasValue());
            return std::move(built).Value();
        }

        [[nodiscard]] std::unique_ptr<Runtime::RuntimeScene> Scene(const std::uint64_t runtimeId, const std::uint64_t revision = 1,
                                                                   const bool includeChild = true) {
            Runtime::RuntimeSceneDefinition definition = Definition(revision, includeChild);
            Result<std::unique_ptr<Runtime::RuntimeScene>> scene =
                Runtime::RuntimeScene::Create(definition, Runtime::SceneRuntimeId{runtimeId});
            REQUIRE(scene.HasValue());
            return std::move(scene).Value();
        }

        [[nodiscard]] TransformOriginEpoch Origin(const std::int64_t xMillimeters = 0, const std::uint64_t generation = 1) {
            return {Math::WorldCoordinate64::FromMillimeters(xMillimeters, 0, 0), generation};
        }

        [[nodiscard]] Result<TransformTrackCommandBatch> ApplyForTest(const std::span<const TransformTrackView> tracks,
                                                                      const CurveTime time, const TransformOriginEpoch &origin,
                                                                      Runtime::RuntimeScene &scene,
                                                                      const TransformTrackOrderWorkspace workspace) {
            Result<TransformTrackCommandBatch> batch = BuildTransformTrackCommands(tracks, time, origin, scene.View(), workspace);
            if (batch.HasError())
                return Result<TransformTrackCommandBatch>::Failure(batch.ErrorValue());
            const Result<Runtime::StructuralCommitResult> committed = scene.Commit(batch.Value().commands);
            if (committed.HasError())
                return Result<TransformTrackCommandBatch>::Failure(committed.ErrorValue());
            return batch;
        }
    }  // namespace

    TEST_CASE("Transform track samples all TRS channels without seek history", "[unit][cinematic][transform][sampling]") {
        TrackKeyStorage storage{};
        const Math::Quaternion endRotation = Math::Quaternion::FromAxisAngle({0.0F, 1.0F, 0.0F}, Math::Pi);
        Result<TransformTrackView> created =
            TransformTrackView::Create({.target = Runtime::SceneObjectId{1}, .rootAnchor = Math::WorldCoordinate64{}},
                                       Curves(storage, {0.0F, 2.0F, 4.0F}, {10.0F, 12.0F, 14.0F}, Math::Quaternion::Identity(), endRotation,
                                              {1.0F, 2.0F, 3.0F}, {3.0F, 4.0F, 5.0F}));
        REQUIRE(created.HasValue());

        Result<Math::Transform> middle = created.Value().Sample(5, Origin());
        REQUIRE(middle.HasValue());
        CHECK(middle.Value().translation == Math::Vec3{5.0F, 7.0F, 9.0F});
        CHECK(middle.Value().scale == Math::Vec3{2.0F, 3.0F, 4.0F});
        const Math::Quaternion rotation = middle.Value().rotation;
        CHECK(Math::Length(Math::Vec4{rotation.x, rotation.y, rotation.z, rotation.w}) == Approx(1.0F));

        REQUIRE(created.Value().Sample(10, Origin()).HasValue());
        REQUIRE(created.Value().Sample(1, Origin()).HasValue());
        const Result<Math::Transform> repeated = created.Value().Sample(5, Origin());
        REQUIRE(repeated.HasValue());
        CHECK(repeated.Value() == middle.Value());
        CHECK(created.Value().Sample(-100, Origin()).Value().translation == Math::Vec3{0.0F, 2.0F, 4.0F});
        CHECK(created.Value().Sample(100, Origin()).Value().translation == Math::Vec3{10.0F, 12.0F, 14.0F});

        static_cast<void>(created.Value().Sample(5, Origin()));
        const std::size_t before = gTransformTrackTestAllocations.load(std::memory_order_relaxed);
        bool allSamplesSucceeded = true;
        for (std::size_t iteration = 0; iteration < 1'000; ++iteration)
            allSamplesSucceeded =
                allSamplesSucceeded && created.Value().Sample(static_cast<CurveTime>(iteration % 11), Origin()).HasValue();
        CHECK(allSamplesSucceeded);
        CHECK(gTransformTrackTestAllocations.load(std::memory_order_relaxed) == before);
    }

    TEST_CASE("Empty transform batches are bounded no-ops", "[unit][cinematic][transform][empty]") {
        std::array<std::uint8_t, 1> states{};
        std::array<std::size_t, 1> sorted{};
        std::array<std::size_t, 1> stack{};
        std::array<std::size_t, 1> order{};
        std::unique_ptr<Runtime::RuntimeScene> scene = Scene(9);
        const Result<TransformTrackCommandBatch> applied = ApplyForTest({}, 0, Origin(), *scene, {states, sorted, stack, order});
        REQUIRE(applied.HasValue());
        CHECK(applied.Value().transformsQueued == 0);
    }

    TEST_CASE("Transform position channels preserve scalar tangent interpolation", "[unit][cinematic][transform][tangent]") {
        TrackKeyStorage storage{};
        TransformTrackCurves curves = Curves(storage);
        ChannelKeys tangentKeys{{{.time = 0, .value = 0.0F, .interpolation = CurveInterpolation::HermiteSpline, .tangentOut = {2, 4.0F}},
                                 {.time = 10, .value = 10.0F, .tangentIn = {-2, -4.0F}}}};
        Result<ScalarCurveView> tangentCurve = ScalarCurveView::Create(tangentKeys);
        REQUIRE(tangentCurve.HasValue());
        curves.position[0] = tangentCurve.Value();
        Result<TransformTrackView> track =
            TransformTrackView::Create({.target = Runtime::SceneObjectId{1}, .rootAnchor = Math::WorldCoordinate64{}}, curves);
        REQUIRE(track.HasValue());
        Result<ScalarCurveSample> scalar = tangentCurve.Value().Sample(5);
        Result<Math::Transform> transform = track.Value().Sample(5, Origin());
        REQUIRE(scalar.HasValue());
        REQUIRE(transform.HasValue());
        CHECK(transform.Value().translation.x == scalar.Value().value);
    }

    TEST_CASE("Transform track rejects ambiguous spaces and invalid sampled rotations", "[unit][cinematic][transform][validation]") {
        TrackKeyStorage storage{};
        TransformTrackCurves curves = Curves(storage);
        RequireError(TransformTrackView::Create({.target = {}}, curves), CinematicErrors::TransformBindingInvalid);
        RequireError(TransformTrackView::Create({.target = Runtime::SceneObjectId{1}}, curves), CinematicErrors::TransformBindingInvalid);
        RequireError(TransformTrackView::Create({.target = Runtime::SceneObjectId{1}, .parent = Runtime::SceneObjectId{1}}, curves),
                     CinematicErrors::TransformBindingInvalid);
        RequireError(TransformTrackView::Create({.target = Runtime::SceneObjectId{1},
                                                 .parent = Runtime::SceneObjectId{2},
                                                 .rootAnchor = Math::WorldCoordinate64{}},
                                                curves),
                     CinematicErrors::TransformBindingInvalid);

        TrackKeyStorage zeroRotationStorage{};
        Result<TransformTrackView> zeroRotation =
            TransformTrackView::Create({.target = Runtime::SceneObjectId{1}, .rootAnchor = Math::WorldCoordinate64{}},
                                       Curves(zeroRotationStorage, {}, {}, {0.0F, 0.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 0.0F, 0.0F}));
        RequireError(zeroRotation, CinematicErrors::TransformRotationInvalid);

        TrackKeyStorage validStorage{};
        const TransformTrackView valid = RootTrack(validStorage, 1);
        RequireError(valid.Sample(5, Origin(0, 0)), CinematicErrors::TransformOriginInvalid);

        TrackKeyStorage flippedStorage{};
        RequireError(TransformTrackView::Create({.target = Runtime::SceneObjectId{1}, .rootAnchor = Math::WorldCoordinate64{}},
                                                Curves(flippedStorage, {}, {}, Math::Quaternion::Identity(), {0.0F, 0.0F, 0.0F, -1.0F})),
                     CinematicErrors::TransformRotationInvalid);

        ScalarCurveKey nonFinite{.time = 0, .value = std::numeric_limits<float>::infinity()};
        RequireError(ScalarCurveView::Create(std::span{&nonFinite, 1}), CinematicErrors::CurveNonFinite);
    }

    TEST_CASE("Transform hierarchy order is stable parent-first and rejects duplicate cycles", "[unit][cinematic][transform][order]") {
        TrackKeyStorage rootStorage{};
        TrackKeyStorage childStorage{};
        TrackKeyStorage grandchildStorage{};
        const TransformTrackView root = RootTrack(rootStorage, 1);
        const TransformTrackView child = ChildTrack(childStorage, 2, 1);
        const TransformTrackView grandchild = ChildTrack(grandchildStorage, 3, 2);
        const std::array tracks{grandchild, child, root};
        std::array<std::uint8_t, 3> emitted{};
        std::array<std::size_t, 3> sorted{};
        std::array<std::size_t, 3> stack{};
        std::array<std::size_t, 3> order{};
        Result<std::size_t> built = BuildTransformTrackOrder(tracks, {emitted, sorted, stack, order});
        REQUIRE(built.HasValue());
        CHECK(order == std::array<std::size_t, 3>{2, 1, 0});

        const std::array duplicate{root, root};
        std::array<std::uint8_t, 2> duplicateScratch{};
        std::array<std::size_t, 2> duplicateSorted{};
        std::array<std::size_t, 2> duplicateStack{};
        std::array<std::size_t, 2> duplicateOrder{};
        RequireError(BuildTransformTrackOrder(duplicate, {duplicateScratch, duplicateSorted, duplicateStack, duplicateOrder}),
                     CinematicErrors::TransformBindingDuplicate);
        RequireError(BuildTransformTrackOrder(tracks, {{}, {}, {}, {}}), CinematicErrors::TransformCapacityExceeded);

        TrackKeyStorage firstStorage{};
        TrackKeyStorage secondStorage{};
        const TransformTrackView first = ChildTrack(firstStorage, 10, 11);
        const TransformTrackView second = ChildTrack(secondStorage, 11, 10);
        const std::array cycle{first, second};
        RequireError(BuildTransformTrackOrder(cycle, {duplicateScratch, duplicateSorted, duplicateStack, duplicateOrder}),
                     CinematicErrors::TransformHierarchyCycle);
    }

    TEST_CASE("Runtime application resolves current generations and localizes roots once",
              "[unit][cinematic][transform][runtime][rebase]") {
        TrackKeyStorage rootStorage{};
        TrackKeyStorage childStorage{};
        Result<TransformTrackView> root =
            TransformTrackView::Create({.target = Runtime::SceneObjectId{1},
                                        .rootAnchor = Math::WorldCoordinate64::FromMillimeters(1'001'000, 0, 0)},
                                       Curves(rootStorage, {}, {2.0F, 0.0F, 0.0F}));
        REQUIRE(root.HasValue());
        const TransformTrackView child = ChildTrack(childStorage, 2, 1, {4.0F, 0.0F, 0.0F});
        const std::array tracks{child, root.Value()};
        std::array<std::uint8_t, 2> emitted{};
        std::array<std::size_t, 2> sorted{};
        std::array<std::size_t, 2> stack{};
        std::array<std::size_t, 2> order{};
        std::unique_ptr<Runtime::RuntimeScene> scene = Scene(10);

        Result<TransformTrackCommandBatch> first = ApplyForTest(tracks, 10, Origin(1'000'000, 7), *scene, {emitted, sorted, stack, order});
        REQUIRE(first.HasValue());
        CHECK(first.Value().transformsQueued == 2);
        CHECK(first.Value().originGeneration == 7);
        Runtime::RuntimeSceneView view = scene->View();
        const Runtime::EntityRef rootEntity = *view.Find(Runtime::SceneObjectId{1});
        const Runtime::EntityRef childEntity = *view.Find(Runtime::SceneObjectId{2});
        CHECK(view.Get(rootEntity).Value().localTransform->translation.x == Approx(3.0F));
        CHECK(view.Get(childEntity).Value().localTransform->translation.x == Approx(4.0F));
        CHECK(root.Value().Sample(10, Origin(1'000'000, 7)).Value() == *view.Get(rootEntity).Value().localTransform);

        Result<TransformTrackCommandBatch> rebased =
            ApplyForTest(tracks, 10, Origin(1'000'500, 8), *scene, {emitted, sorted, stack, order});
        REQUIRE(rebased.HasValue());
        view = scene->View();
        CHECK(view.Get(*view.Find(Runtime::SceneObjectId{1})).Value().localTransform->translation.x == Approx(2.5F));
        CHECK(view.Get(*view.Find(Runtime::SceneObjectId{2})).Value().localTransform->translation.x == Approx(4.0F));
    }

    TEST_CASE("Runtime replacement and respawn never reuse cached entity references", "[unit][cinematic][transform][lifecycle]") {
        TrackKeyStorage storage{};
        const TransformTrackView root = RootTrack(storage, 1);
        const std::array tracks{root};
        std::array<std::uint8_t, 1> emitted{};
        std::array<std::size_t, 1> sorted{};
        std::array<std::size_t, 1> stack{};
        std::array<std::size_t, 1> order{};
        std::unique_ptr<Runtime::RuntimeScene> oldScene = Scene(21);
        const Runtime::EntityRef oldReference = *oldScene->View().Find(Runtime::SceneObjectId{1});
        std::unique_ptr<Runtime::RuntimeScene> replacement = Scene(22, 2, false);
        REQUIRE(ApplyForTest(tracks, 5, Origin(), *replacement, {emitted, sorted, stack, order}).HasValue());
        const Runtime::EntityRef replacementReference = *replacement->View().Find(Runtime::SceneObjectId{1});
        CHECK(oldReference.runtime != replacementReference.runtime);
        REQUIRE(replacement->View().Get(oldReference).HasError());

        Runtime::SceneCommandBuffer destroy;
        destroy.Destroy(replacementReference);
        REQUIRE(replacement->Commit(destroy).HasValue());
        RequireError(ApplyForTest(tracks, 5, Origin(), *replacement, {emitted, sorted, stack, order}),
                     CinematicErrors::TransformTargetMissing);
        Runtime::SceneCommandBuffer respawn;
        static_cast<void>(respawn.Create({.authoredObject = Runtime::SceneObjectId{1}}));
        REQUIRE(replacement->Commit(respawn).HasValue());
        REQUIRE(ApplyForTest(tracks, 5, Origin(), *replacement, {emitted, sorted, stack, order}).HasValue());
        const Runtime::EntityRef respawned = *replacement->View().Find(Runtime::SceneObjectId{1});
        CHECK(respawned.entity.generation != replacementReference.entity.generation);
    }

    TEST_CASE("Missing or mismatched hierarchy fails without partial mutation", "[unit][cinematic][transform][atomic]") {
        TrackKeyStorage rootStorage{};
        TrackKeyStorage childStorage{};
        const TransformTrackView root = RootTrack(rootStorage, 1);
        const TransformTrackView child = ChildTrack(childStorage, 2, 1, {10.0F, 0.0F, 0.0F});
        const std::array tracks{root, child};
        std::array<std::uint8_t, 2> emitted{};
        std::array<std::size_t, 2> sorted{};
        std::array<std::size_t, 2> stack{};
        std::array<std::size_t, 2> order{};
        std::unique_ptr<Runtime::RuntimeScene> missingChild = Scene(30, 1, false);
        RequireError(ApplyForTest(tracks, 10, Origin(), *missingChild, {emitted, sorted, stack, order}),
                     CinematicErrors::TransformTargetMissing);
        CHECK(missingChild->View().Get(*missingChild->View().Find(Runtime::SceneObjectId{1})).Value().localTransform->translation.x ==
              0.0F);

        TrackKeyStorage wrongParentStorage{};
        const TransformTrackView wrongParent = ChildTrack(wrongParentStorage, 2, 99);
        const std::array wrongTracks{wrongParent};
        std::array<std::uint8_t, 1> oneScratch{};
        std::array<std::size_t, 1> oneSorted{};
        std::array<std::size_t, 1> oneStack{};
        std::array<std::size_t, 1> oneOrder{};
        std::unique_ptr<Runtime::RuntimeScene> scene = Scene(31);
        RequireError(ApplyForTest(wrongTracks, 5, Origin(), *scene, {oneScratch, oneSorted, oneStack, oneOrder}),
                     CinematicErrors::TransformParentMissing);
    }
}  // namespace Horo::Cinematic
