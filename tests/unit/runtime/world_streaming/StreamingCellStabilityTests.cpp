#include "Horo/WorldStreaming/StreamingCellStability.h"
#include "Horo/WorldStreaming/WorldStreamingErrors.h"
#include "WorldStreamingTestUtils.h"

#include <catch2/catch_test_macros.hpp>
#include <limits>

namespace Horo::WorldStreaming {
    namespace {
        using TestSupport::IdentityFrom;
        using TestSupport::Layer;
        using TestSupport::RequireError;
        using TestSupport::World;

        StreamingCellStabilityPolicy Policy(const std::uint64_t revision = 1, const std::uint32_t maximumTrackedCells = 8,
                                            const std::uint64_t lingerMilliseconds = 5'000) {
            StreamingCellStabilityPolicyRequest request{};
            request.id = IdentityFrom<StreamingCellStabilityPolicyId>(10);
            request.revision = IdentityFrom<StreamingCellStabilityPolicyRevision>(revision);
            request.enterMarginMillimeters = 100;
            request.exitMarginMillimeters = 200;
            request.lingerMilliseconds = lingerMilliseconds;
            request.maximumTrackedCells = maximumTrackedCells;
            return StreamingCellStabilityPolicy::Create(request).Value();
        }

        StreamingCellStabilityContext Context(const std::uint64_t time = 1'000, const std::uint32_t tracked = 0) {
            return {.policy = IdentityFrom<StreamingCellStabilityPolicyId>(10),
                    .policyRevision = IdentityFrom<StreamingCellStabilityPolicyRevision>(1),
                    .partition = World(),
                    .epoch = IdentityFrom<PartitionEpoch>(4),
                    .serviceTimeMilliseconds = time,
                    .trackedCells = tracked,
                    .lifecycle = StreamingCellStabilityLifecycle::Active};
        }

        StreamingCellStabilityObservation Observation(const std::int64_t boundaryDistance,
                                                      const StreamingDesiredResidency residency = StreamingDesiredResidency::Activated) {
            return {.cell = {0, 0, 0, 0, Layer(2)},
                    .effectiveResidency = residency,
                    .pinnedResidencyFloor = std::nullopt,
                    .signedBoundaryDistanceMillimeters = boundaryDistance};
        }

