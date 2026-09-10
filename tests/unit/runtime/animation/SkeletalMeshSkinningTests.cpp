#include "AnimationTestFixtures.h"
#include "Horo/Animation/SkeletalMeshSkinning.h"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <initializer_list>
#include <limits>
#include <optional>
#include <ranges>
#include <string>
#include <type_traits>
#include <vector>

namespace Horo::Animation {
    namespace {
        using Test::Asset;
        using Test::Id;

        SkeletonJoint SkeletonJointFixture(const std::uint64_t id, const std::optional<std::uint64_t> parent = std::nullopt) {
            SkeletonJoint joint;
            joint.id = Id<JointId>(id);
            joint.name = "joint-" + std::to_string(id);
            if (parent.has_value())
                joint.parent = Id<JointId>(*parent);
            return joint;
        }

        SkeletonAsset SkeletonFixture(const std::uint8_t identity = 1) {
            SkeletonAssetData data{.skeleton = Asset<SkeletonId>(identity),
                                   .joints = {SkeletonJointFixture(30, 20), SkeletonJointFixture(10), SkeletonJointFixture(20, 10)}};
            auto created = SkeletonAsset::Create(std::move(data));
            REQUIRE(created.HasValue());
            return std::move(created.Value());
        }

        SkinningInfluence Influence(const std::uint64_t joint, const float weight) {
            return {.joint = Id<SkinningJointId>(joint), .weight = weight};
        }

        SkinnedVertex Vertex(std::initializer_list<SkinningInfluence> influences) {
            return {.influences = influences};
        }

        SkeletalMeshSection Section(const std::uint64_t id, const std::uint32_t firstVertex, const std::uint32_t vertexCount,
                                    std::initializer_list<std::uint64_t> palette) {
            SkeletalMeshSection section{.id = Id<SkeletalMeshSectionId>(id), .firstVertex = firstVertex, .vertexCount = vertexCount};
            for (const std::uint64_t joint : palette)
                section.palette.push_back(Id<SkinningJointId>(joint));
            return section;
        }

        SkeletalMeshSkinningData Candidate(const SkeletonAsset &skeleton) {
            SkeletalMeshSkinningData data{.mesh = Asset<SkeletalMeshId>(7),
                                          .binding = {.skeleton = skeleton.Data().skeleton,
                                                      .skeletonContractVersion = skeleton.Data().contractVersion,
                                                      .skeletonGeneration = Id<SkeletonAssetGeneration>(4),
                                                      .jointRemap = {{Id<SkinningJointId>(30), Id<JointId>(30)},
                                                                     {Id<SkinningJointId>(10), Id<JointId>(10)},
                                                                     {Id<SkinningJointId>(20), Id<JointId>(20)}}}};
            data.lods.push_back({.level = 0,
                                 .localBounds = {{-1.0F, -2.0F, -1.0F}, {1.0F, 2.0F, 1.0F}},
                                 .vertices = {Vertex({Influence(20, 1.0F), Influence(10, 3.0F)}), Vertex({Influence(30, 1.0F)})},
                                 .sections = {Section(9, 1, 1, {30}), Section(3, 0, 1, {20, 10})}});
            return data;
        }

        SkeletalMeshSkinningBuildContext Context() {
            return {.currentSkeletonGeneration = Id<SkeletonAssetGeneration>(4)};
        }

        template <typename Value> void RequireSkinningError(const Result<Value> &result, const ErrorCodeDescriptor &expected) {
            REQUIRE(result.HasError());
            const Error &error = result.ErrorValue();
            CHECK(error.domain.Value() == "horo.animation");
            CHECK(error.code.Value() == expected.code.Value());
        }
    }  // namespace

    static_assert(!std::is_same_v<SkeletalMeshId, SkeletonId>);
    static_assert(!std::is_same_v<SkinningJointId, JointId>);
    static_assert(!std::is_same_v<SkeletalMeshSectionId, SkinningJointId>);

