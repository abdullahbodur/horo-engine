#include "Horo/Navigation/NavigationErrors.h"
#include "Horo/Navigation/NavigationSourceGeometry.h"
#include "navigation/NavigationTestAssertions.h"

#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <limits>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

namespace Horo::Navigation {
    static_assert(!std::is_copy_constructible_v<NavigationSourceGeometrySnapshot>);
    static_assert(!std::is_copy_assignable_v<NavigationSourceGeometrySnapshot>);
    static_assert(std::is_nothrow_move_constructible_v<NavigationSourceGeometrySnapshot>);
    static_assert(!std::is_move_assignable_v<NavigationSourceGeometrySnapshot>);

    namespace {
        using TestSupport::RequireError;

        template <typename Identity> [[nodiscard]] Identity Id(const std::uint64_t value) {
            return Identity::Create(value).Value();
        }

        [[nodiscard]] Sha256Digest Digest(const std::uint8_t seed) {
            Sha256Digest digest{};
            for (std::size_t index = 0; index < digest.bytes.size(); ++index)
                digest.bytes[index] = static_cast<std::uint8_t>(seed + index);
            return digest;
        }

        struct OwnedContribution final {
            NavigationSourceProducerKind kind{NavigationSourceProducerKind::StaticCollider};
            NavigationSourceProducerId producer{Id<NavigationSourceProducerId>(1)};
            NavigationSourceContributionId contribution{Id<NavigationSourceContributionId>(1)};
            NavigationSourceRevision revision{Id<NavigationSourceRevision>(1)};
            Sha256Digest digest{Digest(1)};
            Math::Transform transform{};
            std::vector<Math::Vec3> vertices{{0.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 1.0F}};
            std::vector<NavigationSourceTriangleInput> triangles{
                {.vertexIndices = {0, 1, 2}, .area = Id<NavigationAreaId>(1), .materialSlot = {.value = 7}}};

            [[nodiscard]] NavigationSourceContributionInput View() const {
                return {
                    .kind = kind,
                    .producer = producer,
                    .contribution = contribution,
                    .revision = revision,
                    .contentDigest = digest,
                    .localToCanonicalMeters = transform,
                    .vertices = vertices,
                    .triangles = triangles,
                };
            }

            [[nodiscard]] NavigationSourceObservation Observation() const {
                return {.kind = kind, .producer = producer, .contribution = contribution, .revision = revision, .contentDigest = digest};
            }
        };

    }  // namespace

    TEST_CASE("Source snapshot owns canonical transformed triangles and complete authored provenance",
              "[unit][navigation][source_geometry]") {
        std::array<OwnedContribution, 4> owned{};
        const std::array kinds{NavigationSourceProducerKind::ApprovedCustom, NavigationSourceProducerKind::ProceduralGeneration,
                               NavigationSourceProducerKind::Terrain, NavigationSourceProducerKind::StaticCollider};
        for (std::size_t index = 0; index < owned.size(); ++index) {
            owned[index].kind = kinds[index];
            owned[index].producer = Id<NavigationSourceProducerId>(4 - index);
            owned[index].contribution = Id<NavigationSourceContributionId>(20 + index);
            owned[index].revision = Id<NavigationSourceRevision>(30 + index);
            owned[index].digest = Digest(static_cast<std::uint8_t>(40 + index));
            owned[index].transform.translation = {10.0F, 2.0F, -3.0F};
            owned[index].transform.scale = {2.0F, 2.0F, 2.0F};
        }
        std::array<NavigationSourceContributionInput, 4> inputs{};
        for (std::size_t index = 0; index < inputs.size(); ++index)
            inputs[index] = owned[index].View();

        auto snapshot = std::move(NavigationSourceGeometrySnapshot::Create(Id<NavigationSourceSnapshotRevision>(9), inputs)).Value();
        owned.back().vertices.front() = {99.0F, 99.0F, 99.0F};
        owned.back().triangles.front().materialSlot.value = 99;

        REQUIRE(snapshot.Revision() == Id<NavigationSourceSnapshotRevision>(9));
        REQUIRE(snapshot.Limits() == NavigationSourceGeometryLimits{});
        REQUIRE(snapshot.Contributions().size() == 4);
        REQUIRE(snapshot.Vertices().size() == 12);
        REQUIRE(snapshot.Triangles().size() == 4);
        REQUIRE(snapshot.Contributions().front().producer == Id<NavigationSourceProducerId>(1));
        REQUIRE(snapshot.Contributions().back().producer == Id<NavigationSourceProducerId>(4));
        REQUIRE(snapshot.Vertices().front() == Math::Vec3{10.0F, 2.0F, -3.0F});
        REQUIRE(snapshot.Vertices()[1] == Math::Vec3{12.0F, 2.0F, -3.0F});
        REQUIRE(snapshot.Triangles().front().materialSlot.value == 7);
        REQUIRE(snapshot.Triangles().front().provenance.kind == NavigationSourceProducerKind::StaticCollider);
        REQUIRE(snapshot.Triangles().front().provenance.producer == snapshot.Contributions().front().producer);
        REQUIRE(snapshot.Triangles().front().provenance.contribution == snapshot.Contributions().front().contribution);
        REQUIRE(snapshot.Triangles().front().provenance.sourceTriangleIndex == 0);
    }

