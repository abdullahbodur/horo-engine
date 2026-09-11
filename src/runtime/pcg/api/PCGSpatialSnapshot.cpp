#include "Horo/PCG/PCGSpatialSnapshot.h"

#include "Horo/PCG/PCGErrors.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <new>
#include <type_traits>

namespace Horo::PCG {
    struct PCGSpatialSnapshot::State final {
        State(PCGSpatialSnapshotCandidate candidate, const std::size_t residentBytesValue)
            : id(candidate.snapshot), provenance(std::move(candidate.provenance)), coordinates(std::move(candidate.coordinates)),
              bounds(candidate.bounds), coverage(candidate.coverage), surfaces(std::move(candidate.surfaces)),
              volumes(std::move(candidate.volumes)), splines(std::move(candidate.splines)), grids(std::move(candidate.grids)),
              residentBytes(residentBytesValue) {}

        SpatialSnapshotId id;
        PCGSpatialProvenance provenance;
        PCGSpatialCoordinateContract coordinates;
        Math::Aabb bounds;
        PCGSpatialCoverage coverage;
        std::vector<PCGSurfaceTriangle> surfaces;
        std::vector<PCGSpatialVolume> volumes;
        std::vector<PCGSpline> splines;
        std::vector<PCGGrid> grids;
        std::size_t residentBytes{};
    };

    namespace {
        constexpr float NormalTolerance = 0.001F;

        [[nodiscard]] Error Failure(const ErrorCodeDescriptor &descriptor) {
            return MakeError(descriptor);
        }

        [[nodiscard]] bool StrictBounds(const Math::Aabb &bounds) noexcept {
            return bounds.IsValid() && bounds.minimum.x < bounds.maximum.x && bounds.minimum.y < bounds.maximum.y &&
                   bounds.minimum.z < bounds.maximum.z;
        }

        [[nodiscard]] bool Contains(const Math::Aabb &bounds, const Math::Vec3 value) noexcept {
            return Math::IsFinite(value) && value.x >= bounds.minimum.x && value.x <= bounds.maximum.x && value.y >= bounds.minimum.y &&
                   value.y <= bounds.maximum.y && value.z >= bounds.minimum.z && value.z <= bounds.maximum.z;
        }

        [[nodiscard]] bool Contains(const Math::Aabb &outer, const Math::Aabb &inner) noexcept {
            return StrictBounds(inner) && Contains(outer, inner.minimum) && Contains(outer, inner.maximum);
        }

        [[nodiscard]] bool ValidCoordinates(const PCGSpatialCoordinateContract &coordinates) noexcept {
            const bool knownAxes = coordinates.axes == PCGSpatialAxisConvention::RightHandedYUp;
            const bool knownPrecision = coordinates.precision == PCGSpatialPrecision::Float32;
            const Math::Vec3 scale = coordinates.sourceToSnapshot.scale;
            return knownAxes && knownPrecision && std::isfinite(coordinates.metersPerUnit) && coordinates.metersPerUnit > 0.0F &&
                   coordinates.originEpoch != 0 && std::abs(scale.x) > Math::DefaultEpsilon && std::abs(scale.y) > Math::DefaultEpsilon &&
                   std::abs(scale.z) > Math::DefaultEpsilon && coordinates.sourceToSnapshot.TryToMatrix().HasValue();
        }

        [[nodiscard]] SpatialElementId VolumeId(const PCGSpatialVolume &volume) noexcept {
            return std::visit([](const auto &value) {
                return value.id;
            }, volume);
        }