    TEST_CASE("Skeletal mesh skinning canonicalizes remaps sections palettes and normalized influences", "[unit][animation][skinning]") {
        const SkeletonAsset skeleton = SkeletonFixture();
        auto created = SkeletalMeshSkinningAsset::Create(Candidate(skeleton), skeleton, Context());
        REQUIRE(created.HasValue());
        const auto &data = created.Value().Data();
        REQUIRE(data.binding.jointRemap.size() == 3);
        CHECK(data.binding.jointRemap[0].meshJoint == Id<SkinningJointId>(10));
        CHECK(data.binding.jointRemap[1].meshJoint == Id<SkinningJointId>(20));
        CHECK(data.binding.jointRemap[2].meshJoint == Id<SkinningJointId>(30));
        REQUIRE(data.lods[0].sections.size() == 2);
        CHECK(data.lods[0].sections[0].id == Id<SkeletalMeshSectionId>(3));
        CHECK(data.lods[0].sections[1].id == Id<SkeletalMeshSectionId>(9));
        CHECK(data.lods[0].sections[0].palette == std::vector<SkinningJointId>{Id<SkinningJointId>(10), Id<SkinningJointId>(20)});
        const auto &influences = data.lods[0].vertices[0].influences;
        REQUIRE(influences.size() == 2);
        CHECK(influences[0].joint == Id<SkinningJointId>(10));
        CHECK(influences[0].weight == 0.75F);
        CHECK(influences[1].joint == Id<SkinningJointId>(20));
        CHECK(influences[1].weight == 0.25F);
        CHECK(std::ranges::equal(created.Value().Palette(0, Id<SkeletalMeshSectionId>(3)), data.lods[0].sections[0].palette));
        CHECK(created.Value().Palette(1, Id<SkeletalMeshSectionId>(3)).empty());
        CHECK(created.Value().Palette(0, Id<SkeletalMeshSectionId>(99)).empty());
    }

    TEST_CASE("Skeletal mesh skinning canonical form is independent of source container order",
              "[unit][animation][skinning][determinism]") {
        const SkeletonAsset skeleton = SkeletonFixture();
        auto first = Candidate(skeleton);
        auto second = Candidate(skeleton);
        std::ranges::reverse(second.binding.jointRemap);
        std::ranges::reverse(second.lods[0].sections);
        std::ranges::reverse(second.lods[0].sections[1].palette);
        std::ranges::reverse(second.lods[0].vertices[0].influences);
        auto firstAsset = SkeletalMeshSkinningAsset::Create(std::move(first), skeleton, Context());
        auto secondAsset = SkeletalMeshSkinningAsset::Create(std::move(second), skeleton, Context());
        REQUIRE(firstAsset.HasValue());
        REQUIRE(secondAsset.HasValue());
        CHECK(firstAsset.Value().Data() == secondAsset.Value().Data());
    }

    TEST_CASE("Skeletal mesh skinning rejects version identity and reload skew", "[unit][animation][skinning][version]") {
        const SkeletonAsset skeleton = SkeletonFixture();
        auto versionSkew = Candidate(skeleton);
        versionSkew.contractVersion.minor = 1;
        RequireSkinningError(SkeletalMeshSkinningAsset::Create(std::move(versionSkew), skeleton, Context()),
                             AnimationErrors::SkinningVersionUnsupported);

        auto missingMesh = Candidate(skeleton);
        missingMesh.mesh = {};
        RequireSkinningError(SkeletalMeshSkinningAsset::Create(std::move(missingMesh), skeleton, Context()),
                             AnimationErrors::IdentityInvalid);

        auto reloadCandidate = Candidate(skeleton);
        auto reloadContext = Context();
        reloadContext.replacing = Asset<SkeletalMeshId>(8);
        RequireSkinningError(SkeletalMeshSkinningAsset::Create(std::move(reloadCandidate), skeleton, reloadContext),
                             AnimationErrors::SkinningReloadMismatch);
    }

    TEST_CASE("Skeletal mesh skinning lifecycle rejects cancellation and shutdown before malformed data",
              "[unit][animation][skinning][lifecycle]") {
        const SkeletonAsset skeleton = SkeletonFixture();
        auto cancelled = Context();
        cancelled.admission = SkeletalMeshSkinningAdmissionState::CancellationRequested;
        auto malformed = Candidate(skeleton);
        malformed.binding.jointRemap.clear();
        RequireSkinningError(SkeletalMeshSkinningAsset::Create(malformed, skeleton, cancelled),
                             AnimationErrors::SkinningValidationCancelled);

        auto shutdown = Context();
        shutdown.admission = SkeletalMeshSkinningAdmissionState::ShuttingDown;
        RequireSkinningError(SkeletalMeshSkinningAsset::Create(malformed, skeleton, shutdown), AnimationErrors::SkinningAdmissionRejected);
        shutdown.admission = static_cast<SkeletalMeshSkinningAdmissionState>(255);
        RequireSkinningError(SkeletalMeshSkinningAsset::Create(std::move(malformed), skeleton, shutdown),
                             AnimationErrors::SkinningAdmissionRejected);
    }

