#include "AnimationTestFixtures.h"
#include "Horo/Animation/SkeletonAsset.h"

#include <catch2/catch_test_macros.hpp>
#include <limits>
#include <string>
#include <vector>

namespace Horo::Animation {
    namespace {
        using Test::Asset;
        using Test::Id;

        SkeletonJoint Joint(const std::uint64_t id, const std::optional<std::uint64_t> parent = std::nullopt) {
            return {.id = Id<JointId>(id),
                    .parent = parent ? std::optional{Id<JointId>(*parent)} : std::nullopt,
                    .name = "joint-" + std::to_string(id)};
        }

        SkeletonSocket Socket(const std::uint64_t id, const std::uint64_t joint) {
            return {.id = Id<SkeletonSocketId>(id), .joint = Id<JointId>(joint), .name = "socket-" + std::to_string(id)};
        }

        SkeletonAssetData Candidate(std::vector<SkeletonJoint> joints) {
            return {.skeleton = Asset<SkeletonId>(1), .joints = std::move(joints)};
        }

        template <typename Value> void RequireSkeletonError(const Result<Value> &result, const ErrorCodeDescriptor &expected) {
            REQUIRE(result.HasError());
            CHECK(result.ErrorValue().domain.Value() == "horo.animation");
            CHECK(result.ErrorValue().code.Value() == expected.code.Value());
        }

        std::vector<std::uint64_t> JointValues(const SkeletonAsset &asset) {
            std::vector<std::uint64_t> values;
            values.reserve(asset.Data().joints.size());
            for (const SkeletonJoint &joint : asset.Data().joints)
                values.push_back(joint.id.Value());
            return values;
        }
    }  // namespace

    TEST_CASE("Skeleton assets canonicalize arbitrary source order into stable parent-first order", "[unit][animation][skeleton]") {
        auto candidate = Candidate({Joint(40, 20), Joint(30, 10), Joint(20), Joint(10), Joint(25, 20)});
        candidate.sockets = {Socket(9, 40), Socket(3, 10)};

        auto created = SkeletonAsset::Create(std::move(candidate));
        REQUIRE(created.HasValue());
        const SkeletonAsset &asset = created.Value();
        CHECK(JointValues(asset) == std::vector<std::uint64_t>{10, 20, 25, 30, 40});
        REQUIRE(asset.RootJoints().size() == 2);
        CHECK(asset.RootJoints()[0] == Id<JointId>(10));
        CHECK(asset.RootJoints()[1] == Id<JointId>(20));
        CHECK(asset.Data().sockets[0].id == Id<SkeletonSocketId>(3));
        CHECK(asset.Data().sockets[1].id == Id<SkeletonSocketId>(9));
    }

    TEST_CASE("Skeleton canonical order is independent of candidate container order", "[unit][animation][skeleton][determinism]") {
        auto first = Candidate({Joint(8, 3), Joint(3, 1), Joint(5, 1), Joint(1)});
        auto second = Candidate({Joint(5, 1), Joint(1), Joint(8, 3), Joint(3, 1)});
        const auto firstAsset = SkeletonAsset::Create(std::move(first));
        const auto secondAsset = SkeletonAsset::Create(std::move(second));
        REQUIRE(firstAsset.HasValue());
        REQUIRE(secondAsset.HasValue());
        CHECK(firstAsset.Value().Data() == secondAsset.Value().Data());
    }

    TEST_CASE("Skeleton validation rejects duplicate joint and socket identities", "[unit][animation][skeleton]") {
        RequireSkeletonError(SkeletonAsset::Create(Candidate({Joint(1), Joint(1)})), AnimationErrors::SkeletonDuplicateIdentity);

        auto duplicateSocket = Candidate({Joint(1)});
        duplicateSocket.sockets = {Socket(2, 1), Socket(2, 1)};
        RequireSkeletonError(SkeletonAsset::Create(std::move(duplicateSocket)), AnimationErrors::SkeletonDuplicateIdentity);
    }

    TEST_CASE("Skeleton validation rejects missing joint references", "[unit][animation][skeleton]") {
        RequireSkeletonError(SkeletonAsset::Create(Candidate({Joint(1), Joint(2, 99)})), AnimationErrors::SkeletonJointMissing);

        auto missingSocket = Candidate({Joint(1)});
        missingSocket.sockets = {Socket(2, 99)};
        RequireSkeletonError(SkeletonAsset::Create(std::move(missingSocket)), AnimationErrors::SkeletonJointMissing);

        auto missingMirror = Candidate({Joint(1)});
        missingMirror.joints.front().side = SkeletonJointSide::Left;
        missingMirror.joints.front().mirror = Id<JointId>(99);
        RequireSkeletonError(SkeletonAsset::Create(std::move(missingMirror)), AnimationErrors::SkeletonJointMissing);
    }

