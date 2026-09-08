#include "Horo/WorldStreaming/WorldSpanningObjectPlan.h"

#include "Horo/WorldStreaming/WorldStreamingErrors.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <utility>

namespace Horo::WorldStreaming {
    namespace {
        [[nodiscard]] bool IsKnown(const WorldSpanningObjectPolicy policy) noexcept {
            using enum WorldSpanningObjectPolicy;
            switch (policy) {
                case SingleCellOwner:
                case SplitPerCell:
                case NonSpatial:
                    return true;
            }
            return false;
        }

        [[nodiscard]] bool SameAddress(const WorldSpanningObjectDirective &left, const WorldSpanningObjectDirective &right) noexcept {
            return left.address == right.address;
        }

        [[nodiscard]] const WorldSpanningObjectDirective *FindDirective(const std::span<const WorldSpanningObjectDirective> directives,
                                                                        const WorldAuthoringObjectAddress address) noexcept {
            const auto found = std::ranges::lower_bound(directives, address, {}, &WorldSpanningObjectDirective::address);
            if (found == directives.end() || found->address != address)
                return nullptr;
            return std::to_address(found);
        }

        [[nodiscard]] std::optional<std::size_t> FindObject(const std::span<const WorldSpatialAssignmentEntry> objects,
                                                            const WorldAuthoringObjectAddress address) noexcept {
            const auto matches = std::ranges::equal_range(objects, address, {}, &WorldSpatialAssignmentEntry::address);
            if (matches.empty())
                return std::nullopt;
            return static_cast<std::size_t>(std::ranges::distance(objects.begin(), matches.begin()));
        }

        [[nodiscard]] bool ContainsCell(const std::span<const StreamingCellId> cells, const StreamingCellId cell) noexcept {
            return std::ranges::binary_search(cells, cell, StreamingCellCanonicalLess{});
        }

        [[nodiscard]] Result<void> ValidateDirective(const WorldSpanningObjectDirective &directive,
                                                     const WorldSpatialAssignment &assignments,
                                                     const WorldSpanningObjectPlanLimits limits) {
            if (!directive.address.IsValid() || !directive.revision.IsValid())
                return Result<void>::Failure(MakeError(WorldStreamingErrors::SpanningObjectPlanInvalid));
            if (!IsKnown(directive.policy))
                return Result<void>::Failure(MakeError(WorldStreamingErrors::SpanningObjectPlanUnsupported));
            if ((directive.policy == WorldSpanningObjectPolicy::SingleCellOwner) != directive.ownerCell.IsValid())
                return Result<void>::Failure(MakeError(WorldStreamingErrors::SpanningObjectPlanInvalid));

            const auto objectIndex = FindObject(assignments.Objects(), directive.address);
            if (!objectIndex.has_value())
                return Result<void>::Failure(MakeError(WorldStreamingErrors::SpanningObjectPlanIdentityConflict));
            if (const auto &object = assignments.Objects()[*objectIndex]; object.revision != directive.revision)
                return Result<void>::Failure(MakeError(WorldStreamingErrors::SpanningObjectPlanRevisionStale));

            const auto sourceCells = assignments.CellsForObject(*objectIndex);
            if (sourceCells.size() <= limits.maximumDirectCellsPerObject)
                return Result<void>::Failure(MakeError(WorldStreamingErrors::SpanningObjectPlanInvalid));
            if (directive.policy == WorldSpanningObjectPolicy::SingleCellOwner && !ContainsCell(sourceCells, directive.ownerCell))
                return Result<void>::Failure(MakeError(WorldStreamingErrors::SpanningObjectPlanOwnerUnavailable));
            return Result<void>::Success();
        }

        struct PlanStorage final {
            std::vector<WorldSpanningObjectEntry> objects;
            std::vector<StreamingCellId> cells;
        };

        [[nodiscard]] Result<void> AppendCells(const std::span<const StreamingCellId> source, const WorldSpanningObjectPlanLimits limits,
                                               PlanStorage &storage) {
            if (source.size() > limits.maximumPlacementCells - storage.cells.size())
                return Result<void>::Failure(MakeError(WorldStreamingErrors::SpanningObjectPlanCapacityExceeded));
            storage.cells.insert(storage.cells.end(), source.begin(), source.end());
            return Result<void>::Success();
        }

        [[nodiscard]] WorldSpanningObjectPlacement PlacementFor(const WorldSpanningObjectPolicy policy) noexcept {
            switch (policy) {
                case WorldSpanningObjectPolicy::SingleCellOwner:
                    return WorldSpanningObjectPlacement::SingleCellOwner;
                case WorldSpanningObjectPolicy::SplitPerCell:
                    return WorldSpanningObjectPlacement::SplitPerCell;
                case WorldSpanningObjectPolicy::NonSpatial:
                    return WorldSpanningObjectPlacement::NonSpatial;
            }
            return WorldSpanningObjectPlacement::Direct;
        }

