#include "AnimationTestFixtures.h"
#include "Horo/Animation/PoseStorage.h"

#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <limits>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

namespace Horo::Animation {
    namespace {
        using Test::Asset;
        using Test::Id;

        SkeletonJoint PoseJoint(const std::uint64_t id, const std::optional<std::uint64_t> parent = std::nullopt) {
            return {.id = Id<JointId>(id), .parent = parent ? std::optional{Id<JointId>(*parent)} : std::nullopt, .name = "pose-joint"};
        }

        SkeletonAsset PoseSkeleton(std::vector<SkeletonJoint> joints = {PoseJoint(10), PoseJoint(20, 10), PoseJoint(30, 20),
                                                                        PoseJoint(40, 10)}) {
            auto result = SkeletonAsset::Create({.skeleton = Asset<SkeletonId>(61), .joints = std::move(joints)});
            REQUIRE(result.HasValue());
            return std::move(result).Value();
        }

        PoseStorageCreateContext PoseContext(const std::uint32_t capacity = 4) {
            return {.runtime = Id<AnimationRuntimeId>(71),
                    .skeletonGeneration = Id<SkeletonAssetGeneration>(81),
                    .limits = {.maximumPosesPerFrame = capacity, .maximumJointsPerPose = 8}};
        }

        AnimationInstanceHandle PoseInstance(const AnimationRuntimeId runtime = Id<AnimationRuntimeId>(71)) {
            return {.owner = runtime, .component = Id<AnimationComponentId>(91), .slot = {.index = 2, .generation = 3}};
        }

        std::vector<Math::Transform> LocalPose(const std::size_t count = 4) {
            return std::vector<Math::Transform>(count);
        }

        PoseFrameArena ReadyArena(const SkeletonAsset &skeleton, const std::uint32_t capacity = 4) {
            auto created = PoseFrameArena::Create(skeleton, PoseContext(capacity));
            REQUIRE(created.HasValue());
            PoseFrameArena arena = std::move(created).Value();
            REQUIRE(arena.BeginFrame(Id<AnimationFrameId>(1), skeleton.Data().skeleton, Id<SkeletonAssetGeneration>(81)).HasValue());
            return arena;
        }

        PoseHandle AddPose(PoseFrameArena &arena, std::vector<Math::Transform> local = LocalPose(), const std::uint64_t generation = 1) {
            auto allocated = arena.AllocatePose(PoseInstance(), Id<PoseGeneration>(generation), local);
            REQUIRE(allocated.HasValue());
            return allocated.Value();
        }

        PoseReadLease LeasePose(PoseFrameArena &arena, const PoseHandle &pose) {
            auto acquired = arena.AcquireReadLease(pose);
            REQUIRE(acquired.HasValue());
            return std::move(acquired).Value();
        }

        PoseReadLease AddEvaluateAndLease(PoseFrameArena &arena) {
            const PoseHandle pose = AddPose(arena);
            REQUIRE(arena.EvaluateModelSpace(pose).HasValue());
            return LeasePose(arena, pose);
        }

        template <typename Value> void CheckPoseFailure(const Result<Value> &result, const ErrorCodeDescriptor &expected) {
            REQUIRE_FALSE(result.HasValue());
            const Error error = result.ErrorValue();
            CHECK(error.code.Value() == expected.code.Value());
        }
    }  // namespace

    TEST_CASE("Pose arena evaluates canonical hierarchy into immutable model-space leases", "[unit][animation][pose]") {
        const SkeletonAsset skeleton = PoseSkeleton();
        PoseFrameArena arena = ReadyArena(skeleton);
        auto local = LocalPose();
        local[0].translation.x = 1.0F;
        local[1].translation.x = 2.0F;
        local[2].translation.x = 3.0F;
        const PoseHandle pose = AddPose(arena, local);
        REQUIRE(arena.EvaluateModelSpace(pose).HasValue());

        const PoseReadLease lease = LeasePose(arena, pose);
        CHECK(lease.IsValid());
        CHECK(lease.Handle() == pose);
        CHECK(lease.Frame() == Id<AnimationFrameId>(1));
        REQUIRE(lease.LocalTransforms().size() == 4);
        CHECK(lease.LocalTransforms()[1].translation.x == 2.0F);
        const auto root = lease.ModelSpace(Id<JointId>(10));
        const auto grandchild = lease.ModelSpace(Id<JointId>(30));
        REQUIRE(root.HasValue());
        REQUIRE(grandchild.HasValue());
        CHECK(root.Value().At(0, 3) == 1.0F);
        CHECK(grandchild.Value().At(0, 3) == 6.0F);
    }

