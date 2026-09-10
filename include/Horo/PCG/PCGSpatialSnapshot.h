#pragma once

/**
 * @file PCGSpatialSnapshot.h
 * @brief Immutable bounded provider-neutral PCG spatial input snapshots.
 */

#include "Horo/Foundation/Result.h"
#include "Horo/Math/SceneMath.h"
#include "Horo/PCG/PCGIdentity.h"
#include "Horo/PCG/PCGPointSchema.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <variant>
#include <vector>

namespace Horo::PCG {
    /** @brief Closed coordinate convention for canonical PCG spatial values. */
    enum class PCGSpatialAxisConvention : std::uint8_t {
        RightHandedYUp
    };

    /** @brief Numeric representation promised by the captured provider. */
    enum class PCGSpatialPrecision : std::uint8_t {
        Float32
    };

    /** @brief Evidence describing how much of the requested region was captured. */
    enum class PCGSpatialCoverage : std::uint8_t {
        Complete,
        Partial,
        Missing
    };

    /** @brief Canonical coordinates shared by every value in one snapshot. */
    struct PCGSpatialCoordinateContract final {
        PCGSpatialAxisConvention axes{PCGSpatialAxisConvention::RightHandedYUp}; /**< Horo semantic axis convention. */
        PCGSpatialPrecision precision{PCGSpatialPrecision::Float32};             /**< Canonical value representation. */
        float metersPerUnit{1.0F};                                               /**< Positive finite world-unit scale. */
        std::array<std::int64_t, 3> originCell{};                                /**< Large-world cell-local origin. */
        std::uint64_t originEpoch{1};                                            /**< Non-zero origin-rebase generation. */
        Math::Transform sourceToSnapshot{};                                      /**< Finite source-to-canonical transform. */
    };

    /** @brief Exact provider/source/revision evidence for captured spatial truth. */
    struct PCGSpatialProvenance final {
        SpatialProviderId provider{}; /**< Stable host-composed provider contribution. */
        SpatialSourceId source{};     /**< Stable provider-owned source truth. */
        SpatialRevision revision{};   /**< Exact semantic source revision. */
    };

    /** @brief One canonical non-degenerate surface triangle and unit normal. */
    struct PCGSurfaceTriangle final {
        SpatialElementId id{};                /**< Stable provider-owned primitive identity. */
        std::array<Math::Vec3, 3> vertices{}; /**< Canonical snapshot-space winding. */
        Math::Vec3 normal{0.0F, 1.0F, 0.0F};  /**< Finite unit normal matching provider semantics. */
    };

    /** @brief One canonical axis-aligned volume. */
    struct PCGBoxVolume final {
        SpatialElementId id{}; /**< Stable provider-owned volume identity. */
        Math::Aabb bounds{};   /**< Non-degenerate canonical snapshot-space bounds. */
    };

    /** @brief One canonical spherical volume. */
    struct PCGSphereVolume final {
        SpatialElementId id{}; /**< Stable provider-owned volume identity. */
        Math::Vec3 center{};   /**< Canonical snapshot-space center. */
        float radius{};        /**< Positive finite radius in snapshot units. */
    };

    /** @brief Closed provider-neutral volume vocabulary. */
    using PCGSpatialVolume = std::variant<PCGBoxVolume, PCGSphereVolume>;

    /** @brief Canonical cubic spline control point. */
    struct PCGSplineControlPoint final {
        Math::Vec3 position{};      /**< Canonical snapshot-space point. */
        Math::Vec3 arriveTangent{}; /**< Finite incoming tangent. */
        Math::Vec3 leaveTangent{};  /**< Finite outgoing tangent. */
    };

    /** @brief One bounded canonical spline. */
    struct PCGSpline final {
        SpatialElementId id{};                     /**< Stable provider-owned spline identity. */
        std::vector<PCGSplineControlPoint> points; /**< Owned ordered canonical control points. */
        bool closed{};                             /**< Whether the final point connects to the first. */
    };

    /** @brief One axis-aligned canonical grid definition; no samples are materialized by this contract. */
    struct PCGGrid final {
        SpatialElementId id{};                     /**< Stable provider-owned grid identity. */
        Math::Vec3 origin{};                       /**< Canonical minimum grid point. */
        Math::Vec3 spacing{1.0F, 1.0F, 1.0F};      /**< Positive finite step per axis. */
        std::array<std::uint32_t, 3> dimensions{}; /**< Positive finite point counts per axis. */
    };

