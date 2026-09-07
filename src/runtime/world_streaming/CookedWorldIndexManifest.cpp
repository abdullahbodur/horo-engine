#include "Horo/WorldStreaming/CookedWorldIndexManifest.h"

#include "Horo/WorldStreaming/WorldStreamingErrors.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace Horo::WorldStreaming {
    namespace {
        template <typename T> [[nodiscard]] Result<T> Invalid(const ErrorCodeDescriptor &descriptor) {
            return Result<T>::Failure(MakeError(descriptor));
        }

        [[nodiscard]] bool TryAccumulate(const std::uint64_t value, const std::uint64_t limit, std::uint64_t &total) noexcept {
            if (total > limit || value > limit - total) {
                return false;
            }
            total += value;
            return true;
        }

        [[nodiscard]] bool ContainsCell(const std::span<const WorldPartitionCellDescriptor> cells, const StreamingCellId &cell) noexcept {
            return std::ranges::binary_search(cells, cell, StreamingCellCanonicalLess{}, &WorldPartitionCellDescriptor::id);
        }

        [[nodiscard]] Result<void> ValidateLimits(const CookedWorldIndexManifestLimits limits) {
            if (limits.maximumCellEntries == 0 || limits.maximumDependenciesPerCell == 0 || limits.maximumTotalDependencies == 0 ||
                limits.maximumCompressedBytes == 0 || limits.maximumUncompressedBytes == 0) {
                return Invalid<void>(WorldStreamingErrors::CookedManifestInvalid);
            }
            return Result<void>::Success();
        }

        [[nodiscard]] Result<void> ValidateDependency(const StreamingCellId &owner, const StreamingCellId &dependency,
                                                      const std::span<const WorldPartitionCellDescriptor> descriptorCells) {
            if (!dependency.IsValid() || dependency == owner || !ContainsCell(descriptorCells, dependency)) {
                return Invalid<void>(WorldStreamingErrors::CookedManifestDependencyInvalid);
            }
            return Result<void>::Success();
        }

        struct OwnedCells final {
            std::vector<CookedWorldCellManifestEntry> cells;
            std::uint64_t totalCompressedBytes{};
            std::uint64_t totalUncompressedBytes{};
        };

        [[nodiscard]] Result<OwnedCells> ValidateAndOwnCells(const WorldPartitionDescriptor &descriptor,
                                                             const std::span<const CookedWorldCellManifestEntry> cells,
                                                             const CookedWorldIndexManifestLimits limits) {
            const auto descriptorCells = descriptor.Cells();
            if (cells.empty()) {
                return Invalid<OwnedCells>(WorldStreamingErrors::CookedManifestInvalid);
            }
            if (cells.size() != descriptorCells.size()) {
                return Invalid<OwnedCells>(WorldStreamingErrors::CookedManifestIdentityConflict);
            }
            if (cells.size() > limits.maximumCellEntries) {
                return Invalid<OwnedCells>(WorldStreamingErrors::CookedManifestCapacityExceeded);
            }

            std::uint64_t totalCompressedBytes{};
            std::uint64_t totalUncompressedBytes{};
            std::uint64_t totalDependencies{};
            for (const auto &cell : cells) {
                if (!cell.cell.IsValid() || cell.compressedSize == 0 || cell.uncompressedSize == 0) {
                    return Invalid<OwnedCells>(WorldStreamingErrors::CookedManifestInvalid);
                }
                if (cell.hardDependencies.size() > limits.maximumDependenciesPerCell ||
                    !TryAccumulate(cell.hardDependencies.size(), limits.maximumTotalDependencies, totalDependencies) ||
                    !TryAccumulate(cell.compressedSize, limits.maximumCompressedBytes, totalCompressedBytes) ||
                    !TryAccumulate(cell.uncompressedSize, limits.maximumUncompressedBytes, totalUncompressedBytes)) {
                    return Invalid<OwnedCells>(WorldStreamingErrors::CookedManifestCapacityExceeded);
                }
                for (const auto &dependency : cell.hardDependencies) {
                    if (const auto valid = ValidateDependency(cell.cell, dependency, descriptorCells); valid.HasError()) {
                        return Invalid<OwnedCells>(WorldStreamingErrors::CookedManifestDependencyInvalid);
                    }
                }
            }

            std::vector<CookedWorldCellManifestEntry> owned{cells.begin(), cells.end()};
            for (auto &cell : owned) {
                std::ranges::sort(cell.hardDependencies, StreamingCellCanonicalLess{});
                if (std::ranges::adjacent_find(cell.hardDependencies) != cell.hardDependencies.end()) {
                    return Invalid<OwnedCells>(WorldStreamingErrors::CookedManifestDependencyInvalid);
                }
            }
            std::ranges::sort(owned, StreamingCellCanonicalLess{}, &CookedWorldCellManifestEntry::cell);
            if (std::ranges::adjacent_find(owned, {}, &CookedWorldCellManifestEntry::cell) != owned.end()) {
                return Invalid<OwnedCells>(WorldStreamingErrors::CookedManifestIdentityConflict);
            }
            for (std::size_t index = 0; index < owned.size(); ++index) {
                if (owned[index].cell != descriptorCells[index].id) {
                    return Invalid<OwnedCells>(WorldStreamingErrors::CookedManifestIdentityConflict);
                }
            }

            return Result<OwnedCells>::Success(OwnedCells{std::move(owned), totalCompressedBytes, totalUncompressedBytes});
        }
    }  // namespace

    /** @copydoc CookedWorldIndexManifest::CookedWorldIndexManifest */
    CookedWorldIndexManifest::CookedWorldIndexManifest(WorldPartitionDescriptor &&descriptor,
                                                       std::vector<CookedWorldCellManifestEntry> cells,
                                                       const std::uint64_t totalCompressedBytes,
                                                       const std::uint64_t totalUncompressedBytes) noexcept
        : descriptor_(std::move(descriptor)), cells_(std::move(cells)), totalCompressedBytes_(totalCompressedBytes),
          totalUncompressedBytes_(totalUncompressedBytes) {}

    /** @copydoc CookedWorldIndexManifest::Create */
    Result<CookedWorldIndexManifest> CookedWorldIndexManifest::Create(WorldPartitionDescriptor &&descriptor,
                                                                      const std::span<const CookedWorldCellManifestEntry> cells,
                                                                      const CookedWorldIndexManifestLimits limits) {
        if (const auto validLimits = ValidateLimits(limits); validLimits.HasError()) {
            return Invalid<CookedWorldIndexManifest>(WorldStreamingErrors::CookedManifestInvalid);
        }
        auto ownedResult = ValidateAndOwnCells(descriptor, cells, limits);
        if (ownedResult.HasError()) {
            return Result<CookedWorldIndexManifest>::Failure(ownedResult.ErrorValue());
        }
        auto owned = std::move(ownedResult).Value();
        return Result<CookedWorldIndexManifest>::Success(CookedWorldIndexManifest{std::move(descriptor), std::move(owned.cells),
                                                                                  owned.totalCompressedBytes,
                                                                                  owned.totalUncompressedBytes});
    }
}  // namespace Horo::WorldStreaming
