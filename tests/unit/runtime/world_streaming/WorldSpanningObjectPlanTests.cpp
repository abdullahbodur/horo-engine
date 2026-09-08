#include "Horo/WorldStreaming/WorldSpanningObjectPlan.h"
#include "Horo/WorldStreaming/WorldStreamingErrors.h"
#include "WorldStreamingTestUtils.h"

#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <type_traits>
#include <utility>
#include <vector>

namespace Horo::WorldStreaming {
    namespace {
        using TestSupport::Asset;
        using TestSupport::IdentityFrom;
        using TestSupport::RequireError;

        StreamingLayerId Layer() {
            return TestSupport::Layer(3);
        }

        WorldPartitionDescriptor Descriptor() {
            const auto grid = WorldCellQuantizationPolicy::Create({}, 100, {-2, 2, 0, 0, 0, 0}, 1).Value();
            const std::vector<WorldLayerDescriptor> layers{
                {Layer(), "spatial", WorldLayerOwnership::WorldStreaming, WorldLayerFlags::Persistent, 1.0F}};
            std::vector<WorldPartitionCellDescriptor> cells;
            for (std::int32_t x = -2; x <= 2; ++x)
                cells.push_back({{x, 0, 0, 0, Layer()}, {Asset(static_cast<std::uint8_t>(x + 3))}});
            auto result = WorldPartitionDescriptor::Create({}, TestSupport::World(),
                                                           {Math::WorldCoordinate64::FromMillimeters(-200, 0, 0),
                                                            Math::WorldCoordinate64::FromMillimeters(299, 99, 99)},
                                                           grid, layers, cells, {1, 5, 16});
            REQUIRE(result.HasValue());
            return std::move(result).Value();
        }

        WorldSpatialAssignmentCandidate Candidate(const std::uint64_t object, const std::int64_t minimum, const std::int64_t maximum,
                                                  const WorldAuthoringRevision revision = IdentityFrom<WorldAuthoringRevision>(1)) {
            return {{Asset(9), object},
                    revision,
                    {Math::WorldCoordinate64::FromMillimeters(minimum, 0, 0), Math::WorldCoordinate64::FromMillimeters(maximum, 0, 0)},
                    Layer(),
                    0};
        }

        WorldSpatialAssignment Assign(const std::span<const WorldSpatialAssignmentCandidate> candidates) {
            auto result = WorldSpatialAssignment::Create(Descriptor(), candidates, {8, 8, 32});
            REQUIRE(result.HasValue());
            return std::move(result).Value();
        }

        WorldSpanningObjectDirective Directive(const WorldSpatialAssignmentCandidate &candidate, const WorldSpanningObjectPolicy policy,
                                               const StreamingCellId ownerCell = {}) {
            return {candidate.address, candidate.revision, policy, ownerCell};
        }

        constexpr WorldSpanningObjectPlanLimits Limits{8, 2, 16};

        TEST_CASE("Spanning object plan selects explicit owner split and non-spatial placements canonically",
                  "[unit][world_streaming][spanning_object]") {
            const std::array candidates{Candidate(1, 0, 99), Candidate(2, -1, 199), Candidate(3, -1, 199), Candidate(4, -1, 199)};
            auto assignments = Assign(candidates);
            std::vector directives{
                Directive(candidates[3], WorldSpanningObjectPolicy::NonSpatial),
                Directive(candidates[2], WorldSpanningObjectPolicy::SplitPerCell),
                Directive(candidates[1], WorldSpanningObjectPolicy::SingleCellOwner, {0, 0, 0, 0, Layer()}),
            };
            const auto originalDirectives = directives;
            const std::vector<WorldSpatialAssignmentEntry> originalObjects{assignments.Objects().begin(), assignments.Objects().end()};

            auto result = WorldSpanningObjectPlan::Create(assignments, directives, Limits);
            REQUIRE(result.HasValue());
            auto plan = std::move(result).Value();

            REQUIRE(plan.Partition() == assignments.Partition());
            REQUIRE(plan.Objects().size() == 4);
            CHECK(plan.Objects()[0].placement == WorldSpanningObjectPlacement::Direct);
            REQUIRE(std::ranges::equal(plan.CellsForObject(0), std::span{&assignments.CellsForObject(0).front(), 1}));
            CHECK(plan.Objects()[1].placement == WorldSpanningObjectPlacement::SingleCellOwner);
            REQUIRE(std::ranges::equal(plan.CellsForObject(1), std::span{&directives[2].ownerCell, 1}));
            CHECK(plan.Objects()[2].placement == WorldSpanningObjectPlacement::SplitPerCell);
            REQUIRE(std::ranges::equal(plan.CellsForObject(2), assignments.CellsForObject(2)));
            CHECK(plan.Objects()[3].placement == WorldSpanningObjectPlacement::NonSpatial);
            CHECK(plan.CellsForObject(3).empty());
            CHECK(plan.CellsForObject(4).empty());
            CHECK(directives == originalDirectives);
            CHECK(std::ranges::equal(assignments.Objects(), originalObjects));
            static_assert(!std::is_copy_constructible_v<WorldSpanningObjectPlan>);
            static_assert(std::is_move_constructible_v<WorldSpanningObjectPlan>);
        }