    TEST_CASE("Partial hierarchy evaluation computes only requested ancestors", "[unit][animation][pose][partial]") {
        const SkeletonAsset skeleton = PoseSkeleton();
        PoseFrameArena arena = ReadyArena(skeleton);
        const PoseHandle pose = AddPose(arena);
        const JointId requested[]{Id<JointId>(20)};
        REQUIRE(arena.EvaluateModelSpace(pose, requested).HasValue());

        const PoseReadLease lease = LeasePose(arena, pose);
        REQUIRE(lease.ModelSpace(Id<JointId>(10)).HasValue());
        REQUIRE(lease.ModelSpace(Id<JointId>(20)).HasValue());
        CheckPoseFailure(lease.ModelSpace(Id<JointId>(30)), AnimationErrors::PoseNotEvaluated);
        CheckPoseFailure(lease.ModelSpace(Id<JointId>(40)), AnimationErrors::PoseNotEvaluated);
    }

    TEST_CASE("Dirty propagation invalidates exactly one descendant branch", "[unit][animation][pose][dirty]") {
        const SkeletonAsset skeleton = PoseSkeleton();
        PoseFrameArena arena = ReadyArena(skeleton);
        const PoseHandle pose = AddPose(arena);
        REQUIRE(arena.EvaluateModelSpace(pose).HasValue());

        Math::Transform replacement{};
        replacement.translation.y = 5.0F;
        REQUIRE(arena.SetLocalTransform(pose, Id<JointId>(20), replacement).HasValue());
        const JointId requested[]{Id<JointId>(20)};
        REQUIRE(arena.EvaluateModelSpace(pose, requested).HasValue());
        const PoseReadLease lease = LeasePose(arena, pose);
        CHECK(lease.ModelSpace(Id<JointId>(20)).Value().At(1, 3) == 5.0F);
        CheckPoseFailure(lease.ModelSpace(Id<JointId>(30)), AnimationErrors::PoseNotEvaluated);
        REQUIRE(lease.ModelSpace(Id<JointId>(40)).HasValue());
    }

    TEST_CASE("Pose arena exhaustion is bounded measured and reset by a newer frame", "[unit][animation][pose][limits]") {
        const SkeletonAsset skeleton = PoseSkeleton();
        PoseFrameArena arena = ReadyArena(skeleton, 2);
        const PoseHandle oldPose = AddPose(arena, LocalPose(), 1);
        AddPose(arena, LocalPose(), 2);
        CheckPoseFailure(arena.AllocatePose(PoseInstance(), Id<PoseGeneration>(3), LocalPose()), AnimationErrors::PoseArenaExhausted);
        CHECK(arena.Statistics().usedPoses == 2);
        CHECK(arena.Statistics().peakUsedPoses == 2);
        CHECK(arena.Statistics().failedAllocations == 1);

        REQUIRE(arena.BeginFrame(Id<AnimationFrameId>(2), skeleton.Data().skeleton, Id<SkeletonAssetGeneration>(81)).HasValue());
        CHECK(arena.Statistics().usedPoses == 0);
        CheckPoseFailure(arena.EvaluateModelSpace(oldPose), AnimationErrors::HandleStale);
        const PoseHandle replacement = AddPose(arena, LocalPose(), 4);
        CHECK(replacement.slot.index == oldPose.slot.index);
        CHECK(replacement.slot.generation == oldPose.slot.generation + 1U);
    }

