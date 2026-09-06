#include "Horo/WorldStreaming/StreamingSourceRange.h"
#include "Horo/WorldStreaming/WorldStreamingErrors.h"
#include "WorldStreamingTestUtils.h"

#include <catch2/catch_test_macros.hpp>
#include <limits>

namespace Horo::WorldStreaming {
    namespace {
        using TestSupport::Context;
        using TestSupport::Descriptor;
        using TestSupport::IdentityFrom;
        using TestSupport::Owner;
        using TestSupport::RequireError;

        StreamingLayerId Layer() {
            return StreamingLayerId::Create(0).Value();
        }

        WorldCellQuantizationPolicy Policy() {
            return WorldCellQuantizationPolicy::Create({}, 1'000, {-10, 10, -10, 10, -10, 10}, 2).Value();
        }

        Math::WorldCoordinate64 Point(const std::int64_t x, const std::int64_t y, const std::int64_t z) {
            return Math::WorldCoordinate64::FromMillimeters(x, y, z);
        }

        Result<bool> Evaluate(const StreamingSourceShape &shape, const StreamingCellId cell = {0, 0, 0, 0, Layer()},
                              const StreamingSourceShapeSupport support = {}) {
            return EvaluateStreamingSourceCellRange(Descriptor(), Context(), shape, support, Policy(), cell);
        }

        StreamingSourceFrustum UnitCellFrustum() {
            return {.anchor = Point(500, 500, 500),
                    .planes = {{{{1.0F, 0.0F, 0.0F}, 500.0F},
                                {{-1.0F, 0.0F, 0.0F}, 499.0F},
                                {{0.0F, 1.0F, 0.0F}, 500.0F},
                                {{0.0F, -1.0F, 0.0F}, 499.0F},
                                {{0.0F, 0.0F, 1.0F}, 500.0F},
                                {{0.0F, 0.0F, -1.0F}, 499.0F}}},
                    .maximumExtentMillimeters = 500};
        }

        TEST_CASE("Streaming source spheres and boxes intersect exact cell boundaries", "[unit][world_streaming][range]") {
            const StreamingSourceShape touchingSphere = StreamingSourceSphere{Point(1'100, 500, 500), 101};
            REQUIRE(ValidateStreamingSourceShape(touchingSphere).HasValue());
            REQUIRE(Evaluate(touchingSphere).Value());

            const StreamingSourceShape separatedSphere = StreamingSourceSphere{Point(1'101, 500, 500), 101};
            REQUIRE_FALSE(Evaluate(separatedSphere).Value());

            const StreamingSourceShape touchingBox = StreamingSourceBox{Point(999, -20, 20), Point(1'200, 20, 40)};
            REQUIRE(Evaluate(touchingBox).Value());
            const StreamingSourceShape separatedBox = StreamingSourceBox{Point(1'000, -20, 20), Point(1'200, 20, 40)};
            REQUIRE_FALSE(Evaluate(separatedBox).Value());
        }

        TEST_CASE("Streaming source frusta use bounded inward half spaces", "[unit][world_streaming][range]") {
            const StreamingSourceShape frustum = UnitCellFrustum();
            REQUIRE(ValidateStreamingSourceShape(frustum).HasValue());
            REQUIRE(Evaluate(frustum).Value());
            REQUIRE_FALSE(Evaluate(frustum, {2, 0, 0, 0, Layer()}).Value());

            auto malformed = UnitCellFrustum();
            malformed.planes[0].inwardNormal = {2.0F, 0.0F, 0.0F};
            RequireError(ValidateStreamingSourceShape(StreamingSourceShape{malformed}), WorldStreamingErrors::SourceShapeInvalid);

            malformed = UnitCellFrustum();
            malformed.planes[0].distanceMillimeters = -1.0F;
            RequireError(ValidateStreamingSourceShape(StreamingSourceShape{malformed}), WorldStreamingErrors::SourceShapeInvalid);
        }

        TEST_CASE("Streaming source path volumes own bounded points and sweep an axis-aligned extent", "[unit][world_streaming][range]") {
            StreamingSourcePathVolume path;
            path.points[0] = Point(-500, 500, 500);
            path.points[1] = Point(1'500, 500, 500);
            path.pointCount = 2;
            path.halfExtentMillimeters = 25;
            REQUIRE(ValidateStreamingSourceShape(StreamingSourceShape{path}).HasValue());
            REQUIRE(Evaluate(StreamingSourceShape{path}).Value());
            REQUIRE_FALSE(Evaluate(StreamingSourceShape{path}, {0, 2, 0, 0, Layer()}).Value());

            path.pointCount = path.points.size() + 1;
            RequireError(ValidateStreamingSourceShape(StreamingSourceShape{path}), WorldStreamingErrors::SourceShapeInvalid);
        }

        TEST_CASE("Streaming path evaluation preserves millimeter precision in far-world cells", "[unit][world_streaming][range]") {
            constexpr std::int64_t Origin = std::numeric_limits<std::int64_t>::max() - 2'000;
            const auto policy = WorldCellQuantizationPolicy::Create(Point(Origin, 0, 0), 1'000, {0, 0, 0, 0, 0, 0}, 1).Value();
            StreamingSourcePathVolume path;
            path.points[0] = Point(Origin + 100, 500, 500);
            path.points[1] = Point(Origin + 900, 500, 500);
            path.pointCount = 2;
            path.halfExtentMillimeters = 1;

            REQUIRE(EvaluateStreamingSourceCellRange(Descriptor(), Context(), StreamingSourceShape{path}, {}, policy, {0, 0, 0, 0, Layer()})
                        .Value());
        }

        TEST_CASE("Streaming source range validation rejects unbounded and overflowing shapes", "[unit][world_streaming][range]") {
            RequireError(ValidateStreamingSourceShape(StreamingSourceShape{StreamingSourceSphere{{}, 0}}),
                         WorldStreamingErrors::SourceShapeInvalid);
            RequireError(ValidateStreamingSourceShape(StreamingSourceShape{StreamingSourceBox{Point(1, 0, 0), Point(0, 0, 0)}}),
                         WorldStreamingErrors::SourceShapeInvalid);
            RequireError(ValidateStreamingSourceShape(
                             StreamingSourceShape{StreamingSourceSphere{Point(std::numeric_limits<std::int64_t>::max(), 0, 0), 1}}),
                         WorldStreamingErrors::SourceShapeInvalid);
            RequireError(ValidateStreamingSourceShape(StreamingSourceShape{
                             StreamingSourceBox{Point(0, 0, 0), Point(StreamingSourceRangeLimits::MaximumExtentMillimeters + 1, 0, 0)}}),
                         WorldStreamingErrors::SourceShapeInvalid);
        }

        TEST_CASE("Streaming source range evaluation distinguishes unsupported shapes and invalid cells",
                  "[unit][world_streaming][range]") {
            StreamingSourceShapeSupport support;
            support.sphere = false;
            RequireError(Evaluate(StreamingSourceShape{StreamingSourceSphere{Point(500, 500, 500), 10}}, {0, 0, 0, 0, Layer()}, support),
                         WorldStreamingErrors::SourceShapeUnsupported);
            RequireError(Evaluate(StreamingSourceShape{StreamingSourceBox{Point(0, 0, 0), Point(1, 1, 1)}}, {11, 0, 0, 0, Layer()}),
                         WorldStreamingErrors::CellOutOfBounds);
            RequireError(Evaluate(StreamingSourceShape{StreamingSourceBox{Point(0, 0, 0), Point(1, 1, 1)}}, {0, 0, 0, 2, Layer()}),
                         WorldStreamingErrors::LodUnsupported);

            const auto overflowingPolicy =
                WorldCellQuantizationPolicy::Create(Point(std::numeric_limits<std::int64_t>::max(), 0, 0), 1'000, {0, 0, 0, 0, 0, 0}, 1);
            REQUIRE(overflowingPolicy.HasValue());
            RequireError(EvaluateStreamingSourceCellRange(Descriptor(), Context(),
                                                          StreamingSourceShape{StreamingSourceSphere{Point(0, 0, 0), 1}}, {},
                                                          overflowingPolicy.Value(), {0, 0, 0, 0, Layer()}),
                         WorldStreamingErrors::CoordinateOutOfRange);
        }

        TEST_CASE("Streaming source range evaluation preserves admission lifetime and capacity failures",
                  "[unit][world_streaming][range][lifecycle]") {
            const StreamingSourceShape shape = StreamingSourceSphere{Point(500, 500, 500), 10};

            auto stale = Context();
            stale.expectedOwner = Owner(2);
            RequireError(EvaluateStreamingSourceCellRange(Descriptor(), stale, shape, {}, Policy(), {0, 0, 0, 0, Layer()}),
                         WorldStreamingErrors::SourceOwnerStale);

            auto full = Context();
            full.activeSourceCount = full.sourceCapacity;
            RequireError(EvaluateStreamingSourceCellRange(Descriptor(), full, shape, {}, Policy(), {0, 0, 0, 0, Layer()}),
                         WorldStreamingErrors::SourceCapacityExceeded);
            full.currentRevision = IdentityFrom<StreamingSourceRevision>(1);
            REQUIRE(EvaluateStreamingSourceCellRange(Descriptor(IdentityFrom<StreamingSourceRevision>(2)), full, shape, {}, Policy(),
                                                     {0, 0, 0, 0, Layer()})
                        .Value());
            REQUIRE(full.activeSourceCount == full.sourceCapacity);

            auto closing = Context();
            const std::size_t activeCount = closing.activeSourceCount;
            closing.ownerState = StreamingSourceOwnerState::Cancelling;
            RequireError(EvaluateStreamingSourceCellRange(Descriptor(), closing, shape, {}, Policy(), {0, 0, 0, 0, Layer()}),
                         WorldStreamingErrors::SourceLifecycleUnavailable);
            REQUIRE(closing.activeSourceCount == activeCount);
            REQUIRE_FALSE(closing.currentRevision.has_value());

            closing.ownerState = StreamingSourceOwnerState::Closed;
            RequireError(EvaluateStreamingSourceCellRange(Descriptor(), closing, shape, {}, Policy(), {0, 0, 0, 0, Layer()}),
                         WorldStreamingErrors::SourceLifecycleUnavailable);
        }
    }  // namespace
}  // namespace Horo::WorldStreaming