    TEST_CASE("Skeletal mesh bindings reject incompatible and stale skeleton publications", "[unit][animation][skinning][binding]") {
        const SkeletonAsset skeleton = SkeletonFixture();
        const SkeletonAsset otherSkeleton = SkeletonFixture(2);
        auto incompatible = Candidate(skeleton);
        RequireSkinningError(SkeletalMeshSkinningAsset::Create(incompatible, otherSkeleton, Context()),
                             AnimationErrors::SkinningSkeletonMismatch);

        incompatible.binding.skeletonContractVersion.minor = 1;
        RequireSkinningError(SkeletalMeshSkinningAsset::Create(std::move(incompatible), skeleton, Context()),
                             AnimationErrors::SkinningSkeletonMismatch);

        auto stale = Candidate(skeleton);
        stale.binding.skeletonGeneration = Id<SkeletonAssetGeneration>(3);
        RequireSkinningError(SkeletalMeshSkinningAsset::Create(stale, skeleton, Context()), AnimationErrors::SkinningBindingStale);
        stale.binding.skeletonGeneration = Id<SkeletonAssetGeneration>(4);
        auto missingCurrent = Context();
        missingCurrent.currentSkeletonGeneration = {};
        RequireSkinningError(SkeletalMeshSkinningAsset::Create(std::move(stale), skeleton, missingCurrent),
                             AnimationErrors::SkinningBindingStale);
    }

    TEST_CASE("Skeletal mesh remaps reject missing malformed and duplicate identities", "[unit][animation][skinning][remap]") {
        const SkeletonAsset skeleton = SkeletonFixture();
        auto empty = Candidate(skeleton);
        empty.binding.jointRemap.clear();
        RequireSkinningError(SkeletalMeshSkinningAsset::Create(std::move(empty), skeleton, Context()),
                             AnimationErrors::SkinningLimitExceeded);

        auto missing = Candidate(skeleton);
        missing.binding.jointRemap[0].skeletonJoint = Id<JointId>(99);
        RequireSkinningError(SkeletalMeshSkinningAsset::Create(std::move(missing), skeleton, Context()),
                             AnimationErrors::SkinningJointMissing);

        auto invalid = Candidate(skeleton);
        invalid.binding.jointRemap[0].meshJoint = {};
        RequireSkinningError(SkeletalMeshSkinningAsset::Create(std::move(invalid), skeleton, Context()), AnimationErrors::IdentityInvalid);

        auto duplicateSource = Candidate(skeleton);
        duplicateSource.binding.jointRemap[1].meshJoint = duplicateSource.binding.jointRemap[0].meshJoint;
        RequireSkinningError(SkeletalMeshSkinningAsset::Create(std::move(duplicateSource), skeleton, Context()),
                             AnimationErrors::SkinningDuplicateIdentity);

        auto duplicateTarget = Candidate(skeleton);
        duplicateTarget.binding.jointRemap[1].skeletonJoint = duplicateTarget.binding.jointRemap[0].skeletonJoint;
        RequireSkinningError(SkeletalMeshSkinningAsset::Create(std::move(duplicateTarget), skeleton, Context()),
                             AnimationErrors::SkinningDuplicateIdentity);
    }

    TEST_CASE("Skeletal mesh LODs require finite bounds and contiguous unique levels", "[unit][animation][skinning][lod]") {
        const SkeletonAsset skeleton = SkeletonFixture();
        auto empty = Candidate(skeleton);
        empty.lods.clear();
        RequireSkinningError(SkeletalMeshSkinningAsset::Create(std::move(empty), skeleton, Context()),
                             AnimationErrors::SkinningLimitExceeded);

        auto invalidBounds = Candidate(skeleton);
        invalidBounds.lods[0].localBounds.maximum.x = std::numeric_limits<float>::quiet_NaN();
        RequireSkinningError(SkeletalMeshSkinningAsset::Create(std::move(invalidBounds), skeleton, Context()),
                             AnimationErrors::SkinningLayoutInvalid);

        auto gap = Candidate(skeleton);
        gap.lods[0].level = 1;
        RequireSkinningError(SkeletalMeshSkinningAsset::Create(std::move(gap), skeleton, Context()),
                             AnimationErrors::SkinningLayoutInvalid);

        auto duplicate = Candidate(skeleton);
        duplicate.lods.push_back(duplicate.lods.front());
        RequireSkinningError(SkeletalMeshSkinningAsset::Create(std::move(duplicate), skeleton, Context()),
                             AnimationErrors::SkinningLayoutInvalid);
    }

