#include "Horo/WorldStreaming/WorldPartitionDescriptor.h"

#include "Horo/WorldStreaming/WorldStreamingErrors.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <tuple>
#include <utility>

namespace Horo::WorldStreaming {
    namespace {
        constexpr std::uint32_t KnownLayerFlags =
            static_cast<std::uint32_t>(WorldLayerFlags::Persistent) | static_cast<std::uint32_t>(WorldLayerFlags::Optional) |
            static_cast<std::uint32_t>(WorldLayerFlags::ServerOnly) | static_cast<std::uint32_t>(WorldLayerFlags::ClientOnly);

        [[nodiscard]] bool IsKnown(const WorldLayerOwnership value) noexcept {
            using enum WorldLayerOwnership;
            switch (value) {
                case WorldStreaming:
                case GameplayScript:
                case NetworkReplication:
                    return true;
            }
            return false;
        }

        [[nodiscard]] bool TryMultiply(const std::int64_t value, const std::int64_t positiveFactor, std::int64_t &result) noexcept {
            if (positiveFactor <= 0) {
                return false;
            }
            if (value > 0 && value > std::numeric_limits<std::int64_t>::max() / positiveFactor) {
                return false;
            }
            if (value < 0 && value < std::numeric_limits<std::int64_t>::min() / positiveFactor) {
                return false;
            }
            result = value * positiveFactor;
            return true;
        }

        [[nodiscard]] bool TryAdd(const std::int64_t left, const std::int64_t right, std::int64_t &result) noexcept {
            if ((right > 0 && left > std::numeric_limits<std::int64_t>::max() - right) ||
                (right < 0 && left < std::numeric_limits<std::int64_t>::min() - right)) {
                return false;
            }
            result = left + right;
            return true;
        }

        [[nodiscard]] bool AxisContains(const std::int64_t origin, const std::int32_t minimumCell, const std::int32_t maximumCell,
                                        const std::int64_t cellSize, const std::int64_t contentMinimum,
                                        const std::int64_t contentMaximum) noexcept {
            std::int64_t minimumOffset{};
            std::int64_t maximumOffset{};
            std::int64_t gridMinimum{};
            std::int64_t gridMaximumExclusive{};
            const auto maximumBoundary = static_cast<std::int64_t>(maximumCell) + 1;
            return contentMinimum <= contentMaximum && TryMultiply(minimumCell, cellSize, minimumOffset) &&
                   TryMultiply(maximumBoundary, cellSize, maximumOffset) && TryAdd(origin, minimumOffset, gridMinimum) &&
                   TryAdd(origin, maximumOffset, gridMaximumExclusive) && contentMinimum >= gridMinimum &&
                   contentMaximum < gridMaximumExclusive;
        }

        [[nodiscard]] bool CellLess(const WorldPartitionCellDescriptor &left, const WorldPartitionCellDescriptor &right) noexcept {
            return std::tuple{left.id.layer.Value(), left.id.lod, left.id.z, left.id.y, left.id.x} <
                   std::tuple{right.id.layer.Value(), right.id.lod, right.id.z, right.id.y, right.id.x};
        }

        [[nodiscard]] bool SameCell(const WorldPartitionCellDescriptor &left, const WorldPartitionCellDescriptor &right) noexcept {
            return left.id == right.id;
        }

        template <typename T> [[nodiscard]] Result<T> Invalid(const ErrorCodeDescriptor &descriptor) {
            return Result<T>::Failure(MakeError(descriptor));
        }

        [[nodiscard]] Result<void> ValidateEnvelope(const WorldPartitionBounds &bounds, const WorldCellQuantizationPolicy &grid) {
            const auto minimum = bounds.minimum.Millimeters();
            const auto maximum = bounds.maximum.Millimeters();
            const auto origin = grid.Origin().Millimeters();
            if (const auto gridBounds = grid.Bounds(); !AxisContains(origin[0], gridBounds.minimumX, gridBounds.maximumX,
                                                                     grid.BaseCellSizeMillimeters(), minimum[0], maximum[0]) ||
                                                       !AxisContains(origin[1], gridBounds.minimumY, gridBounds.maximumY,
                                                                     grid.BaseCellSizeMillimeters(), minimum[1], maximum[1]) ||
                                                       !AxisContains(origin[2], gridBounds.minimumZ, gridBounds.maximumZ,
                                                                     grid.BaseCellSizeMillimeters(), minimum[2], maximum[2])) {
                return Invalid<void>(WorldStreamingErrors::PartitionBoundsInvalid);
            }
            return Result<void>::Success();
        }

        [[nodiscard]] Result<std::vector<WorldLayerDescriptor>> ValidateAndOwnLayers(const std::span<const WorldLayerDescriptor> layers,
                                                                                     const WorldPartitionDescriptorLimits limits) {
            std::uint32_t totalNameBytes{};
            for (const auto &layer : layers) {
                if (const auto flags = static_cast<std::uint32_t>(layer.flags);
                    !layer.id.IsValid() || !IsKnown(layer.ownership) || (flags & ~KnownLayerFlags) != 0 || layer.name.empty() ||
                    layer.name.find('\0') != std::string::npos || !std::isfinite(layer.priorityMultiplier) ||
                    layer.priorityMultiplier <= 0.0F) {
                    return Invalid<std::vector<WorldLayerDescriptor>>(WorldStreamingErrors::PartitionDescriptorInvalid);
                }
                if (layer.name.size() > limits.maximumLayerNameBytes - totalNameBytes) {
                    return Invalid<std::vector<WorldLayerDescriptor>>(WorldStreamingErrors::PartitionCapacityExceeded);
                }
                totalNameBytes += static_cast<std::uint32_t>(layer.name.size());
            }

            std::vector<WorldLayerDescriptor> owned{layers.begin(), layers.end()};
            std::ranges::sort(owned, {}, &WorldLayerDescriptor::id);
            if (std::ranges::adjacent_find(owned, {}, &WorldLayerDescriptor::id) != owned.end()) {
                return Invalid<std::vector<WorldLayerDescriptor>>(WorldStreamingErrors::PartitionIdentityConflict);
            }
            return Result<std::vector<WorldLayerDescriptor>>::Success(std::move(owned));
        }

