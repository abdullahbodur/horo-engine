#include "Horo/WorldStreaming/WorldPartitionRegistry.h"

#include "Horo/WorldStreaming/WorldStreamingErrors.h"

#include <algorithm>
#include <array>
#include <limits>
#include <new>
#include <utility>
#include <vector>

namespace Horo::WorldStreaming {
    struct WorldPartitionRegistrySnapshot::State final {
        WorldPartitionRegistryBinding binding{};
        WorldPartitionRegistryLimits limits{};
        WorldPartitionDescriptor descriptor;
        std::vector<WorldPartitionBounds> cellBounds;

        State(WorldPartitionRegistryBinding bindingValue, const WorldPartitionRegistryLimits limitsValue,
              WorldPartitionDescriptor descriptorValue, std::vector<WorldPartitionBounds> cellBoundsValue) noexcept
            : binding(std::move(bindingValue)), limits(limitsValue), descriptor(std::move(descriptorValue)),
              cellBounds(std::move(cellBoundsValue)) {}
    };

    namespace {
        template <typename T> [[nodiscard]] Result<T> Failure(const ErrorCodeDescriptor &descriptor) {
            return Result<T>::Failure(MakeError(descriptor));
        }

        [[nodiscard]] bool Ordered(const WorldPartitionBounds &bounds) noexcept {
            const auto minimum = bounds.minimum.Millimeters();
            const auto maximum = bounds.maximum.Millimeters();
            return minimum[0] <= maximum[0] && minimum[1] <= maximum[1] && minimum[2] <= maximum[2];
        }

        [[nodiscard]] bool MultiplyChecked(const std::int64_t value, const std::int64_t factor, std::int64_t &product) noexcept {
            if (factor <= 0)
                return false;
            if (value > 0 && value > std::numeric_limits<std::int64_t>::max() / factor)
                return false;
            if (value < 0 && value < std::numeric_limits<std::int64_t>::min() / factor)
                return false;
            product = value * factor;
            return true;
        }

        [[nodiscard]] bool AddChecked(const std::int64_t left, const std::int64_t right, std::int64_t &sum) noexcept {
            if ((right > 0 && left > std::numeric_limits<std::int64_t>::max() - right) ||
                (right < 0 && left < std::numeric_limits<std::int64_t>::min() - right))
                return false;
            sum = left + right;
            return true;
        }

        [[nodiscard]] bool CellAxisBounds(const std::int64_t origin, const std::int32_t coordinate, const std::int64_t size,
                                          std::int64_t &minimum, std::int64_t &maximum) noexcept {
            std::int64_t offset{};
            return MultiplyChecked(coordinate, size, offset) && AddChecked(origin, offset, minimum) &&
                   AddChecked(minimum, size - 1, maximum);
        }

        [[nodiscard]] bool CellBounds(const WorldCellQuantizationPolicy &grid, const StreamingCellId &cell,
                                      WorldPartitionBounds &bounds) noexcept {
            const std::int64_t size = grid.BaseCellSizeMillimeters() << cell.lod;
            const auto origin = grid.Origin().Millimeters();
            std::array<std::int64_t, 3> minimum{};
            std::array<std::int64_t, 3> maximum{};
            if (!CellAxisBounds(origin[0], cell.x, size, minimum[0], maximum[0]) ||
                !CellAxisBounds(origin[1], cell.y, size, minimum[1], maximum[1]) ||
                !CellAxisBounds(origin[2], cell.z, size, minimum[2], maximum[2]))
                return false;
            bounds = {Math::WorldCoordinate64::FromMillimeters(minimum[0], minimum[1], minimum[2]),
                      Math::WorldCoordinate64::FromMillimeters(maximum[0], maximum[1], maximum[2])};
            return true;
        }

        [[nodiscard]] bool Intersects(const WorldPartitionBounds &left, const WorldPartitionBounds &right) noexcept {
            const auto leftMinimum = left.minimum.Millimeters();
            const auto leftMaximum = left.maximum.Millimeters();
            const auto rightMinimum = right.minimum.Millimeters();
            const auto rightMaximum = right.maximum.Millimeters();
            return leftMinimum[0] <= rightMaximum[0] && leftMaximum[0] >= rightMinimum[0] && leftMinimum[1] <= rightMaximum[1] &&
                   leftMaximum[1] >= rightMinimum[1] && leftMinimum[2] <= rightMaximum[2] && leftMaximum[2] >= rightMinimum[2];
        }