    TEST_CASE("Skeletal mesh sections form one complete non-overlapping vertex partition", "[unit][animation][skinning][section]") {
        const SkeletonAsset skeleton = SkeletonFixture();
        auto empty = Candidate(skeleton);
        empty.lods[0].sections.clear();
        RequireSkinningError(SkeletalMeshSkinningAsset::Create(std::move(empty), skeleton, Context()),
                             AnimationErrors::SkinningLimitExceeded);

        auto zeroRange = Candidate(skeleton);
        zeroRange.lods[0].sections[0].vertexCount = 0;
        RequireSkinningError(SkeletalMeshSkinningAsset::Create(std::move(zeroRange), skeleton, Context()),
                             AnimationErrors::SkinningLayoutInvalid);

        auto gap = Candidate(skeleton);
        gap.lods[0].sections[1].firstVertex = 2;
        RequireSkinningError(SkeletalMeshSkinningAsset::Create(std::move(gap), skeleton, Context()),
                             AnimationErrors::SkinningLayoutInvalid);

        auto overlap = Candidate(skeleton);
        overlap.lods[0].sections[0].firstVertex = 0;
        RequireSkinningError(SkeletalMeshSkinningAsset::Create(std::move(overlap), skeleton, Context()),
                             AnimationErrors::SkinningLayoutInvalid);

        auto duplicate = Candidate(skeleton);
        duplicate.lods[0].sections[1].id = duplicate.lods[0].sections[0].id;
        RequireSkinningError(SkeletalMeshSkinningAsset::Create(std::move(duplicate), skeleton, Context()),
                             AnimationErrors::SkinningDuplicateIdentity);

        auto missingId = Candidate(skeleton);
        missingId.lods[0].sections[0].id = {};
        RequireSkinningError(SkeletalMeshSkinningAsset::Create(std::move(missingId), skeleton, Context()),
                             AnimationErrors::IdentityInvalid);
    }

    TEST_CASE("Skeletal mesh section palettes are bounded unique and remap-backed", "[unit][animation][skinning][palette]") {
        const SkeletonAsset skeleton = SkeletonFixture();
        auto empty = Candidate(skeleton);
        empty.lods[0].sections[0].palette.clear();
        RequireSkinningError(SkeletalMeshSkinningAsset::Create(std::move(empty), skeleton, Context()),
                             AnimationErrors::SkinningLimitExceeded);

        auto duplicate = Candidate(skeleton);
        duplicate.lods[0].sections[0].palette = {Id<SkinningJointId>(30), Id<SkinningJointId>(30)};
        RequireSkinningError(SkeletalMeshSkinningAsset::Create(std::move(duplicate), skeleton, Context()),
                             AnimationErrors::SkinningDuplicateIdentity);

        auto missing = Candidate(skeleton);
        missing.lods[0].sections[0].palette = {Id<SkinningJointId>(99)};
        RequireSkinningError(SkeletalMeshSkinningAsset::Create(std::move(missing), skeleton, Context()),
                             AnimationErrors::SkinningJointMissing);

        auto invalid = Candidate(skeleton);
        invalid.lods[0].sections[0].palette = {SkinningJointId{}};
        RequireSkinningError(SkeletalMeshSkinningAsset::Create(std::move(invalid), skeleton, Context()), AnimationErrors::IdentityInvalid);
    }

    TEST_CASE("Skeletal mesh vertex influences reject malformed missing and duplicate joints", "[unit][animation][skinning][influence]") {
        const SkeletonAsset skeleton = SkeletonFixture();
        auto empty = Candidate(skeleton);
        empty.lods[0].vertices[0].influences.clear();
        RequireSkinningError(SkeletalMeshSkinningAsset::Create(std::move(empty), skeleton, Context()),
                             AnimationErrors::SkinningLimitExceeded);

        auto nonFinite = Candidate(skeleton);
        nonFinite.lods[0].vertices[0].influences[0].weight = std::numeric_limits<float>::quiet_NaN();
        RequireSkinningError(SkeletalMeshSkinningAsset::Create(std::move(nonFinite), skeleton, Context()),
                             AnimationErrors::SkinningInfluenceInvalid);

        auto nonPositive = Candidate(skeleton);
        nonPositive.lods[0].vertices[0].influences[0].weight = 0.0F;
        RequireSkinningError(SkeletalMeshSkinningAsset::Create(std::move(nonPositive), skeleton, Context()),
                             AnimationErrors::SkinningInfluenceInvalid);

        auto duplicate = Candidate(skeleton);
        duplicate.lods[0].vertices[0].influences[1].joint = duplicate.lods[0].vertices[0].influences[0].joint;
        RequireSkinningError(SkeletalMeshSkinningAsset::Create(std::move(duplicate), skeleton, Context()),
                             AnimationErrors::SkinningInfluenceInvalid);

        auto missingPalette = Candidate(skeleton);
        missingPalette.lods[0].vertices[0].influences[0].joint = Id<SkinningJointId>(30);
        RequireSkinningError(SkeletalMeshSkinningAsset::Create(std::move(missingPalette), skeleton, Context()),
                             AnimationErrors::SkinningJointMissing);

        auto missingIdentity = Candidate(skeleton);
        missingIdentity.lods[0].vertices[0].influences[0].joint = {};
        RequireSkinningError(SkeletalMeshSkinningAsset::Create(std::move(missingIdentity), skeleton, Context()),
                             AnimationErrors::SkinningInfluenceInvalid);
    }

