#include "Horo/Math/WorldCoordinate64.h"

#include <limits>

namespace Horo::Math {
    namespace WorldCoordinateErrors {
        namespace {
            const ErrorDomainId Domain{"horo.runtime.precision"};
        }

        const ErrorCodeDescriptor CoordinateOutOfRange{
            .domain = Domain,
            .code = ErrorCode{"precision.coordinate.out_of_range"},
            .defaultSeverity = ErrorSeverity::Error,
            .summary = "A hierarchical global coordinate is not normalized or exceeds signed 64-bit millimeters.",
            .remediationHint = "Use offsets inside the canonical cell and coordinates with an exact signed 64-bit millimeter round trip.",
            .retryable = false,
            .userActionable = true,
        };
    }  // namespace WorldCoordinateErrors

    namespace {
        [[nodiscard]] WorldCoordinateAxis64 NormalizeAxis(const std::int64_t millimeters) noexcept {
            std::int64_t cell = millimeters / WorldCoordinate64::CanonicalCellSizeMillimeters;
            std::int64_t offset = millimeters % WorldCoordinate64::CanonicalCellSizeMillimeters;
            if (offset < 0) {
                --cell;
                offset += WorldCoordinate64::CanonicalCellSizeMillimeters;
            }
            return {.cellIndex = cell, .offsetMillimeters = static_cast<std::int32_t>(offset)};
        }

        [[nodiscard]] bool IsNormalized(const WorldCoordinateAxis64 axis) noexcept {
            return axis.offsetMillimeters >= 0 && axis.offsetMillimeters < WorldCoordinate64::CanonicalCellSizeMillimeters;
        }

        [[nodiscard]] bool FitsMillimeters(const WorldCoordinateAxis64 axis) noexcept {
            static const WorldCoordinateAxis64 minimum = NormalizeAxis(std::numeric_limits<std::int64_t>::min());
            static const WorldCoordinateAxis64 maximum = NormalizeAxis(std::numeric_limits<std::int64_t>::max());
            return axis >= minimum && axis <= maximum;
        }

        [[nodiscard]] std::int64_t ToMillimeters(const WorldCoordinateAxis64 axis) noexcept {
            if (axis.cellIndex < 0) {
                // Shift one cell toward zero before multiplication so the intermediate cannot underflow before adding the normalized
                // offset.
                return (axis.cellIndex + 1) * WorldCoordinate64::CanonicalCellSizeMillimeters -
                       (WorldCoordinate64::CanonicalCellSizeMillimeters - axis.offsetMillimeters);
            }
            return axis.cellIndex * WorldCoordinate64::CanonicalCellSizeMillimeters + axis.offsetMillimeters;
        }
    }  // namespace

    /** @copydoc WorldCoordinate64::FromMillimeters */
    WorldCoordinate64 WorldCoordinate64::FromMillimeters(const std::int64_t xMillimeters, const std::int64_t yMillimeters,
                                                         const std::int64_t zMillimeters) noexcept {
        return WorldCoordinate64{{NormalizeAxis(xMillimeters), NormalizeAxis(yMillimeters), NormalizeAxis(zMillimeters)}};
    }

    /** @copydoc WorldCoordinate64::Create */
    Result<WorldCoordinate64> WorldCoordinate64::Create(const WorldCoordinateAxis64 x, const WorldCoordinateAxis64 y,
                                                        const WorldCoordinateAxis64 z) {
        if (!IsNormalized(x) || !IsNormalized(y) || !IsNormalized(z) || !FitsMillimeters(x) || !FitsMillimeters(y) || !FitsMillimeters(z))
            return Result<WorldCoordinate64>::Failure(MakeError(WorldCoordinateErrors::CoordinateOutOfRange));
        return Result<WorldCoordinate64>::Success(WorldCoordinate64{{x, y, z}});
    }

    /** @copydoc WorldCoordinate64::Millimeters */
    std::array<std::int64_t, 3> WorldCoordinate64::Millimeters() const noexcept {
        return {ToMillimeters(axes_[0]), ToMillimeters(axes_[1]), ToMillimeters(axes_[2])};
    }
}  // namespace Horo::Math
