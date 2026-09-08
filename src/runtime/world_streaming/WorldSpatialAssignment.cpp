#include "Horo/WorldStreaming/WorldSpatialAssignment.h"

#include "Horo/WorldStreaming/WorldStreamingErrors.h"

#include <algorithm>
#include <array>
#include <utility>

namespace Horo::WorldStreaming {
    namespace {
        [[nodiscard]] bool BoundsAreOrderedAndContained(const WorldPartitionBounds &bounds,
                                                        const WorldPartitionBounds &partitionBounds) noexcept {
            const auto minimum = bounds.minimum.Millimeters();
            const auto maximum = bounds.maximum.Millimeters();
            const auto partitionMinimum = partitionBounds.minimum.Millimeters();
            const auto partitionMaximum = partitionBounds.maximum.Millimeters();
            for (std::size_t axis = 0; axis < minimum.size(); ++axis) {
                if (minimum[axis] > maximum[axis] || minimum[axis] < partitionMinimum[axis] || maximum[axis] > partitionMaximum[axis])
                    return false;
            }
            return true;
        }

        [[nodiscard]] bool HasLayer(const WorldPartitionDescriptor &descriptor, const StreamingLayerId layer) noexcept {
            return std::ranges::binary_search(descriptor.Layers(), layer, {}, &WorldLayerDescriptor::id);
        }

        [[nodiscard]] bool HasCell(const WorldPartitionDescriptor &descriptor, const StreamingCellId cell) noexcept {
            return std::ranges::binary_search(descriptor.Cells(), cell, StreamingCellCanonicalLess{}, &WorldPartitionCellDescriptor::id);
        }

        [[nodiscard]] bool TryMultiplyWithin(const std::uint64_t left, const std::uint64_t right, const std::uint64_t limit,
                                             std::uint64_t &result) noexcept {
            if (left != 0 && right > limit / left)
                return false;
            result = left * right;
            return result <= limit;
        }

        [[nodiscard]] Result<std::uint32_t> AssignmentCount(const StreamingCellId minimum, const StreamingCellId maximum,
                                                            const WorldSpatialAssignmentLimits limits) {
            const auto width = static_cast<std::uint64_t>(static_cast<std::int64_t>(maximum.x) - minimum.x) + 1;
            const auto height = static_cast<std::uint64_t>(static_cast<std::int64_t>(maximum.y) - minimum.y) + 1;
            const auto depth = static_cast<std::uint64_t>(static_cast<std::int64_t>(maximum.z) - minimum.z) + 1;
            std::uint64_t area{};
            std::uint64_t count{};
            if (!TryMultiplyWithin(width, height, limits.maximumCellsPerObject, area) ||
                !TryMultiplyWithin(area, depth, limits.maximumCellsPerObject, count))
                return Result<std::uint32_t>::Failure(MakeError(WorldStreamingErrors::SpatialAssignmentCapacityExceeded));
            return Result<std::uint32_t>::Success(static_cast<std::uint32_t>(count));
        }

        [[nodiscard]] Result<std::array<StreamingCellId, 2>> QuantizeBounds(const WorldSpatialAssignmentCandidate &candidate,
                                                                            const WorldPartitionDescriptor &descriptor) {
            auto minimum = QuantizeWorldToCell(candidate.bounds.minimum, descriptor.Grid(), candidate.lod, candidate.layer);
            if (minimum.HasError())
                return Result<std::array<StreamingCellId, 2>>::Failure(minimum.ErrorValue());
            auto maximum = QuantizeWorldToCell(candidate.bounds.maximum, descriptor.Grid(), candidate.lod, candidate.layer);
            if (maximum.HasError())
                return Result<std::array<StreamingCellId, 2>>::Failure(maximum.ErrorValue());
            return Result<std::array<StreamingCellId, 2>>::Success({minimum.Value(), maximum.Value()});
        }

        [[nodiscard]] Result<void> AppendRow(const StreamingCellId minimum, const StreamingCellId maximum, const std::int64_t y,
                                             const std::int64_t z, const WorldPartitionDescriptor &descriptor,
                                             std::vector<StreamingCellId> &assignments) {
            for (std::int64_t x = minimum.x; x <= maximum.x; ++x) {
                const StreamingCellId cell{static_cast<std::int32_t>(x), static_cast<std::int32_t>(y), static_cast<std::int32_t>(z),
                                           minimum.lod, minimum.layer};
                if (!HasCell(descriptor, cell))
                    return Result<void>::Failure(MakeError(WorldStreamingErrors::SpatialAssignmentCellUnavailable));
                assignments.emplace_back(cell);
            }
            return Result<void>::Success();
        }

        [[nodiscard]] Result<void> AppendCells(const StreamingCellId minimum, const StreamingCellId maximum,
                                               const WorldPartitionDescriptor &descriptor, std::vector<StreamingCellId> &assignments) {
            for (std::int64_t z = minimum.z; z <= maximum.z; ++z) {
                for (std::int64_t y = minimum.y; y <= maximum.y; ++y) {
                    if (const auto row = AppendRow(minimum, maximum, y, z, descriptor, assignments); row.HasError())
                        return row;
                }
            }
            return Result<void>::Success();
        }

