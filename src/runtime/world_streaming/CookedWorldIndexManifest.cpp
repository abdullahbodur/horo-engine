#include "Horo/WorldStreaming/CookedWorldIndexManifest.h"

#include "Horo/WorldStreaming/WorldStreamingErrors.h"

#include <algorithm>
#include <utility>

namespace Horo::WorldStreaming {
    namespace {
        template <typename T> [[nodiscard]] Result<T> Invalid(const ErrorCodeDescriptor &descriptor) {
            return Result<T>::Failure(MakeError(descriptor));
        }

        [[nodiscard]] bool TryAccumulate(const std::uint64_t value, const std::uint64_t limit, std::uint64_t &total) noexcept {
            if (total > limit || value > limit - total)
                return false;
            total += value;
            return true;
        }

        [[nodiscard]] bool ContainsCell(const std::span<const WorldPartitionCellDescriptor> cells, const StreamingCellId &cell) noexcept {
            return std::ranges::binary_search(cells, cell, StreamingCellCanonicalLess{}, &WorldPartitionCellDescriptor::id);
        }

        [[nodiscard]] Result<void> ValidateLimits(const CookedWorldIndexManifestLimits limits) {
            if (limits.maximumCellEntries == 0 || limits.maximumDependenciesPerCell == 0 || limits.maximumTotalDependencies == 0 ||
                limits.maximumCompressedBytes == 0 || limits.maximumUncompressedBytes == 0)
                return Invalid<void>(WorldStreamingErrors::CookedManifestInvalid);
            return Result<void>::Success();
        }

        struct ManifestTotals final {
            std::uint64_t compressedBytes{};
            std::uint64_t uncompressedBytes{};
            std::uint32_t dependencies{};
        };

        [[nodiscard]] Result<ManifestTotals> ValidateShapeAndTotals(const std::span<const WorldPartitionCellDescriptor> descriptorCells,
                                                                    const std::span<const CookedWorldCellManifestCandidate> cells,
                                                                    const CookedWorldIndexManifestLimits limits) {
            if (cells.empty())
                return Invalid<ManifestTotals>(WorldStreamingErrors::CookedManifestInvalid);
            if (cells.size() != descriptorCells.size())
                return Invalid<ManifestTotals>(WorldStreamingErrors::CookedManifestIdentityConflict);
            if (cells.size() > limits.maximumCellEntries)
                return Invalid<ManifestTotals>(WorldStreamingErrors::CookedManifestCapacityExceeded);

            ManifestTotals totals;
            std::uint64_t dependencyCount{};
            for (const auto &cell : cells) {
                if (!cell.cell.IsValid() || cell.compressedSize == 0 || cell.uncompressedSize == 0)
                    return Invalid<ManifestTotals>(WorldStreamingErrors::CookedManifestInvalid);
                if (cell.hardDependencies.size() > limits.maximumDependenciesPerCell ||
                    !TryAccumulate(cell.hardDependencies.size(), limits.maximumTotalDependencies, dependencyCount) ||
                    !TryAccumulate(cell.compressedSize, limits.maximumCompressedBytes, totals.compressedBytes) ||
                    !TryAccumulate(cell.uncompressedSize, limits.maximumUncompressedBytes, totals.uncompressedBytes))
                    return Invalid<ManifestTotals>(WorldStreamingErrors::CookedManifestCapacityExceeded);
            }
            totals.dependencies = static_cast<std::uint32_t>(dependencyCount);
            return Result<ManifestTotals>::Success(totals);
        }

        [[nodiscard]] Result<void> ValidateDependencies(const std::span<const WorldPartitionCellDescriptor> descriptorCells,
                                                        const std::span<const CookedWorldCellManifestCandidate> cells) {
            for (const auto &cell : cells) {
                for (const auto &dependency : cell.hardDependencies) {
                    if (!dependency.IsValid() || dependency == cell.cell || !ContainsCell(descriptorCells, dependency))
                        return Invalid<void>(WorldStreamingErrors::CookedManifestDependencyInvalid);
                }
            }
            return Result<void>::Success();
        }

        struct OwnedCells final {
            std::vector<CookedWorldCellManifestEntry> cells;
            std::vector<StreamingCellId> hardDependencies;
            ManifestTotals totals;
        };

