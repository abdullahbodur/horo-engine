#include "Horo/WorldStreaming/StreamingPriorityPolicy.h"
#include "Horo/WorldStreaming/WorldStreamingErrors.h"
#include "WorldStreamingTestUtils.h"

#include <array>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>

namespace Horo::WorldStreaming {
    namespace {
        using Catch::Approx;
        using TestSupport::IdentityFrom;
        using TestSupport::RequireError;

        [[nodiscard]] StreamingPriorityPolicyRequest PolicyRequest() {
            return {.id = IdentityFrom<StreamingPriorityPolicyId>(41),
                    .revision = IdentityFrom<StreamingPriorityPolicyRevision>(7),
                    .maximumCandidates = 8};
        }

        [[nodiscard]] StreamingPriorityPolicy Policy() {
            return StreamingPriorityPolicy::Create(PolicyRequest()).Value();
        }

        [[nodiscard]] StreamingPriorityEvaluationContext Context() {
            return {.policy = IdentityFrom<StreamingPriorityPolicyId>(41),
                    .policyRevision = IdentityFrom<StreamingPriorityPolicyRevision>(7),
                    .partition = TestSupport::World(),
                    .epoch = IdentityFrom<PartitionEpoch>(1),
                    .serviceTimeMilliseconds = 10'000,
                    .state = StreamingPriorityPolicyState::Active};
        }

        [[nodiscard]] StreamingCellPriorityCandidate Candidate(const std::uint64_t sourceId, const StreamingCellId cell,
                                                               const StreamingSourceIntent intent = StreamingSourceIntent::Camera,
                                                               const float basePriority = 10.0F) {
            return {.source = {.id = IdentityFrom<StreamingSourceId>(sourceId),
                               .owner = TestSupport::Owner(),
                               .intent = intent,
                               .priority = StreamingSourcePriority::Create(basePriority).Value(),
                               .revision = IdentityFrom<StreamingSourceRevision>(1)},
                    .cell = cell,
                    .distanceMillimeters = 9'000,
                    .priorityOverride = 1.0,
                    .queuedAtServiceMilliseconds = 5'000};
        }

        [[nodiscard]] StreamingCellId Cell(const std::int32_t x, const std::int32_t z = 0) {
            return {x, 0, z, 0, TestSupport::Layer(0)};
        }
    }  // namespace

    TEST_CASE("Streaming priority evaluates the normative distance and bounded-age formula", "[unit][world_streaming][priority]") {
        const std::array candidates{Candidate(1, Cell(1))};
        std::array<StreamingRankedCellPriority, 1> output{};
        const auto result = RankStreamingCellPriorities(Policy(), Context(), candidates, output);
        REQUIRE(result.HasValue());
        REQUIRE(result.Value() == 1);
        CHECK(output[0].ageBoost == Approx(0.5));
        CHECK(output[0].score == Approx(1.5));
        CHECK(output[0].candidate.source.id == candidates[0].source.id);
    }

    TEST_CASE("Streaming priority applies intent multipliers and explicit overrides without fallback",
              "[unit][world_streaming][priority]") {
        std::array candidates{Candidate(1, Cell(1), StreamingSourceIntent::Gameplay, 1.0F),
                              Candidate(2, Cell(2), StreamingSourceIntent::Camera, 1.0F),
                              Candidate(3, Cell(3), StreamingSourceIntent::NetworkRelevance, 1.0F),
                              Candidate(4, Cell(4), StreamingSourceIntent::Preload, 1.0F)};
        for (auto &candidate : candidates) {
            candidate.distanceMillimeters = 0;
            candidate.queuedAtServiceMilliseconds = 10'000;
        }
        candidates[2].priorityOverride = 2.0;
        std::array<StreamingRankedCellPriority, 4> output{};
        REQUIRE(RankStreamingCellPriorities(Policy(), Context(), candidates, output).HasValue());
        CHECK(output[0].candidate.source.id == IdentityFrom<StreamingSourceId>(3));
        CHECK(output[0].score == Approx(1.6));
        CHECK(output[1].candidate.source.id == IdentityFrom<StreamingSourceId>(4));
        CHECK(output[1].score == Approx(1.2));
        CHECK(output[2].candidate.source.id == IdentityFrom<StreamingSourceId>(2));
        CHECK(output[3].candidate.source.id == IdentityFrom<StreamingSourceId>(1));
    }