        [[nodiscard]] bool Matches(const WorldPartitionCellDescriptor &cell, const WorldPartitionBounds &cellBounds,
                                   const WorldPartitionSpatialQuery &query) noexcept {
            if ((query.layer.has_value() && cell.id.layer != *query.layer) || (query.lod.has_value() && cell.id.lod != *query.lod))
                return false;
            return Intersects(cellBounds, query.bounds);
        }

        [[nodiscard]] auto FindCell(const std::span<const WorldPartitionCellDescriptor> cells, const StreamingCellId cell) noexcept {
            return std::ranges::lower_bound(cells, cell, StreamingCellCanonicalLess{}, &WorldPartitionCellDescriptor::id);
        }

        [[nodiscard]] bool IsDeclaredLayer(const WorldPartitionDescriptor &descriptor, const StreamingLayerId layer) noexcept {
            return std::ranges::binary_search(descriptor.Layers(), layer, {}, &WorldLayerDescriptor::id);
        }

        [[nodiscard]] Result<std::vector<WorldPartitionBounds>> BuildCellBounds(const WorldPartitionDescriptor &descriptor,
                                                                                const WorldPartitionRegistryLimits limits) {
            if (descriptor.Cells().size() > limits.cells)
                return Failure<std::vector<WorldPartitionBounds>>(WorldStreamingErrors::PartitionRegistryCapacityExceeded);
            std::vector<WorldPartitionBounds> bounds;
            try {
                bounds.reserve(descriptor.Cells().size());
            } catch (const std::bad_alloc &) {
                return Failure<std::vector<WorldPartitionBounds>>(WorldStreamingErrors::PartitionRegistryStorageUnavailable);
            }
            for (const auto &cell : descriptor.Cells()) {
                WorldPartitionBounds cellBounds{};
                if (!CellBounds(descriptor.Grid(), cell.id, cellBounds))
                    return Failure<std::vector<WorldPartitionBounds>>(WorldStreamingErrors::PartitionRegistryUnsupported);
                bounds.emplace_back(cellBounds);
            }
            return Result<std::vector<WorldPartitionBounds>>::Success(std::move(bounds));
        }
    }  // namespace

    /** @copydoc WorldPartitionRegistryBinding::IsValid */
    bool WorldPartitionRegistryBinding::IsValid() const noexcept {
        return registry.IsValid() && revision.IsValid() && owner.IsValid();
    }

    /** @copydoc WorldPartitionCellHandle::IsValid */
    bool WorldPartitionCellHandle::IsValid() const noexcept {
        return binding.IsValid() && cell.IsValid();
    }

    /** @copydoc WorldPartitionRegistryLimits::IsValid */
    bool WorldPartitionRegistryLimits::IsValid() const noexcept {
        return cells > 0 && cells <= MaximumCells && queryResults > 0 && queryResults <= MaximumQueryResults && queryResults <= cells;
    }

    /** @copydoc WorldPartitionRegistrySnapshot::IsValid */
    bool WorldPartitionRegistrySnapshot::IsValid() const noexcept {
        return state_ != nullptr && state_->binding.IsValid();
    }

    /** @copydoc WorldPartitionRegistrySnapshot::Binding */
    const WorldPartitionRegistryBinding &WorldPartitionRegistrySnapshot::Binding() const noexcept {
        static const WorldPartitionRegistryBinding InvalidBinding{};
        return state_ == nullptr ? InvalidBinding : state_->binding;
    }

    /** @copydoc WorldPartitionRegistrySnapshot::Cells */
    std::span<const WorldPartitionCellDescriptor> WorldPartitionRegistrySnapshot::Cells() const noexcept {
        return state_ == nullptr ? std::span<const WorldPartitionCellDescriptor>{} : state_->descriptor.Cells();
    }