        [[nodiscard]] bool ValidSurface(const PCGSurfaceTriangle &surface, const Math::Aabb &bounds) noexcept {
            if (!surface.id.IsValid() || !Contains(bounds, surface.vertices[0]) || !Contains(bounds, surface.vertices[1]) ||
                !Contains(bounds, surface.vertices[2]) || !Math::IsFinite(surface.normal))
                return false;
            const Math::Vec3 geometricNormal =
                Math::Cross(surface.vertices[1] - surface.vertices[0], surface.vertices[2] - surface.vertices[0]);
            const float geometricLengthSquared = Math::LengthSquared(geometricNormal);
            if (const float declaredLengthSquared = Math::LengthSquared(surface.normal);
                !std::isfinite(geometricLengthSquared) || geometricLengthSquared <= Math::DefaultEpsilon * Math::DefaultEpsilon ||
                !std::isfinite(declaredLengthSquared) || std::abs(declaredLengthSquared - 1.0F) > NormalTolerance)
                return false;
            const float alignment = Math::Dot(geometricNormal, surface.normal) / std::sqrt(geometricLengthSquared);
            return std::isfinite(alignment) && alignment >= 1.0F - NormalTolerance;
        }

        [[nodiscard]] bool ValidVolume(const PCGSpatialVolume &volume, const Math::Aabb &bounds) noexcept {
            return std::visit([&]<typename T>(const T &value) {
                if (!value.id.IsValid())
                    return false;
                if constexpr (std::is_same_v<T, PCGBoxVolume>) {
                    return Contains(bounds, value.bounds);
                } else {
                    if (!Contains(bounds, value.center) || !std::isfinite(value.radius) || value.radius <= 0.0F)
                        return false;
                    return value.center.x - value.radius >= bounds.minimum.x && value.center.x + value.radius <= bounds.maximum.x &&
                           value.center.y - value.radius >= bounds.minimum.y && value.center.y + value.radius <= bounds.maximum.y &&
                           value.center.z - value.radius >= bounds.minimum.z && value.center.z + value.radius <= bounds.maximum.z;
                }
            }, volume);
        }

        [[nodiscard]] bool ValidSpline(const PCGSpline &spline, const Math::Aabb &bounds) noexcept {
            if (const std::size_t minimumPoints = spline.closed ? 3U : 2U; !spline.id.IsValid() || spline.points.size() < minimumPoints)
                return false;
            for (std::size_t index = 0; index < spline.points.size(); ++index) {
                const auto &point = spline.points[index];
                if (!Contains(bounds, point.position) || !Math::IsFinite(point.arriveTangent) || !Math::IsFinite(point.leaveTangent))
                    return false;
                if (index > 0 && point.position == spline.points[index - 1].position)
                    return false;
            }
            return !spline.closed || spline.points.back().position != spline.points.front().position;
        }

        [[nodiscard]] Result<std::size_t> GridPointCount(const PCGGrid &grid, const Math::Aabb &bounds) {
            if (!grid.id.IsValid() || !Contains(bounds, grid.origin) || !Math::IsFinite(grid.spacing) || grid.spacing.x <= 0.0F ||
                grid.spacing.y <= 0.0F || grid.spacing.z <= 0.0F || std::ranges::any_of(grid.dimensions, [](const std::uint32_t value) {
                return value == 0;
            }))
                return Result<std::size_t>::Failure(Failure(PCGErrors::SpatialInputInvalid));
            auto count = CheckedPCGMultiply(static_cast<std::size_t>(grid.dimensions[0]), static_cast<std::size_t>(grid.dimensions[1]));
            if (count.HasError())
                return Result<std::size_t>::Failure(Failure(PCGErrors::SpatialCapacityExceeded));
            count = CheckedPCGMultiply(count.Value(), static_cast<std::size_t>(grid.dimensions[2]));
            if (count.HasError())
                return Result<std::size_t>::Failure(Failure(PCGErrors::SpatialCapacityExceeded));
            const auto endpoint = [&](const float origin, const float spacing, const std::uint32_t dimension) {
                return static_cast<double>(origin) + static_cast<double>(spacing) * static_cast<double>(dimension - 1U);
            };
            const double maximumX = endpoint(grid.origin.x, grid.spacing.x, grid.dimensions[0]);
            const double maximumY = endpoint(grid.origin.y, grid.spacing.y, grid.dimensions[1]);
            if (const double maximumZ = endpoint(grid.origin.z, grid.spacing.z, grid.dimensions[2]);
                !std::isfinite(maximumX) || !std::isfinite(maximumY) || !std::isfinite(maximumZ) || maximumX > bounds.maximum.x ||
                maximumY > bounds.maximum.y || maximumZ > bounds.maximum.z)
                return Result<std::size_t>::Failure(Failure(PCGErrors::SpatialInputInvalid));
            return count;
        }