    TEST_CASE("Streaming priority uses canonical cell identity for equal-score ties independent of input order",
              "[unit][world_streaming][priority]") {
        auto first = Candidate(1, Cell(7, 2), StreamingSourceIntent::Camera, 0.0F);
        auto second = Candidate(2, Cell(-3, 1), StreamingSourceIntent::Camera, 0.0F);
        first.queuedAtServiceMilliseconds = 10'000;
        second.queuedAtServiceMilliseconds = 10'000;
        const std::array forward{first, second};
        const std::array reverse{second, first};
        std::array<StreamingRankedCellPriority, 2> forwardOutput{};
        std::array<StreamingRankedCellPriority, 2> reverseOutput{};
        REQUIRE(RankStreamingCellPriorities(Policy(), Context(), forward, forwardOutput).HasValue());
        REQUIRE(RankStreamingCellPriorities(Policy(), Context(), reverse, reverseOutput).HasValue());
        CHECK(forwardOutput[0].candidate.cell == Cell(-3, 1));
        CHECK(forwardOutput[1].candidate.cell == Cell(7, 2));
        CHECK(reverseOutput[0].candidate.cell == forwardOutput[0].candidate.cell);
        CHECK(reverseOutput[1].candidate.cell == forwardOutput[1].candidate.cell);
    }

    TEST_CASE("Streaming priority clamps backward service time and caps starvation boost", "[unit][world_streaming][priority]") {
        auto future = Candidate(1, Cell(1), StreamingSourceIntent::Camera, 0.0F);
        future.queuedAtServiceMilliseconds = 200'000;
        auto old = Candidate(2, Cell(2), StreamingSourceIntent::Camera, 0.0F);
        old.queuedAtServiceMilliseconds = 0;
        auto context = Context();
        context.serviceTimeMilliseconds = 100'000;
        const std::array candidates{future, old};
        std::array<StreamingRankedCellPriority, 2> output{};
        REQUIRE(RankStreamingCellPriorities(Policy(), context, candidates, output).HasValue());
        CHECK(output[0].candidate.source.id == old.source.id);
        CHECK(output[0].ageBoost == Approx(2.0));
        CHECK(output[1].ageBoost == Approx(0.0));

        context.serviceTimeMilliseconds = 10'000;
        REQUIRE(RankStreamingCellPriorities(Policy(), context, candidates, output).HasValue());
        CHECK(output[0].candidate.source.id == old.source.id);
        CHECK(output[0].ageBoost == Approx(1.0));
        CHECK(output[1].ageBoost == Approx(0.0));
    }

    TEST_CASE("Streaming priority rejects invalid policies and unsupported contract versions", "[unit][world_streaming][priority]") {
        auto request = PolicyRequest();
        request.contractVersion = 2;
        RequireError(StreamingPriorityPolicy::Create(request), WorldStreamingErrors::PriorityPolicyUnsupported);
        request = PolicyRequest();
        request.id = {};
        RequireError(StreamingPriorityPolicy::Create(request), WorldStreamingErrors::PriorityPolicyInvalid);
        request = PolicyRequest();
        request.epsilonMillimeters = 0;
        RequireError(StreamingPriorityPolicy::Create(request), WorldStreamingErrors::PriorityPolicyInvalid);
        request = PolicyRequest();
        request.intentMultipliers[0] = 0.0;
        RequireError(StreamingPriorityPolicy::Create(request), WorldStreamingErrors::PriorityPolicyInvalid);
        request = PolicyRequest();
        request.ageSlopePerSecond = 0.0;
        RequireError(StreamingPriorityPolicy::Create(request), WorldStreamingErrors::PriorityPolicyInvalid);
        request = PolicyRequest();
        request.maximumCandidates = StreamingPriorityPolicyRequest::MaximumCandidateCount + 1;
        RequireError(StreamingPriorityPolicy::Create(request), WorldStreamingErrors::PriorityPolicyInvalid);
    }