        [[nodiscard]] Result<std::vector<WorldPartitionCellDescriptor>> ValidateAndOwnCells(
            const std::span<const WorldPartitionCellDescriptor> cells, const std::span<const WorldLayerDescriptor> layers,
            const WorldCellQuantizationPolicy &grid) {
            const auto bounds = grid.Bounds();
            for (const auto &cell : cells) {
                const auto id = cell.id;
                const bool withinCoordinates = id.x >= bounds.minimumX && id.x <= bounds.maximumX && id.y >= bounds.minimumY &&
                                               id.y <= bounds.maximumY && id.z >= bounds.minimumZ && id.z <= bounds.maximumZ;
                const bool layerExists = std::ranges::binary_search(layers, id.layer, {}, &WorldLayerDescriptor::id);
                if (!id.IsValid() || id.lod >= grid.LodLevels() || !withinCoordinates || !layerExists ||
                    !cell.package.chunkAsset.IsValid()) {
                    return Invalid<std::vector<WorldPartitionCellDescriptor>>(WorldStreamingErrors::PartitionDescriptorInvalid);
                }
            }

            std::vector<WorldPartitionCellDescriptor> owned{cells.begin(), cells.end()};
            std::ranges::sort(owned, CellLess);
            if (std::ranges::adjacent_find(owned, SameCell) != owned.end()) {
                return Invalid<std::vector<WorldPartitionCellDescriptor>>(WorldStreamingErrors::PartitionIdentityConflict);
            }
            return Result<std::vector<WorldPartitionCellDescriptor>>::Success(std::move(owned));
        }
    }  // namespace

    /** @copydoc WorldPartitionDescriptor::WorldPartitionDescriptor */
    WorldPartitionDescriptor::WorldPartitionDescriptor(WorldPartitionSchemaVersion version, WorldPartitionId partition,
                                                       const WorldPartitionBounds &bounds, const WorldCellQuantizationPolicy &grid,
                                                       std::vector<WorldLayerDescriptor> layers,
                                                       std::vector<WorldPartitionCellDescriptor> cells) noexcept
        : version_(version), partition_(partition), bounds_(bounds), grid_(grid), layers_(std::move(layers)), cells_(std::move(cells)) {}

    /** @copydoc WorldPartitionDescriptor::Create */
    Result<WorldPartitionDescriptor> WorldPartitionDescriptor::Create(const WorldPartitionSchemaVersion version,
                                                                      const WorldPartitionId partition, const WorldPartitionBounds &bounds,
                                                                      const WorldCellQuantizationPolicy &grid,
                                                                      const std::span<const WorldLayerDescriptor> layers,
                                                                      const std::span<const WorldPartitionCellDescriptor> cells,
                                                                      const WorldPartitionDescriptorLimits limits) {
        if (version.major != WorldPartitionSchemaVersion::CurrentMajor || version.minor != WorldPartitionSchemaVersion::CurrentMinor) {
            return Invalid<WorldPartitionDescriptor>(WorldStreamingErrors::PartitionVersionUnsupported);
        }
        if (!partition.IsValid() || limits.maximumLayers == 0 || limits.maximumCells == 0 || limits.maximumLayerNameBytes == 0 ||
            layers.empty() || cells.empty()) {
            return Invalid<WorldPartitionDescriptor>(WorldStreamingErrors::PartitionDescriptorInvalid);
        }
        if (layers.size() > limits.maximumLayers || cells.size() > limits.maximumCells || layers.size() > StreamingLayerId::InvalidValue) {
            return Invalid<WorldPartitionDescriptor>(WorldStreamingErrors::PartitionCapacityExceeded);
        }
        if (const auto envelope = ValidateEnvelope(bounds, grid); envelope.HasError()) {
            return Result<WorldPartitionDescriptor>::Failure(envelope.ErrorValue());
        }
        auto ownedLayersResult = ValidateAndOwnLayers(layers, limits);
        if (ownedLayersResult.HasError()) {
            return Result<WorldPartitionDescriptor>::Failure(ownedLayersResult.ErrorValue());
        }
        auto ownedLayers = std::move(ownedLayersResult).Value();
        auto ownedCellsResult = ValidateAndOwnCells(cells, ownedLayers, grid);
        if (ownedCellsResult.HasError()) {
            return Result<WorldPartitionDescriptor>::Failure(ownedCellsResult.ErrorValue());
        }
        return Result<WorldPartitionDescriptor>::Success(
            WorldPartitionDescriptor{version, partition, bounds, grid, std::move(ownedLayers), std::move(ownedCellsResult).Value()});
    }
}  // namespace Horo::WorldStreaming