    TEST_CASE("Skeleton validation rejects complete and disconnected cycles", "[unit][animation][skeleton]") {
        RequireSkeletonError(SkeletonAsset::Create(Candidate({Joint(1, 2), Joint(2, 1)})), AnimationErrors::SkeletonHierarchyCycle);
        RequireSkeletonError(SkeletonAsset::Create(Candidate({Joint(1), Joint(2, 3), Joint(3, 2)})),
                             AnimationErrors::SkeletonHierarchyCycle);
    }

    TEST_CASE("Skeleton limits bound counts depth names and caller policy", "[unit][animation][skeleton][limits]") {
        SkeletonAssetBuildContext context{};
        context.limits.maximumJoints = 2;
        RequireSkeletonError(SkeletonAsset::Create(Candidate({Joint(1), Joint(2, 1), Joint(3, 2)}), context),
                             AnimationErrors::SkeletonLimitExceeded);

        context = {};
        context.limits.maximumHierarchyDepth = 2;
        RequireSkeletonError(SkeletonAsset::Create(Candidate({Joint(1), Joint(2, 1), Joint(3, 2)}), context),
                             AnimationErrors::SkeletonLimitExceeded);

        context = {};
        context.limits.maximumNameBytes = 4;
        RequireSkeletonError(SkeletonAsset::Create(Candidate({Joint(1)}), context), AnimationErrors::SkeletonMetadataInvalid);

        context = {};
        context.limits.maximumJoints = SkeletonAssetHardLimits::Joints + 1U;
        RequireSkeletonError(SkeletonAsset::Create(Candidate({Joint(1)}), context), AnimationErrors::SkeletonLimitExceeded);
        RequireSkeletonError(SkeletonAsset::Create(Candidate({})), AnimationErrors::SkeletonLimitExceeded);
    }

    TEST_CASE("Skeleton bind and reference transforms must be finite and consistent", "[unit][animation][skeleton][bind]") {
        auto nonFinite = Candidate({Joint(1)});
        nonFinite.joints.front().referenceLocalTransform.translation.x = std::numeric_limits<float>::quiet_NaN();
        RequireSkeletonError(SkeletonAsset::Create(std::move(nonFinite)), AnimationErrors::SkeletonTransformInvalid);

        auto singularInverse = Candidate({Joint(1)});
        singularInverse.joints.front().inverseBindMatrix = {};
        RequireSkeletonError(SkeletonAsset::Create(std::move(singularInverse)), AnimationErrors::SkeletonTransformInvalid);

        auto inconsistent = Candidate({Joint(1)});
        inconsistent.joints.front().referenceLocalTransform.translation.x = 2.0F;
        RequireSkeletonError(SkeletonAsset::Create(std::move(inconsistent)), AnimationErrors::SkeletonTransformInvalid);
    }

    TEST_CASE("Skeleton mirror metadata is reciprocal typed and opposite-sided", "[unit][animation][skeleton][metadata]") {
        auto mirrored = Candidate({Joint(1), Joint(2), Joint(3)});
        mirrored.joints[1].retargetRole = SkeletonRetargetRole::Hand;
        mirrored.joints[1].side = SkeletonJointSide::Left;
        mirrored.joints[1].mirror = Id<JointId>(3);
        mirrored.joints[2].retargetRole = SkeletonRetargetRole::Hand;
        mirrored.joints[2].side = SkeletonJointSide::Right;
        mirrored.joints[2].mirror = Id<JointId>(2);
        REQUIRE(SkeletonAsset::Create(mirrored).HasValue());

        mirrored.joints[2].mirror = std::nullopt;
        RequireSkeletonError(SkeletonAsset::Create(mirrored), AnimationErrors::SkeletonMetadataInvalid);
        mirrored.joints[2].mirror = Id<JointId>(2);
        mirrored.joints[2].side = SkeletonJointSide::Left;
        RequireSkeletonError(SkeletonAsset::Create(mirrored), AnimationErrors::SkeletonMetadataInvalid);
        mirrored.joints[2].side = SkeletonJointSide::Right;
        mirrored.joints[2].retargetRole = SkeletonRetargetRole::Foot;
        RequireSkeletonError(SkeletonAsset::Create(mirrored), AnimationErrors::SkeletonMetadataInvalid);
        mirrored.joints[2].retargetRole = static_cast<SkeletonRetargetRole>(255);
        RequireSkeletonError(SkeletonAsset::Create(std::move(mirrored)), AnimationErrors::SkeletonMetadataInvalid);
    }

