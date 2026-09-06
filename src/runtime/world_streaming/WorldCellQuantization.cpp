#include "Horo/WorldStreaming/WorldCellQuantization.h"

#include "Horo/WorldStreaming/WorldStreamingErrors.h"

#include <array>
#include <limits>

namespace Horo::WorldStreaming {
    namespace {
        [[nodiscard]] bool SubtractChecked(const std::int64_t value, const std::int64_t origin, std::int64_t &difference) noexcept {
            if ((origin > 0 && value < std::numeric_limits<std::int64_t>::min() + origin) ||
                (origin < 0 && value > std::numeric_limits<std::int64_t>::max() + origin))
                return false;
            difference = value - origin;
            return true;
        }

        [[nodiscard]] std::int64_t FloorDivide(const std::int64_t numerator, const std::int64_t denominator) noexcept {
            std::int64_t quotient = numerator / denominator;
            if (numerator % denominator < 0)
                --quotient;
            return quotient;
        }

        [[nodiscard]] bool InBounds(const std::int64_t value, const std::int32_t minimum, const std::int32_t maximum) noexcept {
            return value >= minimum && value <= maximum;
        }
    }  // namespace

    /** @copydoc WorldCellQuantizationPolicy::Create */
    Result<WorldCellQuantizationPolicy> WorldCellQuantizationPolicy::Create(const Math::WorldCoordinate64 origin,
                                                                            const std::int64_t baseCellSizeMillimeters,
                                                                            const WorldCellBounds bounds, const std::uint8_t lodLevels) {
        constexpr std::uint8_t MaximumLodLevels = 32;
        const bool boundsOrdered =
            bounds.minimumX <= bounds.maximumX && bounds.minimumY <= bounds.maximumY && bounds.minimumZ <= bounds.maximumZ;
        if (baseCellSizeMillimeters <= 0 || !boundsOrdered || lodLevels == 0 || lodLevels > MaximumLodLevels ||
            baseCellSizeMillimeters > (std::numeric_limits<std::int64_t>::max() >> (lodLevels - 1U)))
            return Result<WorldCellQuantizationPolicy>::Failure(MakeError(WorldStreamingErrors::QuantizationPolicyInvalid));
        return Result<WorldCellQuantizationPolicy>::Success(
            WorldCellQuantizationPolicy{origin, baseCellSizeMillimeters, bounds, lodLevels});
    }

    /** @copydoc QuantizeWorldToCell */
    Result<StreamingCellId> QuantizeWorldToCell(const Math::WorldCoordinate64 &coordinate, const WorldCellQuantizationPolicy &policy,
                                                const std::uint8_t lod, const StreamingLayerId layer) {
        if (!layer.IsValid())
            return Result<StreamingCellId>::Failure(MakeError(WorldStreamingErrors::IdentityInvalid));
        if (lod >= policy.LodLevels())
            return Result<StreamingCellId>::Failure(MakeError(WorldStreamingErrors::LodUnsupported));

        const auto position = coordinate.Millimeters();
        const auto origin = policy.Origin().Millimeters();
        std::array<std::int64_t, 3> relative{};
        for (std::size_t axis = 0; axis < relative.size(); ++axis) {
            if (!SubtractChecked(position[axis], origin[axis], relative[axis]))
                return Result<StreamingCellId>::Failure(MakeError(WorldStreamingErrors::CoordinateOutOfRange));
        }

        const std::int64_t cellSize = policy.BaseCellSizeMillimeters() << lod;
        const std::array cells{FloorDivide(relative[0], cellSize), FloorDivide(relative[1], cellSize), FloorDivide(relative[2], cellSize)};
        const WorldCellBounds bounds = policy.Bounds();
        if (!InBounds(cells[0], bounds.minimumX, bounds.maximumX) || !InBounds(cells[1], bounds.minimumY, bounds.maximumY) ||
            !InBounds(cells[2], bounds.minimumZ, bounds.maximumZ))
            return Result<StreamingCellId>::Failure(MakeError(WorldStreamingErrors::CellOutOfBounds));

        return Result<StreamingCellId>::Success({
            .x = static_cast<std::int32_t>(cells[0]),
            .y = static_cast<std::int32_t>(cells[1]),
            .z = static_cast<std::int32_t>(cells[2]),
            .lod = lod,
            .layer = layer,
        });
    }
}  // namespace Horo::WorldStreaming
