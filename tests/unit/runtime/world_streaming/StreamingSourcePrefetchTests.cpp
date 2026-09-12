#include "Horo/WorldStreaming/StreamingSourcePrefetch.h"
#include "Horo/WorldStreaming/WorldStreamingErrors.h"
#include "WorldStreamingTestUtils.h"

#include <catch2/catch_test_macros.hpp>
#include <limits>

namespace Horo::WorldStreaming {
    namespace {
        using TestSupport::Descriptor;
        using TestSupport::IdentityFrom;
        using TestSupport::RequireError;
        using TestSupport::World;

        StreamingPrefetchPolicyRequest PolicyRequest() {
            return {.id = IdentityFrom<StreamingPrefetchPolicyId>(71),
                    .revision = IdentityFrom<StreamingPrefetchPolicyRevision>(4),
                    .lookaheadMilliseconds = 1'500,
                    .maximumSampleAgeMilliseconds = 250,
                    .minimumSpeedMillimetersPerSecond = 500,
                    .maximumSpeedMillimetersPerSecond = 100'000,
                    .pathHalfExtentMillimeters = 100};
        }

        StreamingPrefetchPolicy Policy() {
            return StreamingPrefetchPolicy::Create(PolicyRequest()).Value();
        }

        StreamingPrefetchEvaluationContext Context() {
            return {.policy = IdentityFrom<StreamingPrefetchPolicyId>(71),
                    .policyRevision = IdentityFrom<StreamingPrefetchPolicyRevision>(4),
                    .partition = World(),
                    .epoch = IdentityFrom<PartitionEpoch>(1),
                    .serviceTimeMilliseconds = 1'000,
                    .sourceAdmission = TestSupport::Context(),
                    .lifecycle = StreamingPrefetchLifecycle::Active};
        }

