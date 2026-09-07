#pragma once

/**
 * @file CookedWorldIndexManifest.h
 * @brief Immutable cooked-cell metadata layered over one owned partition descriptor.
 */

#include "Horo/Foundation/Sha256.h"
#include "Horo/WorldStreaming/WorldPartitionDescriptor.h"

#include <cstdint>
#include <span>
#include <vector>

namespace Horo::WorldStreaming {
    /** @brief Aggregate integrity and dependency metadata for one cooked cell archive. */
    struct CookedWorldCellManifestEntry final {
        StreamingCellId cell{};                        /**< Exact cell key already declared by the partition descriptor. */
        std::uint64_t uncompressedSize{};              /**< Non-zero aggregate decoded payload bytes. */
        std::uint64_t compressedSize{};                /**< Non-zero complete encoded cell-body bytes. */
        std::uint32_t payloadCrc32{};                  /**< Aggregate CRC32 of decoded provider payloads in TOC order. */
        Sha256Digest artifactHash{};                   /**< SHA-256 of the canonical cell header and encoded body. */
        std::vector<StreamingCellId> hardDependencies; /**< Required cells in canonical identity order. */

        [[nodiscard]] auto operator<=>(const CookedWorldCellManifestEntry &) const noexcept = default;
    };

    /** @brief Caller-owned ceilings checked before a cooked manifest takes ownership. */
    struct CookedWorldIndexManifestLimits final {
        std::uint32_t maximumCellEntries{};         /**< Maximum non-zero cooked-cell record count. */
        std::uint32_t maximumDependenciesPerCell{}; /**< Maximum hard dependencies declared by one cell. */
        std::uint32_t maximumTotalDependencies{};   /**< Maximum hard dependencies across the complete manifest. */
        std::uint64_t maximumCompressedBytes{};     /**< Maximum aggregate encoded cell bytes. */
        std::uint64_t maximumUncompressedBytes{};   /**< Maximum aggregate decoded cell bytes. */
    };

    /** @brief Complete immutable runtime projection of one validated cooked world index. */
    class CookedWorldIndexManifest final {
    public:
        CookedWorldIndexManifest(const CookedWorldIndexManifest &) = delete;
        CookedWorldIndexManifest &operator=(const CookedWorldIndexManifest &) = delete;
        CookedWorldIndexManifest(CookedWorldIndexManifest &&) noexcept = default;
        CookedWorldIndexManifest &operator=(CookedWorldIndexManifest &&) = delete;

        /**
         * @brief Validates and owns cooked metadata plus its sole topology and package-location authority.
         * @details All validation, allocation, copying, and canonicalization completes before @p descriptor is moved.
         * Success consumes @p descriptor exactly once. Failure leaves it valid and unmodified, including when the caller
         * passes `std::move(descriptor)`.
         * @param descriptor Validated partition topology whose cells and chunk AssetIds are authoritative.
         * @param cells One cooked metadata record for every descriptor cell; caller storage is never retained or modified.
         * @param limits Mandatory storage and byte ceilings applied before publication.
         * @return Complete owned manifest, or a stable typed error with no partial result.
         * @throws std::bad_alloc if owned metadata allocation fails; no manifest is published and @p descriptor remains unmodified.
         */
        [[nodiscard]] static Result<CookedWorldIndexManifest> Create(WorldPartitionDescriptor &&descriptor,
                                                                     std::span<const CookedWorldCellManifestEntry> cells,
                                                                     CookedWorldIndexManifestLimits limits);

        /** @brief Returns the sole owned topology and package-location authority. @return Immutable descriptor reference. */
        [[nodiscard]] const WorldPartitionDescriptor &Descriptor() const noexcept {
            return descriptor_;
        }

        /**
         * @brief Returns cooked records in canonical cell order.
         * @return Read-only view valid until this manifest is moved from or destroyed.
         */
        [[nodiscard]] std::span<const CookedWorldCellManifestEntry> Cells() const noexcept {
            return cells_;
        }

        /** @brief Returns the checked sum of encoded cell sizes. @return Aggregate encoded bytes. */
        [[nodiscard]] constexpr std::uint64_t TotalCompressedBytes() const noexcept {
            return totalCompressedBytes_;
        }

        /** @brief Returns the checked sum of decoded cell sizes. @return Aggregate decoded bytes. */
        [[nodiscard]] constexpr std::uint64_t TotalUncompressedBytes() const noexcept {
            return totalUncompressedBytes_;
        }

    private:
        /** @brief Stores already validated and canonically ordered owned state. */
        CookedWorldIndexManifest(WorldPartitionDescriptor &&descriptor, std::vector<CookedWorldCellManifestEntry> cells,
                                 std::uint64_t totalCompressedBytes, std::uint64_t totalUncompressedBytes) noexcept;

        WorldPartitionDescriptor descriptor_;
        std::vector<CookedWorldCellManifestEntry> cells_;
        std::uint64_t totalCompressedBytes_{};
        std::uint64_t totalUncompressedBytes_{};
    };
}  // namespace Horo::WorldStreaming
