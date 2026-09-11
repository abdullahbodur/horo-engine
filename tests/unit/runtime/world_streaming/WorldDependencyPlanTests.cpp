#include "Horo/WorldStreaming/WorldDependencyPlan.h"
#include "Horo/WorldStreaming/WorldStreamingErrors.h"
#include "WorldStreamingTestUtils.h"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <type_traits>
#include <vector>

namespace Horo::WorldStreaming {
    namespace {
        using TestSupport::IdentityFrom;
        using TestSupport::RequireError;

        Assets::AssetId Page() {
            std::array<std::uint8_t, 16> bytes{};
            bytes.front() = 1;
            return Assets::AssetId::FromBytes(bytes);
        }

        Assets::AssetId Chunk() {
            std::array<std::uint8_t, 16> bytes{};
            bytes.front() = 99;
            return Assets::AssetId::FromBytes(bytes);
        }

        StreamingLayerId Layer() {
            return StreamingLayerId::Create(2).Value();
        }

        WorldSpatialAssignment Assignments(const std::size_t objectCount = 4) {
            const auto grid = WorldCellQuantizationPolicy::Create({}, 100, {0, 0, 0, 0, 0, 0}, 1).Value();
            const std::vector<WorldLayerDescriptor> layers{
                {Layer(), "base", WorldLayerOwnership::WorldStreaming, WorldLayerFlags::Persistent, 1.0F}};
            const std::vector<WorldPartitionCellDescriptor> cells{{{0, 0, 0, 0, Layer()}, {Chunk()}}};
            auto descriptor = WorldPartitionDescriptor::Create({}, TestSupport::World(),
                                                               {Math::WorldCoordinate64::FromMillimeters(0, 0, 0),
                                                                Math::WorldCoordinate64::FromMillimeters(99, 99, 99)},
                                                               grid, layers, cells, {1, 1, 16});
            REQUIRE(descriptor.HasValue());

            std::vector<WorldSpatialAssignmentCandidate> candidates;
            for (std::size_t index = 0; index < objectCount; ++index) {
                candidates.emplace_back(WorldAuthoringObjectAddress{Page(), index + 1}, IdentityFrom<WorldAuthoringRevision>(index + 10),
                                        WorldPartitionBounds{}, Layer(), 0);
            }
            auto result = WorldSpatialAssignment::Create(descriptor.Value(), candidates, {16, 1, 16});
            REQUIRE(result.HasValue());
            return std::move(result).Value();
        }

        WorldDependencyEndpoint Endpoint(const std::uint64_t object, const std::uint64_t revision) {
            return {{Page(), object}, IdentityFrom<WorldAuthoringRevision>(revision)};
        }

        WorldDependencyCandidate Edge(const std::uint64_t source, const std::uint64_t target,
                                      const WorldDependencyKind kind = WorldDependencyKind::Hard, const std::uint64_t sourceRevision = 0,
                                      const std::uint64_t targetRevision = 0) {
            const auto resolvedSourceRevision = sourceRevision == 0 ? source + 9 : sourceRevision;
            const auto resolvedTargetRevision = targetRevision == 0 ? target + 9 : targetRevision;
            return {Endpoint(source, resolvedSourceRevision), Endpoint(target, resolvedTargetRevision), kind};
        }

        constexpr WorldDependencyPlanLimits Limits{16, 8, 8, 8};

        TEST_CASE("Dependency plan collapses hard chains diamonds and cycles into one canonical bundle",
                  "[unit][world_streaming][dependency_plan]") {
            const auto assignments = Assignments();
            std::vector dependencies{Edge(4, 2), Edge(2, 3), Edge(1, 3), Edge(1, 2), Edge(3, 1)};
            const auto original = dependencies;

            auto result = WorldDependencyPlan::Create(assignments, dependencies, Limits);
            REQUIRE(result.HasValue());
            auto plan = std::move(result).Value();

            REQUIRE(plan.Partition() == assignments.Partition());
            REQUIRE(plan.Bundles().size() == 1);
            REQUIRE(plan.MembersForBundle(0).size() == 4);
            for (std::size_t index = 0; index < 4; ++index) {
                REQUIRE(plan.MembersForBundle(0)[index] == Endpoint(index + 1, index + 10));
            }
            REQUIRE(plan.MembersForBundle(1).empty());
            REQUIRE(plan.SoftReferences().empty());
            REQUIRE(dependencies == original);
            static_assert(!std::is_copy_constructible_v<WorldDependencyPlan>);
            static_assert(std::is_move_constructible_v<WorldDependencyPlan>);
        }

        TEST_CASE("Dependency plan emits disconnected bundles by canonical first member", "[unit][world_streaming][dependency_plan]") {
            const auto assignments = Assignments(6);
            const std::array dependencies{Edge(6, 5), Edge(3, 1)};

            auto result = WorldDependencyPlan::Create(assignments, dependencies, Limits);
            REQUIRE(result.HasValue());
            REQUIRE(result.Value().Bundles().size() == 2);
            REQUIRE(result.Value().MembersForBundle(0).front() == Endpoint(1, 10));
            REQUIRE(result.Value().MembersForBundle(1).front() == Endpoint(5, 14));
        }

        TEST_CASE("Dependency plan preserves canonical resolved and unresolved soft references",
                  "[unit][world_streaming][dependency_plan]") {
            const auto assignments = Assignments();
            const std::array dependencies{Edge(3, 9, WorldDependencyKind::Soft, 12, 90), Edge(1, 2, WorldDependencyKind::Soft)};

            auto result = WorldDependencyPlan::Create(assignments, dependencies, Limits);
            REQUIRE(result.HasValue());
            REQUIRE(result.Value().Bundles().empty());
            REQUIRE(result.Value().SoftReferences().size() == 2);
            REQUIRE(result.Value().SoftReferences()[0].source.address.object == 1);
            REQUIRE(result.Value().SoftReferences()[1].target.address.object == 9);

            auto empty = WorldDependencyPlan::Create(assignments, {}, Limits);
            REQUIRE(empty.HasValue());
            REQUIRE(empty.Value().Bundles().empty());
            REQUIRE(empty.Value().SoftReferences().empty());
        }