    TEST_CASE("Source snapshot ordering is deterministic across caller order", "[unit][navigation][source_geometry]") {
        OwnedContribution low;
        OwnedContribution high;
        low.producer = Id<NavigationSourceProducerId>(2);
        low.contribution = Id<NavigationSourceContributionId>(2);
        high.producer = Id<NavigationSourceProducerId>(8);
        high.contribution = Id<NavigationSourceContributionId>(1);
        high.vertices[1].x = 2.0F;
        const std::array forward{low.View(), high.View()};
        const std::array reverse{high.View(), low.View()};

        const auto first = std::move(NavigationSourceGeometrySnapshot::Create(Id<NavigationSourceSnapshotRevision>(1), forward)).Value();
        const auto second = std::move(NavigationSourceGeometrySnapshot::Create(Id<NavigationSourceSnapshotRevision>(1), reverse)).Value();

        REQUIRE(first.Contributions().front().producer == second.Contributions().front().producer);
        REQUIRE(std::ranges::equal(first.Vertices(), second.Vertices()));
        REQUIRE(std::ranges::equal(first.Triangles(), second.Triangles()));
    }

    TEST_CASE("Source snapshot rejects unsupported producers malformed identity transforms and topology",
              "[unit][navigation][source_geometry]") {
        OwnedContribution owned;
        auto input = owned.View();
        const auto revision = Id<NavigationSourceSnapshotRevision>(1);

        input.kind = static_cast<NavigationSourceProducerKind>(255);
        RequireError(NavigationSourceGeometrySnapshot::Create(revision, std::span{&input, 1}), NavigationErrors::SourceGeometryUnsupported);
        input = owned.View();
        input.producer = {};
        RequireError(NavigationSourceGeometrySnapshot::Create(revision, std::span{&input, 1}), NavigationErrors::SourceGeometryInvalid);
        input = owned.View();
        input.revision = {};
        RequireError(NavigationSourceGeometrySnapshot::Create(revision, std::span{&input, 1}), NavigationErrors::SourceGeometryInvalid);
        input = owned.View();
        input.vertices = {};
        RequireError(NavigationSourceGeometrySnapshot::Create(revision, std::span{&input, 1}), NavigationErrors::SourceGeometryInvalid);
        input = owned.View();
        input.triangles = {};
        RequireError(NavigationSourceGeometrySnapshot::Create(revision, std::span{&input, 1}), NavigationErrors::SourceGeometryInvalid);
        input = owned.View();
        input.localToCanonicalMeters.scale.x = 0.0F;
        RequireError(NavigationSourceGeometrySnapshot::Create(revision, std::span{&input, 1}), NavigationErrors::SourceGeometryInvalid);
        input = owned.View();
        input.localToCanonicalMeters.rotation.w = 0.0F;
        RequireError(NavigationSourceGeometrySnapshot::Create(revision, std::span{&input, 1}), NavigationErrors::SourceGeometryInvalid);
        input = owned.View();
        input.localToCanonicalMeters.translation.x = std::numeric_limits<float>::infinity();
        RequireError(NavigationSourceGeometrySnapshot::Create(revision, std::span{&input, 1}), NavigationErrors::SourceGeometryInvalid);

        owned.vertices.front().x = std::numeric_limits<float>::quiet_NaN();
        input = owned.View();
        RequireError(NavigationSourceGeometrySnapshot::Create(revision, std::span{&input, 1}), NavigationErrors::SourceGeometryInvalid);
        owned.vertices.front() = {0.0F, 0.0F, 0.0F};
        owned.triangles.front().vertexIndices = {0, 1, 3};
        input = owned.View();
        RequireError(NavigationSourceGeometrySnapshot::Create(revision, std::span{&input, 1}), NavigationErrors::SourceGeometryInvalid);
        owned.triangles.front().vertexIndices = {0, 1, 1};
        input = owned.View();
        RequireError(NavigationSourceGeometrySnapshot::Create(revision, std::span{&input, 1}), NavigationErrors::SourceGeometryInvalid);
        owned.triangles.front().vertexIndices = {0, 1, 2};
        owned.triangles.front().area = {};
        input = owned.View();
        RequireError(NavigationSourceGeometrySnapshot::Create(revision, std::span{&input, 1}), NavigationErrors::SourceGeometryInvalid);
        owned.triangles.front().area = Id<NavigationAreaId>(1);
        owned.vertices[2] = {2.0F, 0.0F, 0.0F};
        input = owned.View();
        RequireError(NavigationSourceGeometrySnapshot::Create(revision, std::span{&input, 1}), NavigationErrors::SourceGeometryInvalid);
    }