    TEST_CASE("Immutable leases fence mutation frame reset cancellation and shutdown", "[unit][animation][pose][lease]") {
        const SkeletonAsset skeleton = PoseSkeleton();
        PoseFrameArena arena = ReadyArena(skeleton);
        const PoseHandle pose = AddPose(arena);
        PoseReadLease lease = LeasePose(arena, pose);
        CHECK(arena.Statistics().activeLeases == 1);

        CheckPoseFailure(arena.SetLocalTransform(pose, Id<JointId>(10), {}), AnimationErrors::PoseLeaseConflict);
        CheckPoseFailure(arena.EvaluateModelSpace(pose), AnimationErrors::PoseLeaseConflict);
        CheckPoseFailure(arena.BeginFrame(Id<AnimationFrameId>(2), skeleton.Data().skeleton, Id<SkeletonAssetGeneration>(81)),
                         AnimationErrors::PoseLeaseConflict);
        CheckPoseFailure(arena.CancelFrame(), AnimationErrors::PoseLeaseConflict);
        CheckPoseFailure(arena.Shutdown(), AnimationErrors::PoseLeaseConflict);
        lease = {};
        CHECK(arena.Statistics().activeLeases == 0);
        REQUIRE(arena.BeginFrame(Id<AnimationFrameId>(2), skeleton.Data().skeleton, Id<SkeletonAssetGeneration>(81)).HasValue());
    }

    TEST_CASE("Immutable lease fanout is bounded without changing pose identity", "[unit][animation][pose][lease][limits]") {
        const SkeletonAsset skeleton = PoseSkeleton();
        PoseFrameArena arena = ReadyArena(skeleton);
        const PoseHandle pose = AddPose(arena);
        std::vector<PoseReadLease> leases;
        leases.reserve(PoseStorageHardLimits::LeasesPerPose);
        for (std::uint32_t count = 0; count < PoseStorageHardLimits::LeasesPerPose; ++count) {
            auto acquired = arena.AcquireReadLease(pose);
            REQUIRE(acquired.HasValue());
            leases.push_back(std::move(acquired).Value());
        }
        CHECK(arena.Statistics().activeLeases == PoseStorageHardLimits::LeasesPerPose);
        CheckPoseFailure(arena.AcquireReadLease(pose), AnimationErrors::PoseLimitExceeded);
        leases.clear();
        CHECK(arena.Statistics().activeLeases == 0);
    }

    TEST_CASE("Frame and skeleton generation fencing rejects stale publication", "[unit][animation][pose][stale]") {
        const SkeletonAsset skeleton = PoseSkeleton();
        PoseFrameArena arena = ReadyArena(skeleton);
        CheckPoseFailure(arena.BeginFrame(Id<AnimationFrameId>(1), skeleton.Data().skeleton, Id<SkeletonAssetGeneration>(81)),
                         AnimationErrors::PoseFrameStale);
        CheckPoseFailure(arena.BeginFrame(Id<AnimationFrameId>(2), Asset<SkeletonId>(62), Id<SkeletonAssetGeneration>(81)),
                         AnimationErrors::PoseSkeletonMismatch);
        CheckPoseFailure(arena.BeginFrame(Id<AnimationFrameId>(2), skeleton.Data().skeleton, Id<SkeletonAssetGeneration>(82)),
                         AnimationErrors::PoseSkeletonStale);
        REQUIRE(arena.BeginFrame(Id<AnimationFrameId>(2), skeleton.Data().skeleton, Id<SkeletonAssetGeneration>(81)).HasValue());
    }

