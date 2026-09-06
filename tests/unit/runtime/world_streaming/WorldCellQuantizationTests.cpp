#include "Horo/WorldStreaming/WorldCellQuantization.h"
#include "Horo/WorldStreaming/WorldStreamingErrors.h"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <limits>

namespace Horo::WorldStreaming {
    namespace {
        StreamingLayerId Layer(const std::uint16_t value = 0) {
            const auto result = StreamingLayerId::Create(value);
            REQUIRE(result.HasValue());
            return result.Value();
        }

        WorldCellQuantizationPolicy Policy(const Math::WorldCoordinate64 origin = {}, const std::int64_t cellSizeMillimeters = 1'000,
                                           const std::uint8_t lodLevels = 4) {
            const auto result =
                WorldCellQuantizationPolicy::Create(origin, cellSizeMillimeters, {-100, 100, -100, 100, -100, 100}, lodLevels);
            REQUIRE(result.HasValue());
            return result.Value();
        }

        void RequireError(const Result<StreamingCellId> &result, const ErrorCodeDescriptor &descriptor) {
            REQUIRE_FALSE(result.HasValue());
            const auto &error = result.ErrorValue();
            CHECK(error.domain.Value() == descriptor.domain.Value());
            CHECK(error.code.Value() == descriptor.code.Value());
        }

        TEST_CASE("WorldCoordinate64 normalizes negative axes and round trips exact millimeters", "[unit][world_streaming][coordinate]") {
            const auto coordinate = Math::WorldCoordinate64::FromMillimeters(-1, -1'024'000, 1'024'001);
            REQUIRE(coordinate.X() == Math::WorldCoordinateAxis64{-1, 1'023'999});
            REQUIRE(coordinate.Y() == Math::WorldCoordinateAxis64{-1, 0});
            REQUIRE(coordinate.Z() == Math::WorldCoordinateAxis64{1, 1});
            REQUIRE(coordinate.Millimeters() == std::array<std::int64_t, 3>{-1, -1'024'000, 1'024'001});

            const auto minimum = Math::WorldCoordinate64::FromMillimeters(std::numeric_limits<std::int64_t>::min(), 0, 0);
            const auto maximum = Math::WorldCoordinate64::FromMillimeters(std::numeric_limits<std::int64_t>::max(), 0, 0);
            REQUIRE(minimum.Millimeters()[0] == std::numeric_limits<std::int64_t>::min());
            REQUIRE(maximum.Millimeters()[0] == std::numeric_limits<std::int64_t>::max());
        }

        TEST_CASE("WorldCoordinate64 rejects non-normalized and overflowing decoded components", "[unit][world_streaming][coordinate]") {
            REQUIRE(Math::WorldCoordinate64::Create({0, -1}, {}, {}).HasError());
            REQUIRE(Math::WorldCoordinate64::Create({0, 1'024'000}, {}, {}).HasError());
            REQUIRE(Math::WorldCoordinate64::Create({std::numeric_limits<std::int64_t>::max(), 0}, {}, {}).HasError());

            const auto valid = Math::WorldCoordinate64::FromMillimeters(std::numeric_limits<std::int64_t>::min(),
                                                                        std::numeric_limits<std::int64_t>::max(), 0);
            REQUIRE(Math::WorldCoordinate64::Create(valid.X(), valid.Y(), valid.Z()).Value() == valid);
        }

        TEST_CASE("Quantization owns negative boundaries with mathematical floor semantics", "[unit][world_streaming][quantization]") {
            const auto policy = Policy();
            REQUIRE(QuantizeWorldToCell(Math::WorldCoordinate64::FromMillimeters(0, 999, 1'000), policy, 0, Layer()).Value() ==
                    StreamingCellId{0, 0, 1, 0, Layer()});
            REQUIRE(QuantizeWorldToCell(Math::WorldCoordinate64::FromMillimeters(-1, -999, -1'000), policy, 0, Layer()).Value() ==
                    StreamingCellId{-1, -1, -1, 0, Layer()});
            REQUIRE(QuantizeWorldToCell(Math::WorldCoordinate64::FromMillimeters(-1'001, -2'000, 2'000), policy, 0, Layer()).Value() ==
                    StreamingCellId{-2, -2, 2, 0, Layer()});
        }

        TEST_CASE("Quantization subtracts exact grid origin before applying half-open ownership", "[unit][world_streaming][quantization]") {
            const auto origin = Math::WorldCoordinate64::FromMillimeters(-10'000, 25'000, 7);
            const auto policy = Policy(origin);
            REQUIRE(QuantizeWorldToCell(origin, policy, 0, Layer(3)).Value() == StreamingCellId{0, 0, 0, 0, Layer(3)});
            REQUIRE(QuantizeWorldToCell(Math::WorldCoordinate64::FromMillimeters(-10'001, 25'999, 1'006), policy, 0, Layer(3)).Value() ==
                    StreamingCellId{-1, 0, 0, 0, Layer(3)});
            REQUIRE(QuantizeWorldToCell(Math::WorldCoordinate64::FromMillimeters(-9'000, 26'000, 1'007), policy, 0, Layer(3)).Value() ==
                    StreamingCellId{1, 1, 1, 0, Layer(3)});
        }

        TEST_CASE("LOD scales cell size deterministically without dropping identity dimensions", "[unit][world_streaming][quantization]") {
            const auto result = QuantizeWorldToCell(Math::WorldCoordinate64::FromMillimeters(3'999, -4'001, 8'000), Policy(), 2, Layer(9));
            REQUIRE(result.Value() == StreamingCellId{0, -2, 2, 2, Layer(9)});
        }

        TEST_CASE("Invalid policies fail atomically before they can be used", "[unit][world_streaming][quantization]") {
            const WorldCellBounds ordered{-1, 1, -1, 1, -1, 1};
            REQUIRE(WorldCellQuantizationPolicy::Create({}, 0, ordered, 1).HasError());
            REQUIRE(WorldCellQuantizationPolicy::Create({}, 1, {1, -1, -1, 1, -1, 1}, 1).HasError());
            REQUIRE(WorldCellQuantizationPolicy::Create({}, 1, ordered, 0).HasError());
            REQUIRE(WorldCellQuantizationPolicy::Create({}, 1, ordered, 33).HasError());
            REQUIRE(WorldCellQuantizationPolicy::Create({}, std::numeric_limits<std::int64_t>::max(), ordered, 2).HasError());
        }

        TEST_CASE("Quantization returns typed failures for unsupported or unrepresentable requests",
                  "[unit][world_streaming][quantization]") {
            const auto policy = Policy();
            RequireError(QuantizeWorldToCell({}, policy, 4, Layer()), WorldStreamingErrors::LodUnsupported);
            RequireError(QuantizeWorldToCell({}, policy, 0, {}), WorldStreamingErrors::IdentityInvalid);
            RequireError(QuantizeWorldToCell(Math::WorldCoordinate64::FromMillimeters(101'000, 0, 0), policy, 0, Layer()),
                         WorldStreamingErrors::CellOutOfBounds);

            const auto extremePolicy = Policy(Math::WorldCoordinate64::FromMillimeters(std::numeric_limits<std::int64_t>::min(), 0, 0));
            RequireError(QuantizeWorldToCell(Math::WorldCoordinate64::FromMillimeters(std::numeric_limits<std::int64_t>::max(), 0, 0),
                                             extremePolicy, 0, Layer()),
                         WorldStreamingErrors::CoordinateOutOfRange);
        }
    }  // namespace
}  // namespace Horo::WorldStreaming