        TEST_CASE("Cell stability applies inclusive enter and exit hysteresis boundaries",
                  "[unit][world_streaming][stability][hysteresis]") {
            const auto policy = Policy();
            const auto beforeEnter = EvaluateStreamingCellStability(policy, Context(), Observation(99), std::nullopt).Value();
            REQUIRE(beforeEnter.snapshot.phase == StreamingCellStabilityPhase::Unloaded);

            const auto admitted = EvaluateStreamingCellStability(policy, Context(), Observation(100), std::nullopt).Value();
            REQUIRE(admitted.snapshot.phase == StreamingCellStabilityPhase::Resident);
            REQUIRE(admitted.snapshot.retainedResidency == StreamingDesiredResidency::Activated);

            const auto held = EvaluateStreamingCellStability(policy, Context(1'001, 1), Observation(-200), admitted.snapshot).Value();
            REQUIRE(held.snapshot.phase == StreamingCellStabilityPhase::Resident);
            REQUIRE(held.boundaryHeld);

            const auto exited = EvaluateStreamingCellStability(policy, Context(1'002, 1), Observation(-201), held.snapshot).Value();
            REQUIRE(exited.snapshot.phase == StreamingCellStabilityPhase::Lingering);
            REQUIRE(exited.snapshot.lingerStartedAtServiceMilliseconds == 1'002);
        }

        TEST_CASE("Cell stability expires linger exactly and reentry cancels it", "[unit][world_streaming][stability][linger]") {
            const auto policy = Policy();
            const auto admitted = EvaluateStreamingCellStability(policy, Context(), Observation(100), std::nullopt).Value();
            const auto lingering = EvaluateStreamingCellStability(policy, Context(2'000, 1),
                                                                  Observation(-500, StreamingDesiredResidency::Unloaded), admitted.snapshot)
                                       .Value();
            REQUIRE(lingering.snapshot.phase == StreamingCellStabilityPhase::Lingering);

            const auto resumed = EvaluateStreamingCellStability(policy, Context(6'999, 1), Observation(-100), lingering.snapshot).Value();
            REQUIRE(resumed.snapshot.phase == StreamingCellStabilityPhase::Resident);
            REQUIRE(resumed.snapshot.lingerStartedAtServiceMilliseconds == 0);

            const auto secondLinger =
                EvaluateStreamingCellStability(policy, Context(7'000, 1), Observation(-500, StreamingDesiredResidency::Unloaded),
                                               resumed.snapshot)
                    .Value();
            const auto expired =
                EvaluateStreamingCellStability(policy, Context(12'000, 1), Observation(-500, StreamingDesiredResidency::Unloaded),
                                               secondLinger.snapshot)
                    .Value();
            REQUIRE(expired.snapshot.phase == StreamingCellStabilityPhase::Unloaded);
            REQUIRE(expired.lingerExpired);
        }

        TEST_CASE("Pinned demand bypasses geometric thresholds without bypassing record capacity",
                  "[unit][world_streaming][stability][pin][capacity]") {
            const auto policy = Policy(1, 1);
            auto pinned = Observation(-10'000, StreamingDesiredResidency::Activated);
            pinned.pinnedResidencyFloor = StreamingDesiredResidency::Loaded;
            REQUIRE(EvaluateStreamingCellStability(policy, Context(), pinned, std::nullopt).Value().snapshot.phase ==
                    StreamingCellStabilityPhase::Resident);
            RequireError(EvaluateStreamingCellStability(policy, Context(1'000, 1), pinned, std::nullopt),
                         WorldStreamingErrors::CellStabilityCapacityExceeded);
            RequireError(EvaluateStreamingCellStability(policy, Context(1'000, 2), Observation(100), std::nullopt),
                         WorldStreamingErrors::CellStabilityCapacityExceeded);

            const auto tracked = EvaluateStreamingCellStability(policy, Context(), pinned, std::nullopt).Value();
            const auto retiring =
                EvaluateStreamingCellStability(policy, Context(2'000, 2), Observation(-10'000, StreamingDesiredResidency::Unloaded),
                                               tracked.snapshot)
                    .Value();
            REQUIRE(retiring.snapshot.phase == StreamingCellStabilityPhase::Lingering);
        }

        TEST_CASE("Cell stability rejects invalid unsupported stale and closed evaluations",
                  "[unit][world_streaming][stability][failure][lifecycle]") {
            StreamingCellStabilityPolicyRequest invalid{};
            invalid.id = IdentityFrom<StreamingCellStabilityPolicyId>(10);
            invalid.revision = IdentityFrom<StreamingCellStabilityPolicyRevision>(1);
            invalid.enterMarginMillimeters = -1;
            RequireError(StreamingCellStabilityPolicy::Create(invalid), WorldStreamingErrors::CellStabilityInvalid);
            invalid.enterMarginMillimeters = 0;
            invalid.contractVersion = 2;
            RequireError(StreamingCellStabilityPolicy::Create(invalid), WorldStreamingErrors::CellStabilityUnsupported);

            const auto policy = Policy();
            auto staleContext = Context();
            staleContext.policyRevision = IdentityFrom<StreamingCellStabilityPolicyRevision>(2);
            RequireError(EvaluateStreamingCellStability(policy, staleContext, Observation(100), std::nullopt),
                         WorldStreamingErrors::CellStabilityStale);

            auto closed = Context();
            closed.lifecycle = StreamingCellStabilityLifecycle::Closed;
            RequireError(EvaluateStreamingCellStability(policy, closed, Observation(100), std::nullopt),
                         WorldStreamingErrors::CellStabilityLifecycleUnavailable);

            const auto admitted = EvaluateStreamingCellStability(policy, Context(), Observation(100), std::nullopt).Value();
            RequireError(EvaluateStreamingCellStability(policy, Context(999, 1), Observation(100), admitted.snapshot),
                         WorldStreamingErrors::CellStabilityStale);

            auto unsupported = Observation(100);
            unsupported.effectiveResidency = static_cast<StreamingDesiredResidency>(std::numeric_limits<std::uint8_t>::max());
            RequireError(EvaluateStreamingCellStability(policy, Context(), unsupported, std::nullopt),
                         WorldStreamingErrors::CellStabilityUnsupported);
        }

        TEST_CASE("Zero linger releases on first loss while no-demand cells consume no record",
                  "[unit][world_streaming][stability][boundary]") {
            const auto policy = Policy(1, 1, 0);
            const auto absent = EvaluateStreamingCellStability(policy, Context(1'000, 1),
                                                               Observation(-500, StreamingDesiredResidency::Unloaded), std::nullopt)
                                    .Value();
            REQUIRE(absent.snapshot.phase == StreamingCellStabilityPhase::Unloaded);

            const auto admitted = EvaluateStreamingCellStability(policy, Context(), Observation(100), std::nullopt).Value();
            const auto released = EvaluateStreamingCellStability(policy, Context(1'001, 1),
                                                                 Observation(-500, StreamingDesiredResidency::Unloaded), admitted.snapshot)
                                      .Value();
            REQUIRE(released.snapshot.phase == StreamingCellStabilityPhase::Unloaded);
            REQUIRE(released.lingerExpired);
        }
    }  // namespace
}  // namespace Horo::WorldStreaming