    TEST_CASE("Pose arena creation rejects version lifecycle identity and capacity skew", "[unit][animation][pose][create]") {
        const SkeletonAsset skeleton = PoseSkeleton();
        auto context = PoseContext();
        context.contractVersion.minor = 1;
        CheckPoseFailure(PoseFrameArena::Create(skeleton, context), AnimationErrors::PoseVersionUnsupported);
        context = PoseContext();
        context.admission = PoseStorageAdmissionState::CancellationRequested;
        CheckPoseFailure(PoseFrameArena::Create(skeleton, context), AnimationErrors::PoseEvaluationCancelled);
        context.admission = PoseStorageAdmissionState::ShuttingDown;
        CheckPoseFailure(PoseFrameArena::Create(skeleton, context), AnimationErrors::PoseAdmissionRejected);
        context = PoseContext();
        context.runtime = {};
        CheckPoseFailure(PoseFrameArena::Create(skeleton, context), AnimationErrors::IdentityInvalid);
        context = PoseContext();
        context.skeletonGeneration = {};
        CheckPoseFailure(PoseFrameArena::Create(skeleton, context), AnimationErrors::IdentityInvalid);
        context = PoseContext();
        context.limits.maximumPosesPerFrame = PoseStorageHardLimits::PosesPerFrame + 1U;
        CheckPoseFailure(PoseFrameArena::Create(skeleton, context), AnimationErrors::PoseLimitExceeded);
        context = PoseContext();
        context.limits.maximumPosesPerFrame = 0;
        CheckPoseFailure(PoseFrameArena::Create(skeleton, context), AnimationErrors::PoseLimitExceeded);
        context = PoseContext();
        context.limits.maximumJointsPerPose = PoseStorageHardLimits::JointsPerPose + 1U;
        CheckPoseFailure(PoseFrameArena::Create(skeleton, context), AnimationErrors::PoseLimitExceeded);
        context = PoseContext();
        context.limits.maximumJointsPerPose = 3;
        CheckPoseFailure(PoseFrameArena::Create(skeleton, context), AnimationErrors::PoseLimitExceeded);
    }

    TEST_CASE("Pose allocation validates complete finite input and exact runtime owner", "[unit][animation][pose][input]") {
        const SkeletonAsset skeleton = PoseSkeleton();
        PoseFrameArena arena = ReadyArena(skeleton);
        CheckPoseFailure(arena.AllocatePose(PoseInstance(), PoseGeneration{}, LocalPose()), AnimationErrors::HandleMalformed);
        CheckPoseFailure(arena.AllocatePose(PoseInstance(Id<AnimationRuntimeId>(72)), Id<PoseGeneration>(1), LocalPose()),
                         AnimationErrors::HandleOwnerMismatch);
        CheckPoseFailure(arena.AllocatePose(PoseInstance(), Id<PoseGeneration>(1), LocalPose(3)), AnimationErrors::PoseLimitExceeded);
        auto nonFinite = LocalPose();
        nonFinite[2].translation.x = std::numeric_limits<float>::quiet_NaN();
        CheckPoseFailure(arena.AllocatePose(PoseInstance(), Id<PoseGeneration>(1), nonFinite), AnimationErrors::PoseTransformInvalid);
        CheckPoseFailure(arena.AllocatePose({}, Id<PoseGeneration>(1), LocalPose()), AnimationErrors::HandleMalformed);

        Math::Transform malformed{};
        malformed.rotation = {0.0F, 0.0F, 0.0F, 0.0F};
        const PoseHandle pose = AddPose(arena);
        CheckPoseFailure(arena.SetLocalTransform(pose, Id<JointId>(10), malformed), AnimationErrors::PoseTransformInvalid);
    }

    TEST_CASE("Pose mutation and partial evaluation reject unknown joints transactionally", "[unit][animation][pose][joint]") {
        const SkeletonAsset skeleton = PoseSkeleton();
        PoseFrameArena arena = ReadyArena(skeleton);
        const PoseHandle pose = AddPose(arena);
        CheckPoseFailure(arena.SetLocalTransform(pose, Id<JointId>(999), {}), AnimationErrors::PoseJointMissing);
        const JointId unknown[]{Id<JointId>(999)};
        CheckPoseFailure(arena.EvaluateModelSpace(pose, unknown), AnimationErrors::PoseJointMissing);
        const PoseReadLease lease = LeasePose(arena, pose);
        CheckPoseFailure(lease.ModelSpace(Id<JointId>(999)), AnimationErrors::PoseJointMissing);
    }