        TEST_CASE("Dependency plan rejects soft references inside transitive hard co-load bundles",
                  "[unit][world_streaming][dependency_plan][policy]") {
            const auto assignments = Assignments();

            const std::array directConflict{Edge(1, 2), Edge(1, 2, WorldDependencyKind::Soft)};
            RequireError(WorldDependencyPlan::Create(assignments, directConflict, Limits), WorldStreamingErrors::DependencyPlanAmbiguous);

            const std::array transitiveConflict{Edge(1, 2), Edge(2, 3), Edge(3, 1, WorldDependencyKind::Soft)};
            RequireError(WorldDependencyPlan::Create(assignments, transitiveConflict, Limits),
                         WorldStreamingErrors::DependencyPlanAmbiguous);
        }

        TEST_CASE("Dependency plan permits soft-only cycles because resolution remains deferred",
                  "[unit][world_streaming][dependency_plan][policy]") {
            const auto assignments = Assignments();
            const std::array dependencies{Edge(1, 2, WorldDependencyKind::Soft), Edge(2, 1, WorldDependencyKind::Soft)};

            auto result = WorldDependencyPlan::Create(assignments, dependencies, Limits);
            REQUIRE(result.HasValue());
            REQUIRE(result.Value().Bundles().empty());
            REQUIRE(result.Value().SoftReferences().size() == 2);
        }

        TEST_CASE("Dependency plan rejects malformed self and duplicate edges", "[unit][world_streaming][dependency_plan]") {
            const auto assignments = Assignments();
            RequireError(WorldDependencyPlan::Create(assignments, {}, {0, 1, 1, 1}), WorldStreamingErrors::DependencyPlanInvalid);

            auto malformed = Edge(1, 2);
            malformed.kind = static_cast<WorldDependencyKind>(99);
            RequireError(WorldDependencyPlan::Create(assignments, std::array{malformed}, Limits),
                         WorldStreamingErrors::DependencyPlanInvalid);
            malformed = Edge(1, 2);
            malformed.source = {};
            RequireError(WorldDependencyPlan::Create(assignments, std::array{malformed}, Limits),
                         WorldStreamingErrors::DependencyPlanInvalid);
            RequireError(WorldDependencyPlan::Create(assignments, std::array{Edge(1, 1)}, Limits),
                         WorldStreamingErrors::DependencyPlanInvalid);
            RequireError(WorldDependencyPlan::Create(assignments, std::array{Edge(1, 2), Edge(1, 2)}, Limits),
                         WorldStreamingErrors::DependencyPlanInvalid);
        }

        TEST_CASE("Dependency plan distinguishes missing hard targets from missing sources", "[unit][world_streaming][dependency_plan]") {
            const auto assignments = Assignments();
            RequireError(WorldDependencyPlan::Create(assignments, std::array{Edge(9, 1, WorldDependencyKind::Soft, 90, 10)}, Limits),
                         WorldStreamingErrors::DependencyPlanInvalid);
            RequireError(WorldDependencyPlan::Create(assignments, std::array{Edge(1, 9, WorldDependencyKind::Hard, 10, 90)}, Limits),
                         WorldStreamingErrors::DependencyPlanHardTargetMissing);
        }

        TEST_CASE("Dependency plan rejects stale source hard-target and resolved-soft revisions",
                  "[unit][world_streaming][dependency_plan]") {
            const auto assignments = Assignments();
            RequireError(WorldDependencyPlan::Create(assignments, std::array{Edge(1, 2, WorldDependencyKind::Hard, 99, 11)}, Limits),
                         WorldStreamingErrors::DependencyPlanRevisionStale);
            RequireError(WorldDependencyPlan::Create(assignments, std::array{Edge(1, 2, WorldDependencyKind::Hard, 10, 99)}, Limits),
                         WorldStreamingErrors::DependencyPlanRevisionStale);
            RequireError(WorldDependencyPlan::Create(assignments, std::array{Edge(1, 2, WorldDependencyKind::Soft, 10, 99)}, Limits),
                         WorldStreamingErrors::DependencyPlanRevisionStale);
        }

        TEST_CASE("Dependency plan enforces edge hard-degree bundle and soft capacities", "[unit][world_streaming][dependency_plan]") {
            const auto assignments = Assignments();
            const std::array hard{Edge(1, 2), Edge(1, 3)};
            RequireError(WorldDependencyPlan::Create(assignments, hard, {1, 8, 8, 8}),
                         WorldStreamingErrors::DependencyPlanCapacityExceeded);
            RequireError(WorldDependencyPlan::Create(assignments, hard, {8, 1, 8, 8}),
                         WorldStreamingErrors::DependencyPlanCapacityExceeded);
            RequireError(WorldDependencyPlan::Create(assignments, hard, {8, 8, 2, 8}),
                         WorldStreamingErrors::DependencyPlanCapacityExceeded);

            const std::array soft{Edge(1, 2, WorldDependencyKind::Soft), Edge(2, 3, WorldDependencyKind::Soft)};
            RequireError(WorldDependencyPlan::Create(assignments, soft, {8, 8, 8, 1}),
                         WorldStreamingErrors::DependencyPlanCapacityExceeded);
        }
    }  // namespace
}  // namespace Horo::WorldStreaming