    TEST_CASE("Skeletal mesh skinning enforces every captured finite policy limit", "[unit][animation][skinning][limit]") {
        const SkeletonAsset skeleton = SkeletonFixture();

        auto zeroLimit = Context();
        zeroLimit.limits.maximumLods = 0;
        RequireSkinningError(SkeletalMeshSkinningAsset::Create(Candidate(skeleton), skeleton, zeroLimit),
                             AnimationErrors::SkinningLimitExceeded);

        auto hardLimit = Context();
        hardLimit.limits.maximumPaletteJoints = SkeletalMeshSkinningHardLimits::PaletteJoints + 1U;
        RequireSkinningError(SkeletalMeshSkinningAsset::Create(Candidate(skeleton), skeleton, hardLimit),
                             AnimationErrors::SkinningLimitExceeded);

        auto lodLimit = Context();
        lodLimit.limits.maximumLods = 1;
        auto tooManyLods = Candidate(skeleton);
        auto secondLod = tooManyLods.lods.front();
        secondLod.level = 1;
        tooManyLods.lods.push_back(std::move(secondLod));
        RequireSkinningError(SkeletalMeshSkinningAsset::Create(std::move(tooManyLods), skeleton, lodLimit),
                             AnimationErrors::SkinningLimitExceeded);

        auto vertexLimit = Context();
        vertexLimit.limits.maximumVerticesPerLod = 1;
        RequireSkinningError(SkeletalMeshSkinningAsset::Create(Candidate(skeleton), skeleton, vertexLimit),
                             AnimationErrors::SkinningLimitExceeded);

        auto sectionLimit = Context();
        sectionLimit.limits.maximumSectionsPerLod = 1;
        RequireSkinningError(SkeletalMeshSkinningAsset::Create(Candidate(skeleton), skeleton, sectionLimit),
                             AnimationErrors::SkinningLimitExceeded);

        auto remapLimit = Context();
        remapLimit.limits.maximumBindingJoints = 2;
        RequireSkinningError(SkeletalMeshSkinningAsset::Create(Candidate(skeleton), skeleton, remapLimit),
                             AnimationErrors::SkinningLimitExceeded);

        auto paletteLimit = Context();
        paletteLimit.limits.maximumPaletteJoints = 1;
        RequireSkinningError(SkeletalMeshSkinningAsset::Create(Candidate(skeleton), skeleton, paletteLimit),
                             AnimationErrors::SkinningLimitExceeded);

        auto influenceLimit = Context();
        influenceLimit.limits.maximumInfluencesPerVertex = 1;
        RequireSkinningError(SkeletalMeshSkinningAsset::Create(Candidate(skeleton), skeleton, influenceLimit),
                             AnimationErrors::SkinningLimitExceeded);
    }

    TEST_CASE("Skeletal mesh skinning snapshots remain detached from candidate and skeleton lifetimes",
              "[unit][animation][skinning][lifecycle]") {
        SkeletalMeshSkinningAsset snapshot = [&] {
            const SkeletonAsset skeleton = SkeletonFixture();
            auto candidate = Candidate(skeleton);
            auto created = SkeletalMeshSkinningAsset::Create(candidate, skeleton, Context());
            REQUIRE(created.HasValue());
            candidate.binding.jointRemap.clear();
            candidate.lods.clear();
            return std::move(created.Value());
        }();

        CHECK(snapshot.Data().mesh == Asset<SkeletalMeshId>(7));
        REQUIRE(snapshot.Data().binding.jointRemap.size() == 3);
        REQUIRE(snapshot.Data().lods.size() == 1);
        CHECK(snapshot.Data().lods[0].vertices.size() == 2);
    }
}  // namespace Horo::Animation