    TEST_CASE("Streaming priority validates every candidate before publishing caller output", "[unit][world_streaming][priority]") {
        auto invalid = Candidate(1, Cell(1));
        invalid.priorityOverride = 2.01;
        const std::array candidates{invalid};
        std::array<StreamingRankedCellPriority, 1> output{};
        output[0].score = 77.0;
        RequireError(RankStreamingCellPriorities(Policy(), Context(), candidates, output), WorldStreamingErrors::PriorityPolicyInvalid);
        CHECK(output[0].score == 77.0);

        invalid = Candidate(1, Cell(1));
        invalid.source.owner.partition = TestSupport::World(2);
        const std::array staleCandidates{invalid};
        RequireError(RankStreamingCellPriorities(Policy(), Context(), staleCandidates, output), WorldStreamingErrors::PriorityPolicyStale);
        CHECK(output[0].score == 77.0);

        invalid = Candidate(1, Cell(1));
        invalid.source.intent = StreamingSourceIntent::Count;
        const std::array unsupportedCandidates{invalid};
        RequireError(RankStreamingCellPriorities(Policy(), Context(), unsupportedCandidates, output),
                     WorldStreamingErrors::SourceIntentUnsupported);
        CHECK(output[0].score == 77.0);
    }

    TEST_CASE("Streaming priority capacity failure preserves output and exact boundary succeeds", "[unit][world_streaming][priority]") {
        auto request = PolicyRequest();
        request.maximumCandidates = 1;
        const auto policy = StreamingPriorityPolicy::Create(request).Value();
        const std::array candidates{Candidate(1, Cell(1)), Candidate(2, Cell(2))};
        std::array<StreamingRankedCellPriority, 2> output{};
        output[0].score = 55.0;
        RequireError(RankStreamingCellPriorities(policy, Context(), candidates, output),
                     WorldStreamingErrors::PriorityPolicyCapacityExceeded);
        CHECK(output[0].score == 55.0);

        const std::array boundary{candidates[0]};
        REQUIRE(RankStreamingCellPriorities(policy, Context(), boundary, output).HasValue());
    }

    TEST_CASE("Streaming priority fences policy replacement cancellation shutdown and partition replacement",
              "[unit][world_streaming][priority][lifecycle]") {
        const std::array candidates{Candidate(1, Cell(1))};
        std::array<StreamingRankedCellPriority, 1> output{};
        auto context = Context();
        context.policyRevision = IdentityFrom<StreamingPriorityPolicyRevision>(8);
        RequireError(RankStreamingCellPriorities(Policy(), context, candidates, output), WorldStreamingErrors::PriorityPolicyStale);
        context = Context();
        context.epoch = IdentityFrom<PartitionEpoch>(2);
        RequireError(RankStreamingCellPriorities(Policy(), context, candidates, output), WorldStreamingErrors::PriorityPolicyStale);
        context = Context();
        context.state = StreamingPriorityPolicyState::Cancelling;
        RequireError(RankStreamingCellPriorities(Policy(), context, candidates, output),
                     WorldStreamingErrors::PriorityPolicyLifecycleUnavailable);
        context.state = StreamingPriorityPolicyState::Closed;
        RequireError(RankStreamingCellPriorities(Policy(), context, candidates, output),
                     WorldStreamingErrors::PriorityPolicyLifecycleUnavailable);
        context.state = StreamingPriorityPolicyState::Count;
        RequireError(RankStreamingCellPriorities(Policy(), context, candidates, output), WorldStreamingErrors::PriorityPolicyInvalid);
    }

    TEST_CASE("Streaming priority accepts an empty bounded snapshot without touching storage", "[unit][world_streaming][priority]") {
        std::array<StreamingRankedCellPriority, 1> output{};
        output[0].score = 9.0;
        const auto result = RankStreamingCellPriorities(Policy(), Context(), {}, output);
        REQUIRE(result.HasValue());
        CHECK(result.Value() == 0);
        CHECK(output[0].score == 9.0);
    }
}  // namespace Horo::WorldStreaming