    /** @copydoc WorldPartitionRegistrySnapshot::Find */
    Result<WorldPartitionCellHandle> WorldPartitionRegistrySnapshot::Find(const StreamingCellId cell) const {
        if (!IsValid() || !cell.IsValid())
            return Failure<WorldPartitionCellHandle>(WorldStreamingErrors::PartitionRegistryInvalid);
        const auto cells = Cells();
        const auto found = FindCell(cells, cell);
        if (found == cells.end() || found->id != cell)
            return Failure<WorldPartitionCellHandle>(WorldStreamingErrors::PartitionRegistryUnavailable);
        return Result<WorldPartitionCellHandle>::Success({state_->binding, static_cast<std::uint32_t>(found - cells.begin()), cell});
    }

    /** @copydoc WorldPartitionRegistrySnapshot::Resolve */
    Result<const WorldPartitionCellDescriptor *> WorldPartitionRegistrySnapshot::Resolve(const WorldPartitionCellHandle &handle) const {
        if (!IsValid() || !handle.IsValid() || handle.slot >= Cells().size())
            return Failure<const WorldPartitionCellDescriptor *>(WorldStreamingErrors::PartitionRegistryInvalid);
        if (handle.binding != state_->binding)
            return Failure<const WorldPartitionCellDescriptor *>(WorldStreamingErrors::PartitionRegistryStale);
        const auto &record = Cells()[handle.slot];
        if (record.id != handle.cell)
            return Failure<const WorldPartitionCellDescriptor *>(WorldStreamingErrors::PartitionRegistryStale);
        return Result<const WorldPartitionCellDescriptor *>::Success(&record);
    }

    /** @copydoc WorldPartitionRegistrySnapshot::Query */
    Result<std::size_t> WorldPartitionRegistrySnapshot::Query(const WorldPartitionSpatialQuery &query,
                                                              const std::span<WorldPartitionCellHandle> output) const {
        if (!IsValid() || !Ordered(query.bounds) || output.empty())
            return Failure<std::size_t>(WorldStreamingErrors::PartitionRegistryInvalid);
        if (query.layer.has_value() && !query.layer->IsValid())
            return Failure<std::size_t>(WorldStreamingErrors::PartitionRegistryInvalid);
        if (query.layer.has_value() && !IsDeclaredLayer(state_->descriptor, *query.layer))
            return Failure<std::size_t>(WorldStreamingErrors::PartitionRegistryUnsupported);
        if (query.lod.has_value() && *query.lod >= state_->descriptor.Grid().LodLevels())
            return Failure<std::size_t>(WorldStreamingErrors::PartitionRegistryUnsupported);
        std::size_t matches{};
        const auto cells = Cells();
        for (std::size_t index = 0; index < cells.size(); ++index) {
            if (Matches(cells[index], state_->cellBounds[index], query))
                ++matches;
        }
        if (matches > state_->limits.queryResults || matches > output.size())
            return Failure<std::size_t>(WorldStreamingErrors::PartitionRegistryCapacityExceeded);

        std::size_t written{};
        for (std::size_t index = 0; index < cells.size(); ++index) {
            const auto &cell = cells[index];
            if (!Matches(cell, state_->cellBounds[index], query))
                continue;
            output[written] = {state_->binding, static_cast<std::uint32_t>(index), cell.id};
            ++written;
        }
        return Result<std::size_t>::Success(written);
    }

    WorldPartitionRegistry::WorldPartitionRegistry(ConstructionKey, const WorldPartitionRegistryId registry,
                                                   const StreamingRuntimeOwnerToken &owner,
                                                   const WorldPartitionRegistryLimits limits) noexcept
        : registry_(registry), owner_(owner), limits_(limits) {}

    /** @copydoc WorldPartitionRegistry::Create */
    Result<std::unique_ptr<WorldPartitionRegistry>> WorldPartitionRegistry::Create(const WorldPartitionRegistryId registry,
                                                                                   const StreamingRuntimeOwnerToken &owner,
                                                                                   const WorldPartitionRegistryLimits limits) {
        if (!registry.IsValid() || !owner.IsValid() || !limits.IsValid())
            return Failure<std::unique_ptr<WorldPartitionRegistry>>(WorldStreamingErrors::PartitionRegistryInvalid);
        try {
            return Result<std::unique_ptr<WorldPartitionRegistry>>::Success(
                std::make_unique<WorldPartitionRegistry>(ConstructionKey{}, registry, owner, limits));
        } catch (const std::bad_alloc &) {
            return Failure<std::unique_ptr<WorldPartitionRegistry>>(WorldStreamingErrors::PartitionRegistryStorageUnavailable);
        }
    }