        [[nodiscard]] Result<OwnedCells> OwnAndCanonicalizeCells(const std::span<const WorldPartitionCellDescriptor> descriptorCells,
                                                                 const std::span<const CookedWorldCellManifestCandidate> cells,
                                                                 const ManifestTotals totals) {
            OwnedCells owned;
            owned.cells.reserve(cells.size());
            owned.hardDependencies.reserve(totals.dependencies);
            owned.totals = totals;
            for (const auto &cell : cells) {
                const auto offset = static_cast<std::uint32_t>(owned.hardDependencies.size());
                owned.hardDependencies.insert(owned.hardDependencies.end(), cell.hardDependencies.begin(), cell.hardDependencies.end());
                auto dependencies = std::span{owned.hardDependencies}.subspan(offset, cell.hardDependencies.size());
                std::ranges::sort(dependencies, StreamingCellCanonicalLess{});
                if (std::ranges::adjacent_find(dependencies) != dependencies.end())
                    return Invalid<OwnedCells>(WorldStreamingErrors::CookedManifestDependencyInvalid);
                owned.cells.push_back({cell.cell, cell.uncompressedSize, cell.compressedSize, cell.payloadCrc32, cell.artifactHash, offset,
                                       static_cast<std::uint32_t>(cell.hardDependencies.size())});
            }

            std::ranges::sort(owned.cells, StreamingCellCanonicalLess{}, &CookedWorldCellManifestEntry::cell);
            if (std::ranges::adjacent_find(owned.cells, {}, &CookedWorldCellManifestEntry::cell) != owned.cells.end())
                return Invalid<OwnedCells>(WorldStreamingErrors::CookedManifestIdentityConflict);
            for (std::size_t index = 0; index < owned.cells.size(); ++index) {
                if (owned.cells[index].cell != descriptorCells[index].id)
                    return Invalid<OwnedCells>(WorldStreamingErrors::CookedManifestIdentityConflict);
            }
            return Result<OwnedCells>::Success(std::move(owned));
        }

        [[nodiscard]] Result<OwnedCells> ValidateAndOwnCells(const WorldPartitionDescriptor &descriptor,
                                                             const std::span<const CookedWorldCellManifestCandidate> cells,
                                                             const CookedWorldIndexManifestLimits limits) {
            const auto descriptorCells = descriptor.Cells();
            auto totals = ValidateShapeAndTotals(descriptorCells, cells, limits);
            if (totals.HasError())
                return Result<OwnedCells>::Failure(totals.ErrorValue());
            if (const auto dependencies = ValidateDependencies(descriptorCells, cells); dependencies.HasError())
                return Result<OwnedCells>::Failure(dependencies.ErrorValue());
            return OwnAndCanonicalizeCells(descriptorCells, cells, totals.Value());
        }
    }  // namespace

    /** @copydoc CookedWorldIndexManifest::CookedWorldIndexManifest */
    CookedWorldIndexManifest::CookedWorldIndexManifest(WorldPartitionDescriptor &&descriptor,
                                                       std::vector<CookedWorldCellManifestEntry> cells,
                                                       std::vector<StreamingCellId> hardDependencies,
                                                       const std::uint64_t totalCompressedBytes,
                                                       const std::uint64_t totalUncompressedBytes) noexcept
        : descriptor_(std::move(descriptor)), cells_(std::move(cells)), hardDependencies_(std::move(hardDependencies)),
          totalCompressedBytes_(totalCompressedBytes), totalUncompressedBytes_(totalUncompressedBytes) {}

    /** @copydoc CookedWorldIndexManifest::Create */
    Result<CookedWorldIndexManifest> CookedWorldIndexManifest::Create(WorldPartitionDescriptor &&descriptor,
                                                                      const std::span<const CookedWorldCellManifestCandidate> cells,
                                                                      const CookedWorldIndexManifestLimits limits) {
        if (const auto validLimits = ValidateLimits(limits); validLimits.HasError())
            return Invalid<CookedWorldIndexManifest>(WorldStreamingErrors::CookedManifestInvalid);
        auto ownedResult = ValidateAndOwnCells(descriptor, cells, limits);
        if (ownedResult.HasError())
            return Result<CookedWorldIndexManifest>::Failure(ownedResult.ErrorValue());
        auto owned = std::move(ownedResult).Value();
        return Result<CookedWorldIndexManifest>::Success(
            CookedWorldIndexManifest{std::move(descriptor), std::move(owned.cells), std::move(owned.hardDependencies),
                                     owned.totals.compressedBytes, owned.totals.uncompressedBytes});
    }

    /** @copydoc CookedWorldIndexManifest::HardDependencies */
    std::span<const StreamingCellId> CookedWorldIndexManifest::HardDependencies(const std::size_t cellIndex) const noexcept {
        if (cellIndex >= cells_.size())
            return {};
        const auto &cell = cells_[cellIndex];
        return std::span{hardDependencies_}.subspan(cell.dependencyOffset, cell.dependencyCount);
    }
}  // namespace Horo::WorldStreaming