    TEST_CASE("Skeleton contract version and stable identities fail closed", "[unit][animation][skeleton][version]") {
        auto versionSkew = Candidate({Joint(1)});
        versionSkew.contractVersion.minor = 1;
        RequireSkeletonError(SkeletonAsset::Create(std::move(versionSkew)), AnimationErrors::SkeletonVersionUnsupported);

        auto missingAsset = Candidate({Joint(1)});
        missingAsset.skeleton = {};
        RequireSkeletonError(SkeletonAsset::Create(std::move(missingAsset)), AnimationErrors::IdentityInvalid);

        auto missingJoint = Candidate({Joint(1)});
        missingJoint.joints.front().id = {};
        RequireSkeletonError(SkeletonAsset::Create(std::move(missingJoint)), AnimationErrors::IdentityInvalid);

        auto missingParent = Candidate({Joint(1)});
        missingParent.joints.front().parent = JointId{};
        RequireSkeletonError(SkeletonAsset::Create(std::move(missingParent)), AnimationErrors::IdentityInvalid);

        auto missingMirror = Candidate({Joint(1)});
        missingMirror.joints.front().mirror = JointId{};
        RequireSkeletonError(SkeletonAsset::Create(std::move(missingMirror)), AnimationErrors::IdentityInvalid);
    }

    TEST_CASE("Skeleton reload preserves identity and owns a detached immutable snapshot", "[unit][animation][skeleton][reload]") {
        auto candidate = Candidate({Joint(2, 1), Joint(1)});
        SkeletonAssetBuildContext reload{.replacing = candidate.skeleton};
        auto created = SkeletonAsset::Create(candidate, reload);
        REQUIRE(created.HasValue());

        candidate.joints.front().name = "mutated-source";
        CHECK(created.Value().Data().joints.front().name == "joint-1");
        reload.replacing = Asset<SkeletonId>(9);
        RequireSkeletonError(SkeletonAsset::Create(std::move(candidate), reload), AnimationErrors::SkeletonReloadMismatch);
    }

    TEST_CASE("Skeleton cancellation and shutdown reject work before validation", "[unit][animation][skeleton][lifecycle]") {
        auto malformed = Candidate({Joint(1, 99)});
        RequireSkeletonError(SkeletonAsset::Create(malformed, {.admission = SkeletonAssetAdmissionState::CancellationRequested}),
                             AnimationErrors::SkeletonValidationCancelled);
        RequireSkeletonError(SkeletonAsset::Create(malformed, {.admission = SkeletonAssetAdmissionState::ShuttingDown}),
                             AnimationErrors::SkeletonAdmissionRejected);
        RequireSkeletonError(SkeletonAsset::Create(std::move(malformed), {.admission = static_cast<SkeletonAssetAdmissionState>(255)}),
                             AnimationErrors::SkeletonAdmissionRejected);
    }

    TEST_CASE("Skeleton sockets have bounded metadata and finite transforms", "[unit][animation][skeleton][socket]") {
        auto invalidName = Candidate({Joint(1)});
        invalidName.sockets = {Socket(2, 1)};
        invalidName.sockets.front().name.clear();
        RequireSkeletonError(SkeletonAsset::Create(std::move(invalidName)), AnimationErrors::SkeletonMetadataInvalid);

        auto invalidTransform = Candidate({Joint(1)});
        invalidTransform.sockets = {Socket(2, 1)};
        invalidTransform.sockets.front().localTransform.rotation = {0.0F, 0.0F, 0.0F, 0.0F};
        RequireSkeletonError(SkeletonAsset::Create(std::move(invalidTransform)), AnimationErrors::SkeletonTransformInvalid);

        SkeletonAssetBuildContext context{};
        context.limits.maximumSockets = 0;
        auto overLimit = Candidate({Joint(1)});
        overLimit.sockets = {Socket(2, 1)};
        RequireSkeletonError(SkeletonAsset::Create(std::move(overLimit), context), AnimationErrors::SkeletonLimitExceeded);
    }
}  // namespace Horo::Animation