        [[nodiscard]] Result<std::size_t> AccumulateBytes(Result<std::size_t> total, const std::size_t count,
                                                          const std::size_t elementSize) {
            if (total.HasError())
                return total;
            auto bytes = CheckedPCGMultiply(count, elementSize);
            if (bytes.HasError())
                return bytes;
            return CheckedPCGAdd(total.Value(), bytes.Value());
        }

        [[nodiscard]] Result<std::size_t> AccountBytes(const PCGSpatialSnapshotCandidate &candidate) {
            auto surfaceBytes = CheckedPCGMultiply(candidate.surfaces.capacity(), sizeof(PCGSurfaceTriangle));
            if (surfaceBytes.HasError())
                return surfaceBytes;
            auto total = CheckedPCGAdd(sizeof(PCGSpatialSnapshot::State), surfaceBytes.Value());
            if (total.HasError())
                return total;
            const std::array counts{candidate.volumes.capacity(), candidate.splines.capacity(), candidate.grids.capacity()};
            const std::array sizes{sizeof(PCGSpatialVolume), sizeof(PCGSpline), sizeof(PCGGrid)};
            for (std::size_t index = 0; index < counts.size(); ++index)
                total = AccumulateBytes(std::move(total), counts[index], sizes[index]);
            for (const auto &spline : candidate.splines)
                total = AccumulateBytes(std::move(total), spline.points.capacity(), sizeof(PCGSplineControlPoint));
            return total;
        }

        [[nodiscard]] Result<void> ValidateEnvelope(const PCGSpatialSnapshotCandidate &candidate) {
            using enum PCGSpatialCoverage;
            if (!candidate.snapshot.IsValid() || !candidate.provenance.provider.IsValid() || !candidate.provenance.source.IsValid() ||
                !candidate.provenance.revision.IsValid() || !StrictBounds(candidate.bounds))
                return Result<void>::Failure(Failure(PCGErrors::SpatialInputInvalid));
            if (!ValidCoordinates(candidate.coordinates))
                return Result<void>::Failure(Failure(PCGErrors::SpatialCoordinatesUnsupported));
            if (candidate.coverage == Partial || candidate.coverage == Missing)
                return Result<void>::Failure(Failure(PCGErrors::SpatialCoverageUnavailable));
            if (candidate.coverage != Complete)
                return Result<void>::Failure(Failure(PCGErrors::SpatialInputInvalid));
            return Result<void>::Success();
        }

        [[nodiscard]] Result<std::size_t> CountElements(const PCGSpatialSnapshotCandidate &candidate, const std::size_t maximumElements) {
            auto count = CheckedPCGAdd(candidate.surfaces.size(), candidate.volumes.size());
            if (count.HasError())
                return Result<std::size_t>::Failure(Failure(PCGErrors::SpatialCapacityExceeded));
            count = CheckedPCGAdd(count.Value(), candidate.splines.size());
            if (count.HasError())
                return Result<std::size_t>::Failure(Failure(PCGErrors::SpatialCapacityExceeded));
            count = CheckedPCGAdd(count.Value(), candidate.grids.size());
            if (count.HasError() || count.Value() > maximumElements)
                return Result<std::size_t>::Failure(Failure(PCGErrors::SpatialCapacityExceeded));
            return count;
        }