        StreamingVelocityPrefetchObservation Observation(const StreamingVelocity64 velocity = {{{1'000, 0, 0}}}) {
            return {.source = Descriptor(),
                    .position = Math::WorldCoordinate64::FromMillimeters(10'000, 20'000, 30'000),
                    .velocity = velocity,
                    .sampledAtServiceMilliseconds = 900};
        }

        TEST_CASE("Velocity prefetch emits a deterministic bounded camera path", "[unit][world_streaming][prefetch]") {
            const auto policy = Policy();
            const auto observation = Observation();
            const auto first = EvaluateStreamingVelocityPrefetch(policy, Context(), observation).Value();
            const auto second = EvaluateStreamingVelocityPrefetch(policy, Context(), observation).Value();

            REQUIRE(first == second);
            REQUIRE(first.source == observation.source);
            REQUIRE(first.admission == StreamingSourceAdmissionKind::Insert);
            REQUIRE(first.disposition == StreamingPrefetchDisposition::Path);
            REQUIRE(first.path.has_value());
            REQUIRE(first.path->pointCount == 2);
            REQUIRE(first.path->points[0] == observation.position);
            REQUIRE(first.path->points[1] == Math::WorldCoordinate64::FromMillimeters(11'500, 20'000, 30'000));
            REQUIRE(first.path->halfExtentMillimeters == 100);
            REQUIRE(ValidateStreamingSourceShape(StreamingSourceShape{*first.path}).HasValue());
        }

        TEST_CASE("Velocity prefetch applies an inclusive speed threshold without stationary demand",
                  "[unit][world_streaming][prefetch][boundary]") {
            const auto policy = Policy();
            const auto below = EvaluateStreamingVelocityPrefetch(policy, Context(), Observation({{{499, 0, 0}}})).Value();
            REQUIRE(below.disposition == StreamingPrefetchDisposition::Inactive);
            REQUIRE_FALSE(below.path.has_value());

            const auto threshold = EvaluateStreamingVelocityPrefetch(policy, Context(), Observation({{{-500, 0, 0}}})).Value();
            REQUIRE(threshold.disposition == StreamingPrefetchDisposition::Path);
            REQUIRE(threshold.path->points[1] == Math::WorldCoordinate64::FromMillimeters(9'250, 20'000, 30'000));

            auto zeroThreshold = PolicyRequest();
            zeroThreshold.minimumSpeedMillimetersPerSecond = 0;
            const auto stationary = EvaluateStreamingVelocityPrefetch(StreamingPrefetchPolicy::Create(zeroThreshold).Value(), Context(),
                                                                      Observation({{{0, 0, 0}}}))
                                        .Value();
            REQUIRE(stationary.disposition == StreamingPrefetchDisposition::Inactive);
            REQUIRE_FALSE(stationary.path.has_value());
        }

        TEST_CASE("Velocity prefetch policy rejects unsupported and unbounded requests", "[unit][world_streaming][prefetch][failure]") {
            auto request = PolicyRequest();
            request.contractVersion = 2;
            RequireError(StreamingPrefetchPolicy::Create(request), WorldStreamingErrors::PrefetchUnsupported);

            request = PolicyRequest();
            request.lookaheadMilliseconds = 0;
            RequireError(StreamingPrefetchPolicy::Create(request), WorldStreamingErrors::PrefetchInvalid);

            request = PolicyRequest();
            request.minimumSpeedMillimetersPerSecond = request.maximumSpeedMillimetersPerSecond + 1;
            RequireError(StreamingPrefetchPolicy::Create(request), WorldStreamingErrors::PrefetchInvalid);

            request = PolicyRequest();
            request.maximumSpeedMillimetersPerSecond = StreamingPrefetchPolicyRequest::MaximumSpeedMillimetersPerSecond;
            request.lookaheadMilliseconds = StreamingPrefetchPolicyRequest::MaximumLookaheadMilliseconds;
            request.pathHalfExtentMillimeters = StreamingSourceRangeLimits::MaximumExtentMillimeters;
            RequireError(StreamingPrefetchPolicy::Create(request), WorldStreamingErrors::PrefetchInvalid);
        }

        TEST_CASE("Velocity prefetch preserves source replacement and owner capacity semantics",
                  "[unit][world_streaming][prefetch][replacement][capacity]") {
            auto full = Context();
            full.sourceAdmission.activeSourceCount = full.sourceAdmission.sourceCapacity;
            RequireError(EvaluateStreamingVelocityPrefetch(Policy(), full, Observation()), WorldStreamingErrors::SourceCapacityExceeded);

            full.sourceAdmission.currentRevision = IdentityFrom<StreamingSourceRevision>(1);
            auto replacement = Observation();
            replacement.source = Descriptor(IdentityFrom<StreamingSourceRevision>(2));
            const auto replaced = EvaluateStreamingVelocityPrefetch(Policy(), full, replacement).Value();
            REQUIRE(replaced.admission == StreamingSourceAdmissionKind::Replace);
            REQUIRE(replaced.source.revision == IdentityFrom<StreamingSourceRevision>(2));

            replacement.source = Descriptor(IdentityFrom<StreamingSourceRevision>(1));
            RequireError(EvaluateStreamingVelocityPrefetch(Policy(), full, replacement), WorldStreamingErrors::SourceRevisionStale);
        }

        TEST_CASE("Velocity prefetch rejects stale samples generations and closed lifecycles",
                  "[unit][world_streaming][prefetch][stale][lifecycle]") {
            auto stalePolicy = Context();
            stalePolicy.policyRevision = IdentityFrom<StreamingPrefetchPolicyRevision>(5);
            RequireError(EvaluateStreamingVelocityPrefetch(Policy(), stalePolicy, Observation()), WorldStreamingErrors::PrefetchStale);

            auto expired = Observation();
            expired.sampledAtServiceMilliseconds = 749;
            RequireError(EvaluateStreamingVelocityPrefetch(Policy(), Context(), expired), WorldStreamingErrors::PrefetchStale);

            auto future = Observation();
            future.sampledAtServiceMilliseconds = 1'001;
            RequireError(EvaluateStreamingVelocityPrefetch(Policy(), Context(), future), WorldStreamingErrors::PrefetchInvalid);

            auto stalePartition = Observation();
            stalePartition.source.owner.epoch = IdentityFrom<PartitionEpoch>(2);
            RequireError(EvaluateStreamingVelocityPrefetch(Policy(), Context(), stalePartition), WorldStreamingErrors::PrefetchStale);

            auto cancelling = Context();
            cancelling.lifecycle = StreamingPrefetchLifecycle::Cancelling;
            RequireError(EvaluateStreamingVelocityPrefetch(Policy(), cancelling, Observation()),
                         WorldStreamingErrors::PrefetchLifecycleUnavailable);
            cancelling.lifecycle = StreamingPrefetchLifecycle::Closed;
            RequireError(EvaluateStreamingVelocityPrefetch(Policy(), cancelling, Observation()),
                         WorldStreamingErrors::PrefetchLifecycleUnavailable);

            auto invalidLifecycle = Context();
            invalidLifecycle.lifecycle = static_cast<StreamingPrefetchLifecycle>(255);
            RequireError(EvaluateStreamingVelocityPrefetch(Policy(), invalidLifecycle, Observation()),
                         WorldStreamingErrors::PrefetchInvalid);

            auto sourceCancelling = Context();
            sourceCancelling.sourceAdmission.ownerState = StreamingSourceOwnerState::Cancelling;
            RequireError(EvaluateStreamingVelocityPrefetch(Policy(), sourceCancelling, Observation()),
                         WorldStreamingErrors::SourceLifecycleUnavailable);
        }

        TEST_CASE("Velocity prefetch rejects unsupported sources excessive velocity and coordinate overflow",
                  "[unit][world_streaming][prefetch][failure][range]") {
            auto unsupported = Observation();
            unsupported.source.intent = StreamingSourceIntent::NetworkRelevance;
            RequireError(EvaluateStreamingVelocityPrefetch(Policy(), Context(), unsupported), WorldStreamingErrors::PrefetchUnsupported);

            auto unsupportedShape = Context();
            unsupportedShape.shapeSupport.pathVolume = false;
            RequireError(EvaluateStreamingVelocityPrefetch(Policy(), unsupportedShape, Observation()),
                         WorldStreamingErrors::SourceShapeUnsupported);

            RequireError(EvaluateStreamingVelocityPrefetch(Policy(), Context(), Observation({{{100'001, 0, 0}}})),
                         WorldStreamingErrors::PrefetchInvalid);

            auto overflow = Observation();
            overflow.position = Math::WorldCoordinate64::FromMillimeters(std::numeric_limits<std::int64_t>::max(), 0, 0);
            RequireError(EvaluateStreamingVelocityPrefetch(Policy(), Context(), overflow), WorldStreamingErrors::CoordinateOutOfRange);
        }
    }  // namespace
}  // namespace Horo::WorldStreaming