    /** @copydoc WorldPartitionRegistry::Publish */
    Result<void> WorldPartitionRegistry::Publish(WorldPartitionDescriptor &&descriptor, const WorldPartitionRegistryRevision revision) {
        if (Lifecycle() != WorldPartitionRegistryState::Active)
            return Failure<void>(WorldStreamingErrors::PartitionRegistryLifecycleUnavailable);
        if (!revision.IsValid() || descriptor.Partition() != owner_.partition)
            return Failure<void>(WorldStreamingErrors::PartitionRegistryInvalid);
        auto cellBounds = BuildCellBounds(descriptor, limits_);
        if (cellBounds.HasError())
            return Result<void>::Failure(cellBounds.ErrorValue());

        const auto current = std::atomic_load(&state_);
        if (current != nullptr && current->binding.revision.Value() == std::numeric_limits<std::uint64_t>::max())
            return Failure<void>(WorldStreamingErrors::GenerationExhausted);
        if ((current == nullptr && revision.Value() != 1) ||
            (current != nullptr && (current->binding.owner != owner_ || revision.Value() != current->binding.revision.Value() + 1)))
            return Failure<void>(WorldStreamingErrors::PartitionRegistryStale);

        try {
            auto next =
                std::make_shared<WorldPartitionRegistrySnapshot::State>(WorldPartitionRegistryBinding{registry_, revision, owner_}, limits_,
                                                                        std::move(descriptor), std::move(cellBounds).Value());
            std::atomic_store(&state_, std::shared_ptr<const WorldPartitionRegistrySnapshot::State>{std::move(next)});
            return Result<void>::Success();
        } catch (const std::bad_alloc &) {
            return Failure<void>(WorldStreamingErrors::PartitionRegistryStorageUnavailable);
        }
    }

    /** @copydoc WorldPartitionRegistry::Snapshot */
    Result<WorldPartitionRegistrySnapshot> WorldPartitionRegistry::Snapshot() const {
        if (Lifecycle() != WorldPartitionRegistryState::Active)
            return Failure<WorldPartitionRegistrySnapshot>(WorldStreamingErrors::PartitionRegistryLifecycleUnavailable);
        auto state = std::atomic_load(&state_);
        if (state == nullptr)
            return Failure<WorldPartitionRegistrySnapshot>(WorldStreamingErrors::PartitionRegistryUnavailable);
        return Result<WorldPartitionRegistrySnapshot>::Success(WorldPartitionRegistrySnapshot{std::move(state)});
    }

    /** @copydoc WorldPartitionRegistry::BeginCancellation */
    void WorldPartitionRegistry::BeginCancellation() noexcept {
        auto expected = WorldPartitionRegistryState::Active;
        static_cast<void>(lifecycle_.compare_exchange_strong(expected, WorldPartitionRegistryState::Cancelling));
    }

    /** @copydoc WorldPartitionRegistry::Shutdown */
    void WorldPartitionRegistry::Shutdown() noexcept {
        lifecycle_.store(WorldPartitionRegistryState::Closed);
        std::atomic_store(&state_, std::shared_ptr<const WorldPartitionRegistrySnapshot::State>{});
    }

    /** @copydoc WorldPartitionRegistry::~WorldPartitionRegistry */
    WorldPartitionRegistry::~WorldPartitionRegistry() noexcept {
        Shutdown();
    }

    /** @copydoc WorldPartitionRegistry::Lifecycle */
    WorldPartitionRegistryState WorldPartitionRegistry::Lifecycle() const noexcept {
        return lifecycle_.load();
    }

    /** @copydoc NextWorldPartitionRegistryRevision */
    Result<WorldPartitionRegistryRevision> NextWorldPartitionRegistryRevision(const WorldPartitionRegistryRevision current) {
        if (!current.IsValid())
            return Failure<WorldPartitionRegistryRevision>(WorldStreamingErrors::IdentityInvalid);
        if (current.Value() == std::numeric_limits<std::uint64_t>::max())
            return Failure<WorldPartitionRegistryRevision>(WorldStreamingErrors::GenerationExhausted);
        return WorldPartitionRegistryRevision::Create(current.Value() + 1);
    }
}  // namespace Horo::WorldStreaming