    TEST_CASE("Cancellation and shutdown are explicit idempotent lifecycle boundaries", "[unit][animation][pose][lifecycle]") {
        const SkeletonAsset skeleton = PoseSkeleton();
        PoseFrameArena arena = ReadyArena(skeleton);
        const PoseHandle cancelled = AddPose(arena);
        REQUIRE(arena.CancelFrame().HasValue());
        REQUIRE(arena.CancelFrame().HasValue());
        CheckPoseFailure(arena.EvaluateModelSpace(cancelled), AnimationErrors::PoseEvaluationCancelled);
        REQUIRE(arena.BeginFrame(Id<AnimationFrameId>(2), skeleton.Data().skeleton, Id<SkeletonAssetGeneration>(81)).HasValue());
        const PoseHandle retired = AddPose(arena);
        REQUIRE(arena.Shutdown().HasValue());
        REQUIRE(arena.Shutdown().HasValue());
        CheckPoseFailure(arena.EvaluateModelSpace(retired), AnimationErrors::PoseAdmissionRejected);
        CheckPoseFailure(arena.BeginFrame(Id<AnimationFrameId>(3), skeleton.Data().skeleton, Id<SkeletonAssetGeneration>(81)),
                         AnimationErrors::PoseAdmissionRejected);
    }

    TEST_CASE("Lease reads and release are safe on a consumer thread while mutation stays owner-affine",
              "[unit][animation][pose][thread]") {
        const SkeletonAsset skeleton = PoseSkeleton();
        PoseFrameArena arena = ReadyArena(skeleton);
        const PoseHandle pose = AddPose(arena);
        REQUIRE(arena.EvaluateModelSpace(pose).HasValue());
        PoseReadLease lease = LeasePose(arena, pose);
        std::atomic<bool> leaseRead{false};
        std::thread consumer([moved = std::move(lease), &leaseRead]() mutable {
            leaseRead.store(moved.IsValid() && moved.LocalTransforms().size() == 4 && moved.ModelSpace(Id<JointId>(30)).HasValue(),
                            std::memory_order_release);
        });
        consumer.join();
        CHECK(leaseRead.load(std::memory_order_acquire));
        CHECK(arena.Statistics().activeLeases == 0);

        std::atomic<bool> rejected{false};
        std::thread wrongOwner([&] {
            const auto attempted = arena.EvaluateModelSpace(pose);
            rejected.store(attempted.HasError() && attempted.ErrorValue().code.Value() == AnimationErrors::PoseThreadViolation.code.Value(),
                           std::memory_order_release);
        });
        wrongOwner.join();
        CHECK(rejected.load(std::memory_order_acquire));
    }

    TEST_CASE("Move-only lease safely outlives the arena facade without extending mutation authority",
              "[unit][animation][pose][lifetime]") {
        PoseReadLease retained{};
        {
            const SkeletonAsset skeleton = PoseSkeleton();
            PoseFrameArena arena = ReadyArena(skeleton);
            retained = AddEvaluateAndLease(arena);
        }
        CHECK(retained.IsValid());
        CHECK(retained.LocalTransforms().size() == 4);
        REQUIRE(retained.ModelSpace(Id<JointId>(30)).HasValue());
    }

    TEST_CASE("Canonical skeleton order makes hierarchy evaluation independent of source order", "[unit][animation][pose][determinism]") {
        const SkeletonAsset first = PoseSkeleton({PoseJoint(30, 20), PoseJoint(10), PoseJoint(40, 10), PoseJoint(20, 10)});
        const SkeletonAsset second = PoseSkeleton({PoseJoint(40, 10), PoseJoint(20, 10), PoseJoint(10), PoseJoint(30, 20)});
        PoseFrameArena firstArena = ReadyArena(first);
        PoseFrameArena secondArena = ReadyArena(second);
        auto local = LocalPose();
        local[0].translation.x = 1.0F;
        local[1].translation.x = 2.0F;
        local[2].translation.x = 3.0F;
        local[3].translation.x = 4.0F;
        const PoseHandle firstPose = AddPose(firstArena, local);
        const PoseHandle secondPose = AddPose(secondArena, local);
        REQUIRE(firstArena.EvaluateModelSpace(firstPose).HasValue());
        REQUIRE(secondArena.EvaluateModelSpace(secondPose).HasValue());
        const PoseReadLease firstLease = LeasePose(firstArena, firstPose);
        const PoseReadLease secondLease = LeasePose(secondArena, secondPose);
        for (const SkeletonJoint &joint : first.Data().joints)
            CHECK(firstLease.ModelSpace(joint.id).Value() == secondLease.ModelSpace(joint.id).Value());
    }
}  // namespace Horo::Animation