    TEST_CASE("Source snapshot rejects empty duplicate and over-capacity captures transactionally", "[unit][navigation][source_geometry]") {
        OwnedContribution owned;
        const auto input = owned.View();
        const auto revision = Id<NavigationSourceSnapshotRevision>(1);
        RequireError(NavigationSourceGeometrySnapshot::Create({}, std::span{&input, 1}), NavigationErrors::SourceGeometryInvalid);
        RequireError(NavigationSourceGeometrySnapshot::Create(revision, {}), NavigationErrors::SourceGeometryInvalid);
        const std::array duplicates{input, input};
        RequireError(NavigationSourceGeometrySnapshot::Create(revision, duplicates), NavigationErrors::DescriptorConflict);

        NavigationSourceGeometryLimits limits{};
        limits.maxContributions = 0;
        RequireError(NavigationSourceGeometrySnapshot::Create(revision, std::span{&input, 1}, limits),
                     NavigationErrors::SourceGeometryInvalid);
        limits = {};
        limits.maxVertices = NavigationSourceGeometryLimits::MaximumVertices + 1;
        RequireError(NavigationSourceGeometrySnapshot::Create(revision, std::span{&input, 1}, limits),
                     NavigationErrors::SourceGeometryInvalid);
        OwnedContribution second;
        second.producer = Id<NavigationSourceProducerId>(2);
        second.contribution = Id<NavigationSourceContributionId>(2);
        const std::array distinct{input, second.View()};
        limits = {};
        limits.maxContributions = 1;
        RequireError(NavigationSourceGeometrySnapshot::Create(revision, distinct, limits),
                     NavigationErrors::SourceGeometryCapacityExceeded);
        limits = {};
        limits.maxVertices = 2;
        RequireError(NavigationSourceGeometrySnapshot::Create(revision, std::span{&input, 1}, limits),
                     NavigationErrors::SourceGeometryCapacityExceeded);
        limits = {};
        limits.maxTriangles = 1;
        limits.maxOwnedBytes = 1;
        RequireError(NavigationSourceGeometrySnapshot::Create(revision, std::span{&input, 1}, limits),
                     NavigationErrors::SourceGeometryCapacityExceeded);
    }

    TEST_CASE("Source snapshot freshness requires the exact complete revision and digest set",
              "[unit][navigation][source_geometry][stale]") {
        OwnedContribution first;
        OwnedContribution second;
        second.producer = Id<NavigationSourceProducerId>(2);
        second.contribution = Id<NavigationSourceContributionId>(2);
        second.revision = Id<NavigationSourceRevision>(2);
        second.digest = Digest(2);
        const std::array inputs{second.View(), first.View()};
        const auto snapshotRevision = Id<NavigationSourceSnapshotRevision>(5);
        const auto snapshot = std::move(NavigationSourceGeometrySnapshot::Create(snapshotRevision, inputs)).Value();
        std::array observations{second.Observation(), first.Observation()};

        REQUIRE(snapshot.ValidateCurrent(snapshotRevision, observations).HasValue());
        RequireError(snapshot.ValidateCurrent(Id<NavigationSourceSnapshotRevision>(6), observations),
                     NavigationErrors::SourceGeometryStale);
        RequireError(snapshot.ValidateCurrent(snapshotRevision, std::span{observations}.first(1)), NavigationErrors::SourceGeometryStale);
        const std::array extra{second.Observation(), first.Observation(), first.Observation()};
        RequireError(snapshot.ValidateCurrent(snapshotRevision, extra), NavigationErrors::SourceGeometryStale);
        observations.front().revision = Id<NavigationSourceRevision>(9);
        RequireError(snapshot.ValidateCurrent(snapshotRevision, observations), NavigationErrors::SourceGeometryStale);
        observations = {second.Observation(), first.Observation()};
        observations.front().contentDigest = Digest(99);
        RequireError(snapshot.ValidateCurrent(snapshotRevision, observations), NavigationErrors::SourceGeometryStale);
        observations = {second.Observation(), first.Observation()};
        observations.front().kind = NavigationSourceProducerKind::Terrain;
        RequireError(snapshot.ValidateCurrent(snapshotRevision, observations), NavigationErrors::SourceGeometryStale);
        observations.front().kind = static_cast<NavigationSourceProducerKind>(255);
        RequireError(snapshot.ValidateCurrent(snapshotRevision, observations), NavigationErrors::SourceGeometryUnsupported);
        observations = {second.Observation(), first.Observation()};
        observations.front().producer = {};
        RequireError(snapshot.ValidateCurrent(snapshotRevision, observations), NavigationErrors::SourceGeometryInvalid);
        RequireError(snapshot.ValidateCurrent({}, observations), NavigationErrors::SourceGeometryInvalid);
    }
}  // namespace Horo::Navigation