        TEST_CASE("Spanning object plan keeps exact-threshold objects direct", "[unit][world_streaming][spanning_object]") {
            const std::array candidates{Candidate(1, -1, 99)};
            auto assignments = Assign(candidates);

            auto result = WorldSpanningObjectPlan::Create(assignments, {}, Limits);
            REQUIRE(result.HasValue());
            CHECK(result.Value().Objects().front().placement == WorldSpanningObjectPlacement::Direct);
            REQUIRE(std::ranges::equal(result.Value().CellsForObject(0), assignments.CellsForObject(0)));
        }

        TEST_CASE("Spanning object plan requires directives only for oversized objects", "[unit][world_streaming][spanning_object]") {
            const auto oversized = Candidate(1, -1, 199);
            auto oversizedAssignments = Assign(std::array{oversized});
            RequireError(WorldSpanningObjectPlan::Create(oversizedAssignments, {}, Limits),
                         WorldStreamingErrors::SpanningObjectPlanInvalid);

            const auto direct = Candidate(1, 0, 99);
            auto directAssignments = Assign(std::array{direct});
            const std::array unused{Directive(direct, WorldSpanningObjectPolicy::NonSpatial)};
            RequireError(WorldSpanningObjectPlan::Create(directAssignments, unused, Limits),
                         WorldStreamingErrors::SpanningObjectPlanInvalid);
        }

        TEST_CASE("Spanning object plan rejects duplicate foreign and stale directives", "[unit][world_streaming][spanning_object]") {
            const auto candidate = Candidate(1, -1, 199);
            auto assignments = Assign(std::array{candidate});
            const auto split = Directive(candidate, WorldSpanningObjectPolicy::SplitPerCell);

            RequireError(WorldSpanningObjectPlan::Create(assignments, std::array{split, split}, Limits),
                         WorldStreamingErrors::SpanningObjectPlanIdentityConflict);

            auto foreign = split;
            foreign.address.object = 9;
            RequireError(WorldSpanningObjectPlan::Create(assignments, std::array{foreign}, Limits),
                         WorldStreamingErrors::SpanningObjectPlanIdentityConflict);

            auto stale = split;
            stale.revision = IdentityFrom<WorldAuthoringRevision>(2);
            RequireError(WorldSpanningObjectPlan::Create(assignments, std::array{stale}, Limits),
                         WorldStreamingErrors::SpanningObjectPlanRevisionStale);
        }

        TEST_CASE("Spanning object plan validates policy and exact covered owner cells", "[unit][world_streaming][spanning_object]") {
            const auto candidate = Candidate(1, -1, 199);
            auto assignments = Assign(std::array{candidate});

            auto directive = Directive(candidate, static_cast<WorldSpanningObjectPolicy>(255));
            RequireError(WorldSpanningObjectPlan::Create(assignments, std::array{directive}, Limits),
                         WorldStreamingErrors::SpanningObjectPlanUnsupported);

            directive = Directive(candidate, WorldSpanningObjectPolicy::SingleCellOwner);
            RequireError(WorldSpanningObjectPlan::Create(assignments, std::array{directive}, Limits),
                         WorldStreamingErrors::SpanningObjectPlanInvalid);

            directive = Directive(candidate, WorldSpanningObjectPolicy::SplitPerCell, {0, 0, 0, 0, Layer()});
            RequireError(WorldSpanningObjectPlan::Create(assignments, std::array{directive}, Limits),
                         WorldStreamingErrors::SpanningObjectPlanInvalid);

            directive = Directive(candidate, WorldSpanningObjectPolicy::SingleCellOwner, {2, 0, 0, 0, Layer()});
            RequireError(WorldSpanningObjectPlan::Create(assignments, std::array{directive}, Limits),
                         WorldStreamingErrors::SpanningObjectPlanOwnerUnavailable);
        }

        TEST_CASE("Spanning object plan applies mandatory ceilings transactionally", "[unit][world_streaming][spanning_object]") {
            const auto candidate = Candidate(1, -1, 199);
            auto assignments = Assign(std::array{candidate});
            const std::array split{Directive(candidate, WorldSpanningObjectPolicy::SplitPerCell)};

            RequireError(WorldSpanningObjectPlan::Create(assignments, split, {0, 2, 8}), WorldStreamingErrors::SpanningObjectPlanInvalid);
            RequireError(WorldSpanningObjectPlan::Create(assignments, split, {8, 0, 8}), WorldStreamingErrors::SpanningObjectPlanInvalid);
            RequireError(WorldSpanningObjectPlan::Create(assignments, split, {8, 2, 0}), WorldStreamingErrors::SpanningObjectPlanInvalid);
            RequireError(WorldSpanningObjectPlan::Create(assignments, split, {8, 2, 2}),
                         WorldStreamingErrors::SpanningObjectPlanCapacityExceeded);

            const std::array directCandidates{Candidate(1, 0, 99), Candidate(2, 0, 99)};
            auto directAssignments = Assign(directCandidates);
            RequireError(WorldSpanningObjectPlan::Create(directAssignments, {}, {1, 2, 8}),
                         WorldStreamingErrors::SpanningObjectPlanCapacityExceeded);
        }
    }  // namespace
}  // namespace Horo::WorldStreaming