        [[nodiscard]] Result<std::vector<SpatialElementId>> CollectIdentities(const PCGSpatialSnapshotCandidate &candidate,
                                                                              const std::size_t maximumControlPoints,
                                                                              const std::size_t maximumGridPoints) {
            std::vector<SpatialElementId> identities;
            identities.reserve(candidate.surfaces.size() + candidate.volumes.size() + candidate.splines.size() + candidate.grids.size());
            for (const auto &surface : candidate.surfaces) {
                if (!ValidSurface(surface, candidate.bounds))
                    return Result<std::vector<SpatialElementId>>::Failure(Failure(PCGErrors::SpatialInputInvalid));
                identities.push_back(surface.id);
            }
            for (const auto &volume : candidate.volumes) {
                if (!ValidVolume(volume, candidate.bounds))
                    return Result<std::vector<SpatialElementId>>::Failure(Failure(PCGErrors::SpatialInputInvalid));
                identities.push_back(VolumeId(volume));
            }
            std::size_t controlPointCount{};
            for (const auto &spline : candidate.splines) {
                if (!ValidSpline(spline, candidate.bounds))
                    return Result<std::vector<SpatialElementId>>::Failure(Failure(PCGErrors::SpatialInputInvalid));
                identities.push_back(spline.id);
                auto next = CheckedPCGAdd(controlPointCount, spline.points.size());
                if (next.HasError())
                    return Result<std::vector<SpatialElementId>>::Failure(Failure(PCGErrors::SpatialCapacityExceeded));
                controlPointCount = next.Value();
            }
            if (controlPointCount > maximumControlPoints)
                return Result<std::vector<SpatialElementId>>::Failure(Failure(PCGErrors::SpatialCapacityExceeded));
            for (const auto &grid : candidate.grids) {
                auto points = GridPointCount(grid, candidate.bounds);
                if (points.HasError())
                    return Result<std::vector<SpatialElementId>>::Failure(points.ErrorValue());
                if (points.Value() > maximumGridPoints)
                    return Result<std::vector<SpatialElementId>>::Failure(Failure(PCGErrors::SpatialCapacityExceeded));
                identities.push_back(grid.id);
            }
            return Result<std::vector<SpatialElementId>>::Success(std::move(identities));
        }

        template <typename Values, typename Projection> void SortByIdentity(Values &values, Projection projection) {
            std::ranges::sort(values, {}, projection);
        }

        [[nodiscard]] Result<std::size_t> ValidateAndCanonicalize(PCGSpatialSnapshotCandidate &candidate) {
            if (auto envelope = ValidateEnvelope(candidate); envelope.HasError())
                return Result<std::size_t>::Failure(envelope.ErrorValue());
            auto tierLimits = LimitsForTier(candidate.tier);
            if (tierLimits.HasError())
                return Result<std::size_t>::Failure(Failure(PCGErrors::SpatialInputInvalid));
            if (auto elementCount = CountElements(candidate, tierLimits.Value().maximumPointsPerNodeOutput); elementCount.HasError())
                return elementCount;
            auto collected = CollectIdentities(candidate, tierLimits.Value().maximumMaterializedPointRecords,
                                               tierLimits.Value().maximumPointsPerNodeOutput);
            if (collected.HasError())
                return Result<std::size_t>::Failure(collected.ErrorValue());
            auto identities = std::move(collected).Value();
            std::ranges::sort(identities);
            if (std::ranges::adjacent_find(identities) != identities.end())
                return Result<std::size_t>::Failure(Failure(PCGErrors::SpatialInputInvalid));
            SortByIdentity(candidate.surfaces, &PCGSurfaceTriangle::id);
            SortByIdentity(candidate.volumes, VolumeId);
            SortByIdentity(candidate.splines, &PCGSpline::id);
            SortByIdentity(candidate.grids, &PCGGrid::id);
            candidate.surfaces.shrink_to_fit();
            candidate.volumes.shrink_to_fit();
            candidate.grids.shrink_to_fit();
            for (auto &spline : candidate.splines)
                spline.points.shrink_to_fit();
            candidate.splines.shrink_to_fit();
            auto residentBytes = AccountBytes(candidate);
            if (residentBytes.HasError() || residentBytes.Value() > tierLimits.Value().maximumInputSnapshotBytes)
                return Result<std::size_t>::Failure(Failure(PCGErrors::SpatialCapacityExceeded));
            return residentBytes;
        }
    }  // namespace

    /** @copydoc PCGSpatialSnapshot::Id */
    SpatialSnapshotId PCGSpatialSnapshot::Id() const noexcept {
        return state_->id;
    }

    /** @copydoc PCGSpatialSnapshot::Provenance */
    const PCGSpatialProvenance &PCGSpatialSnapshot::Provenance() const noexcept {
        return state_->provenance;
    }