        [[nodiscard]] bool SameObject(const WorldSpatialAssignmentCandidate &left, const WorldSpatialAssignmentCandidate &right) noexcept {
            return left.address == right.address;
        }

        [[nodiscard]] Result<void> ValidateCandidate(const WorldSpatialAssignmentCandidate &candidate,
                                                     const WorldPartitionDescriptor &descriptor) {
            if (!candidate.address.IsValid() || !candidate.revision.IsValid() || !candidate.layer.IsValid() ||
                !BoundsAreOrderedAndContained(candidate.bounds, descriptor.Bounds()))
                return Result<void>::Failure(MakeError(WorldStreamingErrors::SpatialAssignmentInvalid));
            if (!HasLayer(descriptor, candidate.layer))
                return Result<void>::Failure(MakeError(WorldStreamingErrors::SpatialAssignmentUnsupported));
            return Result<void>::Success();
        }

        [[nodiscard]] bool HasValidLimits(const WorldSpatialAssignmentLimits limits) noexcept {
            return limits.maximumObjects != 0 && limits.maximumCellsPerObject != 0 && limits.maximumTotalAssignments != 0;
        }

        [[nodiscard]] Result<void> ValidateRequest(const std::span<const WorldSpatialAssignmentCandidate> candidates,
                                                   const WorldSpatialAssignmentLimits limits) {
            if (candidates.empty() || !HasValidLimits(limits))
                return Result<void>::Failure(MakeError(WorldStreamingErrors::SpatialAssignmentInvalid));
            if (candidates.size() > limits.maximumObjects)
                return Result<void>::Failure(MakeError(WorldStreamingErrors::SpatialAssignmentCapacityExceeded));
            return Result<void>::Success();
        }

        struct AssignmentStorage final {
            std::vector<WorldSpatialAssignmentEntry> objects;
            std::vector<StreamingCellId> cells;
        };

        [[nodiscard]] Result<void> AppendCandidate(const WorldSpatialAssignmentCandidate &candidate,
                                                   const WorldPartitionDescriptor &descriptor, const WorldSpatialAssignmentLimits limits,
                                                   AssignmentStorage &storage) {
            if (const auto validity = ValidateCandidate(candidate, descriptor); validity.HasError())
                return validity;
            auto quantized = QuantizeBounds(candidate, descriptor);
            if (quantized.HasError())
                return Result<void>::Failure(quantized.ErrorValue());
            auto count = AssignmentCount(quantized.Value()[0], quantized.Value()[1], limits);
            if (count.HasError() || count.Value() > limits.maximumTotalAssignments - storage.cells.size())
                return Result<void>::Failure(MakeError(WorldStreamingErrors::SpatialAssignmentCapacityExceeded));

            const auto offset = static_cast<std::uint32_t>(storage.cells.size());
            if (const auto appended = AppendCells(quantized.Value()[0], quantized.Value()[1], descriptor, storage.cells);
                appended.HasError())
                return appended;
            storage.objects.emplace_back(candidate.address, candidate.revision, offset, count.Value());
            return Result<void>::Success();
        }
    }  // namespace

    /** @copydoc WorldSpatialAssignment::WorldSpatialAssignment */
    WorldSpatialAssignment::WorldSpatialAssignment(const WorldPartitionId partition, std::vector<WorldSpatialAssignmentEntry> objects,
                                                   std::vector<StreamingCellId> cells) noexcept
        : partition_(partition), objects_(std::move(objects)), cells_(std::move(cells)) {}

    /** @copydoc WorldSpatialAssignment::Create */
    Result<WorldSpatialAssignment> WorldSpatialAssignment::Create(const WorldPartitionDescriptor &descriptor,
                                                                  const std::span<const WorldSpatialAssignmentCandidate> candidates,
                                                                  const WorldSpatialAssignmentLimits limits) {
        if (const auto request = ValidateRequest(candidates, limits); request.HasError())
            return Result<WorldSpatialAssignment>::Failure(request.ErrorValue());

        std::vector<WorldSpatialAssignmentCandidate> ordered{candidates.begin(), candidates.end()};
        std::ranges::sort(ordered, {}, &WorldSpatialAssignmentCandidate::address);
        if (std::ranges::adjacent_find(ordered, SameObject) != ordered.end())
            return Result<WorldSpatialAssignment>::Failure(MakeError(WorldStreamingErrors::SpatialAssignmentIdentityConflict));

        AssignmentStorage storage;
        storage.objects.reserve(ordered.size());
        for (const auto &candidate : ordered) {
            if (const auto appended = AppendCandidate(candidate, descriptor, limits, storage); appended.HasError())
                return Result<WorldSpatialAssignment>::Failure(appended.ErrorValue());
        }
        return Result<WorldSpatialAssignment>::Success(
            WorldSpatialAssignment{descriptor.Partition(), std::move(storage.objects), std::move(storage.cells)});
    }

    /** @copydoc WorldSpatialAssignment::CellsForObject */
    std::span<const StreamingCellId> WorldSpatialAssignment::CellsForObject(const std::size_t objectIndex) const noexcept {
        if (objectIndex >= objects_.size())
            return {};
        const auto &object = objects_[objectIndex];
        return std::span{cells_}.subspan(object.cellOffset, object.cellCount);
    }
}  // namespace Horo::WorldStreaming
