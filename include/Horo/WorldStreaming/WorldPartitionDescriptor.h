#pragma once

/**
 * @file WorldPartitionDescriptor.h
 * @brief Immutable validated runtime projection of a versioned world-partition manifest.
 */

#include "Horo/Assets/AssetId.h"
#include "Horo/WorldStreaming/WorldCellQuantization.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace Horo::WorldStreaming {
    /** @brief Supported world-partition descriptor schema version. */
    struct WorldPartitionSchemaVersion final {
        static constexpr std::uint16_t CurrentMajor = 1;
        static constexpr std::uint16_t CurrentMinor = 0;

        std::uint16_t major{CurrentMajor}; /**< Breaking schema version. */
        std::uint16_t minor{CurrentMinor}; /**< Backward-compatible schema version. */

        [[nodiscard]] constexpr auto operator<=>(const WorldPartitionSchemaVersion &) const noexcept = default;
    };

    /** @brief Exact inclusive world-space content bounds in canonical millimeters. */
    struct WorldPartitionBounds final {
        Math::WorldCoordinate64 minimum{}; /**< Inclusive minimum content coordinate. */
        Math::WorldCoordinate64 maximum{}; /**< Inclusive maximum content coordinate. */

        [[nodiscard]] constexpr auto operator<=>(const WorldPartitionBounds &) const noexcept = default;
    };

    /** @brief Authority that owns authored state for one streaming layer. */
    enum class WorldLayerOwnership : std::uint8_t {
        WorldStreaming,
        GameplayScript,
        NetworkReplication,
    };

    /** @brief Versioned behavioral metadata for a streaming layer. */
    enum class WorldLayerFlags : std::uint32_t {
        None = 0,
        Persistent = 1U << 0U,
        Optional = 1U << 1U,
        ServerOnly = 1U << 2U,
        ClientOnly = 1U << 3U,
    };

    /** @brief Combines declared streaming-layer flags. */
    [[nodiscard]] constexpr WorldLayerFlags operator|(const WorldLayerFlags left, const WorldLayerFlags right) noexcept {
        return static_cast<WorldLayerFlags>(static_cast<std::uint32_t>(left) | static_cast<std::uint32_t>(right));
    }

    /** @brief One named layer definition owned by a world-partition descriptor. */
    struct WorldLayerDescriptor final {
        StreamingLayerId id{};           /**< Stable manifest layer identity. */
        std::string name{};              /**< Non-empty owned UTF-8 presentation name. */
        WorldLayerOwnership ownership{}; /**< Authored-state authority. */
        WorldLayerFlags flags{};         /**< Known versioned flags only. */
        float priorityMultiplier{1.0F};  /**< Finite positive scheduling multiplier. */

        [[nodiscard]] auto operator<=>(const WorldLayerDescriptor &) const noexcept = default;
    };

    /** @brief Stable package reference for one streamed cell. */
    struct WorldCellPackageReference final {
        Assets::AssetId chunkAsset{}; /**< Non-zero path-independent chunk asset identity. */

        [[nodiscard]] constexpr auto operator<=>(const WorldCellPackageReference &) const noexcept = default;
    };

    /** @brief One canonical cell identity and its immutable package reference. */
    struct WorldPartitionCellDescriptor final {
        StreamingCellId id{};                /**< Exact manifest cell tuple. */
        WorldCellPackageReference package{}; /**< Stable chunk package reference. */

        [[nodiscard]] constexpr auto operator<=>(const WorldPartitionCellDescriptor &) const noexcept = default;
    };

    /** @brief Caller-owned hard ceilings applied before descriptor storage is allocated. */
    struct WorldPartitionDescriptorLimits final {
        std::uint32_t maximumLayers{};         /**< Maximum non-zero layer count. */
        std::uint32_t maximumCells{};          /**< Maximum non-zero cell count. */
        std::uint32_t maximumLayerNameBytes{}; /**< Maximum aggregate layer-name bytes. */
    };

    /** @brief Owned immutable runtime descriptor produced from one validated world.index projection. */
    class WorldPartitionDescriptor final {
    public:
        WorldPartitionDescriptor(const WorldPartitionDescriptor &) = delete;
        WorldPartitionDescriptor &operator=(const WorldPartitionDescriptor &) = delete;
        WorldPartitionDescriptor(WorldPartitionDescriptor &&) noexcept = default;
        WorldPartitionDescriptor &operator=(WorldPartitionDescriptor &&) = delete;

        /**
         * @brief Validates, canonically orders, and copies a complete partition descriptor transactionally.
         * @param version Exact supported schema version.
         * @param partition Stable non-zero partition identity.
         * @param bounds Ordered content bounds contained by the level-zero grid envelope.
         * @param grid Previously validated immutable cell quantization policy.
         * @param layers Non-empty layer definitions; input storage is never retained or modified.
         * @param cells Non-empty cell definitions; input storage is never retained or modified.
         * @param limits Mandatory host ceilings applied before owned storage is published.
         * @return Owned immutable descriptor, or a stable typed descriptor error with no partial result.
         */
        [[nodiscard]] static Result<WorldPartitionDescriptor> Create(WorldPartitionSchemaVersion version, WorldPartitionId partition,
                                                                     WorldPartitionBounds bounds, const WorldCellQuantizationPolicy &grid,
                                                                     std::span<const WorldLayerDescriptor> layers,
                                                                     std::span<const WorldPartitionCellDescriptor> cells,
                                                                     WorldPartitionDescriptorLimits limits);

        /** @brief Returns the admitted schema version. @return Immutable version value. */
        [[nodiscard]] constexpr WorldPartitionSchemaVersion Version() const noexcept {
            return version_;
        }

        /** @brief Returns the stable partition identity. @return Immutable identity value. */
        [[nodiscard]] constexpr const WorldPartitionId &Partition() const noexcept {
            return partition_;
        }

        /** @brief Returns exact content bounds. @return Immutable bounds value. */
        [[nodiscard]] constexpr const WorldPartitionBounds &Bounds() const noexcept {
            return bounds_;
        }

        /** @brief Returns the validated grid policy. @return Immutable policy reference owned by this descriptor. */
        [[nodiscard]] constexpr const WorldCellQuantizationPolicy &Grid() const noexcept {
            return grid_;
        }

        /**
         * @brief Returns layer definitions in ascending identity order.
         * @return Read-only view valid until this descriptor is moved from or destroyed.
         */
        [[nodiscard]] std::span<const WorldLayerDescriptor> Layers() const noexcept {
            return layers_;
        }

        /**
         * @brief Returns cells ordered by layer, LOD, Z, Y, then X.
         * @return Read-only view valid until this descriptor is moved from or destroyed.
         */
        [[nodiscard]] std::span<const WorldPartitionCellDescriptor> Cells() const noexcept {
            return cells_;
        }

    private:
        /** @brief Stores already validated and canonically ordered owned state. */
        WorldPartitionDescriptor(WorldPartitionSchemaVersion version, WorldPartitionId partition, WorldPartitionBounds bounds,
                                 WorldCellQuantizationPolicy grid, std::vector<WorldLayerDescriptor> layers,
                                 std::vector<WorldPartitionCellDescriptor> cells) noexcept;

        WorldPartitionSchemaVersion version_{};
        WorldPartitionId partition_{};
        WorldPartitionBounds bounds_{};
        WorldCellQuantizationPolicy grid_;
        std::vector<WorldLayerDescriptor> layers_;
        std::vector<WorldPartitionCellDescriptor> cells_;
    };
}  // namespace Horo::WorldStreaming