    /** @copydoc PCGSpatialSnapshot::Coordinates */
    const PCGSpatialCoordinateContract &PCGSpatialSnapshot::Coordinates() const noexcept {
        return state_->coordinates;
    }

    /** @copydoc PCGSpatialSnapshot::Bounds */
    const Math::Aabb &PCGSpatialSnapshot::Bounds() const noexcept {
        return state_->bounds;
    }

    /** @copydoc PCGSpatialSnapshot::Coverage */
    PCGSpatialCoverage PCGSpatialSnapshot::Coverage() const noexcept {
        return state_->coverage;
    }

    /** @copydoc PCGSpatialSnapshot::Surfaces */
    std::span<const PCGSurfaceTriangle> PCGSpatialSnapshot::Surfaces() const noexcept {
        return state_->surfaces;
    }

    /** @copydoc PCGSpatialSnapshot::Volumes */
    std::span<const PCGSpatialVolume> PCGSpatialSnapshot::Volumes() const noexcept {
        return state_->volumes;
    }

    /** @copydoc PCGSpatialSnapshot::Splines */
    std::span<const PCGSpline> PCGSpatialSnapshot::Splines() const noexcept {
        return state_->splines;
    }

    /** @copydoc PCGSpatialSnapshot::Grids */
    std::span<const PCGGrid> PCGSpatialSnapshot::Grids() const noexcept {
        return state_->grids;
    }

    /** @copydoc PCGSpatialSnapshot::ResidentBytes */
    std::size_t PCGSpatialSnapshot::ResidentBytes() const noexcept {
        return state_->residentBytes;
    }

    /** @copydoc CapturePCGSpatialSnapshot */
    Result<PCGSpatialSnapshot> CapturePCGSpatialSnapshot(PCGSpatialSnapshotCandidate candidate) {
        try {
            auto bytes = ValidateAndCanonicalize(candidate);
            if (bytes.HasError())
                return Result<PCGSpatialSnapshot>::Failure(bytes.ErrorValue());
            auto state = std::make_shared<const PCGSpatialSnapshot::State>(std::move(candidate), bytes.Value());
            return Result<PCGSpatialSnapshot>::Success(PCGSpatialSnapshot{PCGSpatialSnapshot::ConstructionKey{}, std::move(state)});
        } catch (const std::bad_alloc &) {
            return Result<PCGSpatialSnapshot>::Failure(Failure(PCGErrors::SpatialCapacityExceeded));
        }
    }

    /** @copydoc ReplacePCGSpatialSnapshot */
    Result<PCGSpatialSnapshot> ReplacePCGSpatialSnapshot(const PCGSpatialSnapshot &current, PCGSpatialSnapshotCandidate candidate) {
        if (candidate.provenance.provider != current.Provenance().provider || candidate.provenance.source != current.Provenance().source ||
            candidate.snapshot == current.Id() || candidate.provenance.revision <= current.Provenance().revision)
            return Result<PCGSpatialSnapshot>::Failure(Failure(PCGErrors::SpatialReplacementInvalid));
        return CapturePCGSpatialSnapshot(std::move(candidate));
    }

    /** @copydoc ValidatePCGSpatialSnapshotCurrent */
    Result<void> ValidatePCGSpatialSnapshotCurrent(const PCGSpatialSnapshot &snapshot, const PCGSpatialCurrentness &current) {
        if (!current.provider.IsValid() || !current.source.IsValid() || !current.revision.IsValid() || current.originEpoch == 0)
            return Result<void>::Failure(Failure(PCGErrors::SpatialInputInvalid));
        if (snapshot.Provenance().provider != current.provider || snapshot.Provenance().source != current.source)
            return Result<void>::Failure(Failure(PCGErrors::IdentityUnknown));
        if (snapshot.Provenance().revision != current.revision || snapshot.Coordinates().originEpoch != current.originEpoch)
            return Result<void>::Failure(Failure(PCGErrors::SpatialSnapshotStale));
        return Result<void>::Success();
    }
}  // namespace Horo::PCG