    /** @brief Detached provider candidate whose owned values are validated before publication. */
    struct PCGSpatialSnapshotCandidate final {
        SpatialSnapshotId snapshot{};                             /**< Unique identity for this captured root. */
        PCGSpatialProvenance provenance{};                        /**< Exact provider/source/revision evidence. */
        PCGSpatialCoordinateContract coordinates{};               /**< Canonical coordinate and origin contract. */
        Math::Aabb bounds{};                                      /**< Complete requested snapshot-space region. */
        PCGSpatialCoverage coverage{PCGSpatialCoverage::Missing}; /**< Required coverage evidence. */
        PCGOperationalTier tier{PCGOperationalTier::Baseline};    /**< Capacity profile used for admission. */
        std::vector<PCGSurfaceTriangle> surfaces;                 /**< Detached canonical surface values. */
        std::vector<PCGSpatialVolume> volumes;                    /**< Detached canonical volume values. */
        std::vector<PCGSpline> splines;                           /**< Detached canonical spline values. */
        std::vector<PCGGrid> grids;                               /**< Detached canonical grid values. */
    };

    /** @brief Expected current provider tuple checked before a CurrentAtCommit publication. */
    struct PCGSpatialCurrentness final {
        SpatialProviderId provider{}; /**< Provider expected by the consumer. */
        SpatialSourceId source{};     /**< Source expected by the consumer. */
        SpatialRevision revision{};   /**< Exact currently published source revision. */
        std::uint64_t originEpoch{};  /**< Exact currently published origin epoch. */
    };

    /** @brief Immutable detached spatial root safe for concurrent readers and old-generation retention. */
    class PCGSpatialSnapshot final {
    public:
        struct State;

        /** @brief Opaque construction gate restricted to validated capture. */
        class ConstructionKey final {
            friend Result<PCGSpatialSnapshot> CapturePCGSpatialSnapshot(PCGSpatialSnapshotCandidate candidate);
            ConstructionKey() = default;
        };

        PCGSpatialSnapshot() = delete;

        /** @brief Adopts a fully validated immutable state through the capture-only construction gate. */
        explicit PCGSpatialSnapshot(ConstructionKey, std::shared_ptr<const State> state) : state_(std::move(state)) {}

        /** @brief Returns the unique immutable root identity. */
        [[nodiscard]] SpatialSnapshotId Id() const noexcept;
        /** @brief Returns exact provider/source/revision evidence. */
        [[nodiscard]] const PCGSpatialProvenance &Provenance() const noexcept;
        /** @brief Returns the canonical coordinate and origin contract. */
        [[nodiscard]] const PCGSpatialCoordinateContract &Coordinates() const noexcept;
        /** @brief Returns complete requested bounds. */
        [[nodiscard]] const Math::Aabb &Bounds() const noexcept;
        /** @brief Returns complete coverage evidence. */
        [[nodiscard]] PCGSpatialCoverage Coverage() const noexcept;
        /** @brief Returns canonical identity-ordered surface triangles. */
        [[nodiscard]] std::span<const PCGSurfaceTriangle> Surfaces() const noexcept;
        /** @brief Returns canonical identity-ordered volumes. */
        [[nodiscard]] std::span<const PCGSpatialVolume> Volumes() const noexcept;
        /** @brief Returns canonical identity-ordered splines. */
        [[nodiscard]] std::span<const PCGSpline> Splines() const noexcept;
        /** @brief Returns canonical identity-ordered grids. */
        [[nodiscard]] std::span<const PCGGrid> Grids() const noexcept;
        /** @brief Returns admitted resident bytes for capacity accounting. */
        [[nodiscard]] std::size_t ResidentBytes() const noexcept;

    private:
        std::shared_ptr<const State> state_;
    };

    /**
     * @brief Validates and adopts one detached provider snapshot candidate.
     * @param candidate Complete owned candidate; missing/partial coverage is rejected rather than treated as empty.
     * @return Immutable snapshot or a typed validation/capacity failure.
     */
    [[nodiscard]] Result<PCGSpatialSnapshot> CapturePCGSpatialSnapshot(PCGSpatialSnapshotCandidate candidate);

    /**
     * @brief Validates a complete replacement without mutating the current immutable root.
     * @param current Existing root whose provider/source lineage must be preserved.
     * @param candidate Detached candidate with a distinct snapshot ID and strictly newer revision.
     * @return New immutable root or a typed failure; existing readers remain valid in either case.
     */
    [[nodiscard]] Result<PCGSpatialSnapshot> ReplacePCGSpatialSnapshot(const PCGSpatialSnapshot &current,
                                                                       PCGSpatialSnapshotCandidate candidate);

    /**
     * @brief Revalidates logical currentness independently from snapshot memory lifetime.
     * @param snapshot Captured immutable root that remains memory-safe while retained.
     * @param current Exact provider/source/revision/origin tuple published by current owners.
     * @return Success only for an exact match, otherwise invalid/unknown/stale typed failure.
     */
    [[nodiscard]] Result<void> ValidatePCGSpatialSnapshotCurrent(const PCGSpatialSnapshot &snapshot, const PCGSpatialCurrentness &current);
}  // namespace Horo::PCG