        [[nodiscard]] Result<void> AppendObject(const WorldSpatialAssignmentEntry &object,
                                                const std::span<const StreamingCellId> sourceCells,
                                                const WorldSpanningObjectDirective *directive, const WorldSpanningObjectPlanLimits limits,
                                                PlanStorage &storage) {
            const auto offset = static_cast<std::uint32_t>(storage.cells.size());
            auto placement = WorldSpanningObjectPlacement::Direct;
            if (sourceCells.size() <= limits.maximumDirectCellsPerObject) {
                if (const auto appended = AppendCells(sourceCells, limits, storage); appended.HasError())
                    return appended;
            } else {
                if (directive == nullptr)
                    return Result<void>::Failure(MakeError(WorldStreamingErrors::SpanningObjectPlanInvalid));
                placement = PlacementFor(directive->policy);
                if (directive->policy == WorldSpanningObjectPolicy::SingleCellOwner) {
                    if (const auto appended = AppendCells(std::span{&directive->ownerCell, 1}, limits, storage); appended.HasError())
                        return appended;
                } else if (directive->policy == WorldSpanningObjectPolicy::SplitPerCell) {
                    if (const auto appended = AppendCells(sourceCells, limits, storage); appended.HasError())
                        return appended;
                }
            }
            const auto count = static_cast<std::uint32_t>(storage.cells.size()) - offset;
            storage.objects.emplace_back(object.address, object.revision, placement, offset, count);
            return Result<void>::Success();
        }

        [[nodiscard]] Result<std::vector<WorldSpanningObjectDirective>> PrepareDirectives(
            const WorldSpatialAssignment &assignments, const std::span<const WorldSpanningObjectDirective> directives,
            const WorldSpanningObjectPlanLimits limits) {
            if (limits.maximumObjects == 0 || limits.maximumDirectCellsPerObject == 0 || limits.maximumPlacementCells == 0)
                return Result<std::vector<WorldSpanningObjectDirective>>::Failure(
                    MakeError(WorldStreamingErrors::SpanningObjectPlanInvalid));
            if (assignments.Objects().size() > limits.maximumObjects || directives.size() > limits.maximumObjects)
                return Result<std::vector<WorldSpanningObjectDirective>>::Failure(
                    MakeError(WorldStreamingErrors::SpanningObjectPlanCapacityExceeded));

            std::vector<WorldSpanningObjectDirective> ordered{directives.begin(), directives.end()};
            std::ranges::sort(ordered, {}, &WorldSpanningObjectDirective::address);
            if (std::ranges::adjacent_find(ordered, SameAddress) != ordered.end())
                return Result<std::vector<WorldSpanningObjectDirective>>::Failure(
                    MakeError(WorldStreamingErrors::SpanningObjectPlanIdentityConflict));
            for (const auto &directive : ordered) {
                if (const auto valid = ValidateDirective(directive, assignments, limits); valid.HasError())
                    return Result<std::vector<WorldSpanningObjectDirective>>::Failure(valid.ErrorValue());
            }
            return Result<std::vector<WorldSpanningObjectDirective>>::Success(std::move(ordered));
        }
    }  // namespace

    /** @copydoc WorldSpanningObjectPlan::WorldSpanningObjectPlan */
    WorldSpanningObjectPlan::WorldSpanningObjectPlan(const WorldPartitionId partition, std::vector<WorldSpanningObjectEntry> objects,
                                                     std::vector<StreamingCellId> cells) noexcept
        : partition_(partition), objects_(std::move(objects)), cells_(std::move(cells)) {}

    /** @copydoc WorldSpanningObjectPlan::Create */
    Result<WorldSpanningObjectPlan> WorldSpanningObjectPlan::Create(const WorldSpatialAssignment &assignments,
                                                                    const std::span<const WorldSpanningObjectDirective> directives,
                                                                    const WorldSpanningObjectPlanLimits limits) {
        auto prepared = PrepareDirectives(assignments, directives, limits);
        if (prepared.HasError())
            return Result<WorldSpanningObjectPlan>::Failure(prepared.ErrorValue());
        auto ordered = std::move(prepared).Value();

        PlanStorage storage;
        storage.objects.reserve(assignments.Objects().size());
        for (std::size_t objectIndex = 0; objectIndex < assignments.Objects().size(); ++objectIndex) {
            const auto &object = assignments.Objects()[objectIndex];
            if (const auto appended =
                    AppendObject(object, assignments.CellsForObject(objectIndex), FindDirective(ordered, object.address), limits, storage);
                appended.HasError())
                return Result<WorldSpanningObjectPlan>::Failure(appended.ErrorValue());
        }
        return Result<WorldSpanningObjectPlan>::Success(
            WorldSpanningObjectPlan{assignments.Partition(), std::move(storage.objects), std::move(storage.cells)});
    }

    /** @copydoc WorldSpanningObjectPlan::CellsForObject */
    std::span<const StreamingCellId> WorldSpanningObjectPlan::CellsForObject(const std::size_t objectIndex) const noexcept {
        if (objectIndex >= objects_.size())
            return {};
        const auto &object = objects_[objectIndex];
        if (object.cellCount == 0)
            return {};
        return {cells_.data() + object.cellOffset, object.cellCount};
    }
}  // namespace Horo::WorldStreaming
