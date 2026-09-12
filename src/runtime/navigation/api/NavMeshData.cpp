#include "Horo/Navigation/NavMeshData.h"

#include "Horo/Navigation/NavigationErrors.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <utility>

namespace Horo::Navigation {
    namespace {
        template <typename T> [[nodiscard]] Result<T> Failure(const ErrorCodeDescriptor &descriptor) {
            return Result<T>::Failure(MakeError(descriptor));
        }

        [[nodiscard]] constexpr bool IsKnown(const NavMeshCompression value) noexcept {
            return value < NavMeshCompression::Count;
        }

        [[nodiscard]] constexpr bool IsKnown(const NavMeshByteOrder value) noexcept {
            return value < NavMeshByteOrder::Count;
        }

        [[nodiscard]] constexpr bool IsKnown(const NavigationSourceProducerKind value) noexcept {
            return value < NavigationSourceProducerKind::Count;
        }

        [[nodiscard]] bool IsPresent(const Sha256Digest &digest) noexcept {
            return std::ranges::any_of(digest.bytes, [](const std::uint8_t value) {
                return value != 0;
            });
        }

        [[nodiscard]] bool IsValid(const NavigationAgentBuildGeometry &geometry) noexcept {
            const std::array positive{geometry.radiusMeters, geometry.heightMeters, geometry.cellSizeMeters, geometry.cellHeightMeters};
            const std::array nonNegative{geometry.maxSlopeDegrees, geometry.stepHeightMeters, geometry.minimumRegionSizeMeters};
            const auto positiveFinite = [](const float value) {
                return std::isfinite(value) && value > 0.0F;
            };
            const auto nonNegativeFinite = [](const float value) {
                return std::isfinite(value) && value >= 0.0F;
            };
            return std::ranges::all_of(positive, positiveFinite) && std::ranges::all_of(nonNegative, nonNegativeFinite) &&
                   geometry.maxSlopeDegrees < 90.0F && geometry.stepHeightMeters < geometry.heightMeters;
        }

        [[nodiscard]] constexpr bool IsValidCountLimits(const NavMeshArtifactLimits &limits) noexcept {
            return limits.maxTiles > 0 && limits.maxTiles <= NavMeshArtifactLimits::MaximumTiles && limits.maxVertices > 0 &&
                   limits.maxVertices <= NavMeshArtifactLimits::MaximumVertices && limits.maxPolygons > 0 &&
                   limits.maxPolygons <= NavMeshArtifactLimits::MaximumPolygons && limits.maxPolygonVertexIndices > 0 &&
                   limits.maxPolygonVertexIndices <= NavMeshArtifactLimits::MaximumPolygonVertexIndices &&
                   limits.maxPolygonAdjacencies > 0 && limits.maxPolygonAdjacencies <= NavMeshArtifactLimits::MaximumPolygonAdjacencies &&
                   limits.maxOffMeshLinks > 0 && limits.maxOffMeshLinks <= NavMeshArtifactLimits::MaximumOffMeshLinks &&
                   limits.maxProvenanceRows > 0 && limits.maxProvenanceRows <= NavMeshArtifactLimits::MaximumProvenanceRows &&
                   limits.maxProviderPayloads > 0 && limits.maxProviderPayloads <= NavMeshArtifactLimits::MaximumProviderPayloads &&
                   limits.maxVerticesPerPolygon >= 3 && limits.maxVerticesPerPolygon <= NavMeshArtifactLimits::MaximumVerticesPerPolygon;
        }

        [[nodiscard]] constexpr bool IsValidByteLimits(const NavMeshArtifactLimits &limits) noexcept {
            return limits.maxPortableEncodedBytes > 0 &&
                   limits.maxPortableEncodedBytes <= NavMeshArtifactLimits::MaximumPortableEncodedBytes &&
                   limits.maxPortableDecodedBytes > 0 &&
                   limits.maxPortableDecodedBytes <= NavMeshArtifactLimits::MaximumPortableDecodedBytes &&
                   limits.maxProviderEncodedBytes > 0 &&
                   limits.maxProviderEncodedBytes <= NavMeshArtifactLimits::MaximumProviderEncodedBytes &&
                   limits.maxProviderDecodedBytes > 0 &&
                   limits.maxProviderDecodedBytes <= NavMeshArtifactLimits::MaximumProviderDecodedBytes && limits.maxOwnedBytes > 0 &&
                   limits.maxOwnedBytes <= NavMeshArtifactLimits::MaximumOwnedBytes;
        }

        [[nodiscard]] constexpr bool IsValidLimits(const NavMeshArtifactLimits &limits) noexcept {
            return IsValidCountLimits(limits) && IsValidByteLimits(limits);
        }

        [[nodiscard]] bool CheckedAdd(const std::uint64_t left, const std::uint64_t right, std::uint64_t &sum) noexcept {
            if (right > std::numeric_limits<std::uint64_t>::max() - left)
                return false;
            sum = left + right;
            return true;
        }

        [[nodiscard]] bool CheckedMultiply(const std::uint64_t left, const std::uint64_t right, std::uint64_t &product) noexcept {
            if (left != 0 && right > std::numeric_limits<std::uint64_t>::max() / left)
                return false;
            product = left * right;
            return true;
        }

        [[nodiscard]] bool AccumulateStorage(const std::uint64_t count, const std::uint64_t elementBytes, std::uint64_t &total) noexcept {
            std::uint64_t bytes{};
            return CheckedMultiply(count, elementBytes, bytes) && CheckedAdd(total, bytes, total);
        }

        [[nodiscard]] bool FitsRange(const NavMeshTableRange range, const std::size_t tableSize) noexcept {
            const auto first = static_cast<std::uint64_t>(range.first);
            const auto count = static_cast<std::uint64_t>(range.count);
            return first <= tableSize && count <= static_cast<std::uint64_t>(tableSize) - first;
        }

        [[nodiscard]] constexpr std::uint64_t RangeEnd(const NavMeshTableRange range) noexcept {
            return static_cast<std::uint64_t>(range.first) + static_cast<std::uint64_t>(range.count);
        }

        [[nodiscard]] constexpr bool Contains(const NavMeshTableRange range, const std::uint32_t index) noexcept {
            return index >= range.first && static_cast<std::uint64_t>(index) < RangeEnd(range);
        }

        template <typename T> [[nodiscard]] std::span<const T> Slice(const std::span<const T> values, const NavMeshTableRange range) {
            return values.subspan(static_cast<std::size_t>(range.first), static_cast<std::size_t>(range.count));
        }

        [[nodiscard]] bool IsInside(const Math::Vec3 point, const Math::Aabb &bounds) noexcept {
            return point.x >= bounds.minimum.x && point.x <= bounds.maximum.x && point.y >= bounds.minimum.y &&
                   point.y <= bounds.maximum.y && point.z >= bounds.minimum.z && point.z <= bounds.maximum.z;
        }

        [[nodiscard]] bool MatchesTileGrid(const NavMeshArtifactHeader &header, const NavMeshTileDescriptor &tile) noexcept {
            const float tileSize = header.coordinateFrame.tileSizeMeters;
            const float minimumX = static_cast<float>(tile.key.x) * tileSize;
            const float minimumZ = static_cast<float>(tile.key.z) * tileSize;
            return std::isfinite(minimumX) && std::isfinite(minimumZ) && Math::NearlyEqual(tile.bounds.minimum.x, minimumX) &&
                   Math::NearlyEqual(tile.bounds.maximum.x, minimumX + tileSize) && Math::NearlyEqual(tile.bounds.minimum.z, minimumZ) &&
                   Math::NearlyEqual(tile.bounds.maximum.z, minimumZ + tileSize);
        }

        [[nodiscard]] Result<void> MeasureOwnedStorage(const NavMeshArtifactHeader &header, const NavMeshArtifactLimits &limits) {
            std::uint64_t bytes{};
            const bool measured = AccumulateStorage(header.tileCount, sizeof(NavMeshTileDescriptor), bytes) &&
                                  AccumulateStorage(header.vertexCount, sizeof(Math::Vec3), bytes) &&
                                  AccumulateStorage(header.polygonCount, sizeof(NavMeshPolygon), bytes) &&
                                  AccumulateStorage(header.polygonVertexIndexCount, sizeof(std::uint32_t), bytes) &&
                                  AccumulateStorage(header.polygonAdjacencyCount, sizeof(std::uint32_t), bytes) &&
                                  AccumulateStorage(header.offMeshLinkCount, sizeof(NavMeshOffMeshLink), bytes) &&
                                  AccumulateStorage(header.provenanceCount, sizeof(NavMeshSourceProvenance), bytes) &&
                                  AccumulateStorage(header.providerPayloadCount, sizeof(NavMeshProviderPayloadDescriptor), bytes) &&
                                  CheckedAdd(bytes, header.providerEncodedBytes, bytes);
            if (!measured || bytes > limits.maxOwnedBytes)
                return Failure<void>(NavigationErrors::NavMeshArtifactCapacityExceeded);
            return Result<void>::Success();
        }

        [[nodiscard]] Result<void> ValidateHeaderCounts(const NavMeshArtifactHeader &header, const NavMeshArtifactLimits &limits) {
            if (header.tileCount == 0 || header.vertexCount == 0 || header.polygonCount == 0 || header.polygonVertexIndexCount == 0 ||
                header.provenanceCount == 0 || header.portableEncodedBytes == 0 || header.portableDecodedBytes == 0)
                return Failure<void>(NavigationErrors::NavMeshArtifactInvalid);
            if (header.tileCount > limits.maxTiles || header.vertexCount > limits.maxVertices || header.polygonCount > limits.maxPolygons ||
                header.polygonVertexIndexCount > limits.maxPolygonVertexIndices ||
                header.polygonAdjacencyCount > limits.maxPolygonAdjacencies || header.offMeshLinkCount > limits.maxOffMeshLinks ||
                header.provenanceCount > limits.maxProvenanceRows || header.providerPayloadCount > limits.maxProviderPayloads ||
                header.portableEncodedBytes > limits.maxPortableEncodedBytes ||
                header.portableDecodedBytes > limits.maxPortableDecodedBytes ||
                header.providerEncodedBytes > limits.maxProviderEncodedBytes ||
                header.providerDecodedBytes > limits.maxProviderDecodedBytes)
                return Failure<void>(NavigationErrors::NavMeshArtifactCapacityExceeded);
            if ((header.providerPayloadCount == 0) != (header.providerEncodedBytes == 0) ||
                (header.providerPayloadCount == 0) != (header.providerDecodedBytes == 0))
                return Failure<void>(NavigationErrors::NavMeshArtifactInvalid);
            return MeasureOwnedStorage(header, limits);
        }

        [[nodiscard]] Result<void> ValidateViewShape(const NavMeshArtifactView &artifact) {
            const auto &header = artifact.header;
            if (artifact.observedPayloadDigest != header.payloadDigest || artifact.tiles.size() != header.tileCount ||
                artifact.observedTilePayloadDigests.size() != header.tileCount || artifact.vertices.size() != header.vertexCount ||
                artifact.polygons.size() != header.polygonCount || artifact.polygonVertexIndices.size() != header.polygonVertexIndexCount ||
                artifact.polygonAdjacencies.size() != header.polygonAdjacencyCount ||
                artifact.offMeshLinks.size() != header.offMeshLinkCount || artifact.provenance.size() != header.provenanceCount ||
                artifact.providerPayloads.size() != header.providerPayloadCount ||
                artifact.providerPayloadBytes.size() != header.providerEncodedBytes)
                return Failure<void>(NavigationErrors::NavMeshArtifactCorrupt);
            return Result<void>::Success();
        }

        [[nodiscard]] Result<void> ValidatePolygon(const NavMeshArtifactView &artifact, const NavMeshTileDescriptor &tile,
                                                   const std::uint32_t polygonIndex, const NavMeshArtifactLimits &limits,
                                                   std::uint64_t &expectedVertexIndex, std::uint64_t &expectedAdjacency) {
            const auto &polygon = artifact.polygons[polygonIndex];
            if (!polygon.area.IsValid() || polygon.vertexIndices.count < 3 || polygon.vertexIndices.count > limits.maxVerticesPerPolygon ||
                polygon.vertexIndices.first != expectedVertexIndex || polygon.adjacencies.first != expectedAdjacency ||
                !FitsRange(polygon.vertexIndices, artifact.polygonVertexIndices.size()) ||
                !FitsRange(polygon.adjacencies, artifact.polygonAdjacencies.size()) ||
                RangeEnd(polygon.vertexIndices) > RangeEnd(tile.polygonVertexIndices) ||
                RangeEnd(polygon.adjacencies) > RangeEnd(tile.polygonAdjacencies))
                return Failure<void>(NavigationErrors::NavMeshArtifactCorrupt);

            const auto indices = Slice(artifact.polygonVertexIndices, polygon.vertexIndices);
            for (std::size_t index = 0; index < indices.size(); ++index) {
                if (!Contains(tile.vertices, indices[index]) ||
                    std::find(indices.begin(), indices.begin() + static_cast<std::ptrdiff_t>(index), indices[index]) !=
                        indices.begin() + static_cast<std::ptrdiff_t>(index))
                    return Failure<void>(NavigationErrors::NavMeshArtifactCorrupt);
            }
            for (const auto neighbor : Slice(artifact.polygonAdjacencies, polygon.adjacencies)) {
                if (neighbor >= artifact.polygons.size() || neighbor == polygonIndex)
                    return Failure<void>(NavigationErrors::NavMeshArtifactCorrupt);
            }
            expectedVertexIndex = RangeEnd(polygon.vertexIndices);
            expectedAdjacency = RangeEnd(polygon.adjacencies);
            return Result<void>::Success();
        }

        [[nodiscard]] Result<void> ValidatePolygons(const NavMeshArtifactView &artifact, const NavMeshTileDescriptor &tile,
                                                    const NavMeshArtifactLimits &limits) {
            std::uint64_t expectedVertexIndex = tile.polygonVertexIndices.first;
            std::uint64_t expectedAdjacency = tile.polygonAdjacencies.first;
            const auto end = RangeEnd(tile.polygons);
            for (std::uint64_t polygon = tile.polygons.first; polygon < end; ++polygon) {
                if (const auto valid = ValidatePolygon(artifact, tile, static_cast<std::uint32_t>(polygon), limits, expectedVertexIndex,
                                                       expectedAdjacency);
                    valid.HasError())
                    return valid;
            }
            if (expectedVertexIndex != RangeEnd(tile.polygonVertexIndices) || expectedAdjacency != RangeEnd(tile.polygonAdjacencies))
                return Failure<void>(NavigationErrors::NavMeshArtifactCorrupt);
            return Result<void>::Success();
        }

        [[nodiscard]] Result<void> ValidateLinks(const NavMeshArtifactView &artifact, const NavMeshTileDescriptor &tile) {
            for (const auto &link : Slice(artifact.offMeshLinks, tile.offMeshLinks)) {
                if (!Math::IsFinite(link.start) || !Math::IsFinite(link.end) || !std::isfinite(link.radiusMeters) ||
                    link.radiusMeters <= 0.0F || !link.area.IsValid() || !Contains(tile.polygons, link.startPolygon) ||
                    link.endPolygon >= artifact.polygons.size() || !IsInside(link.start, tile.bounds))
                    return Failure<void>(NavigationErrors::NavMeshArtifactCorrupt);
            }
            return Result<void>::Success();
        }

        [[nodiscard]] Result<void> ValidateProvenance(const NavMeshArtifactView &artifact, const NavMeshTileDescriptor &tile) {
            std::uint64_t expectedPolygon = tile.polygons.first;
            for (const auto &provenance : Slice(artifact.provenance, tile.provenance)) {
                if (!IsKnown(provenance.kind) || !provenance.producer.IsValid() || !provenance.contribution.IsValid() ||
                    !provenance.revision.IsValid() || !IsPresent(provenance.sourceDigest) || provenance.polygons.count == 0 ||
                    provenance.polygons.first != expectedPolygon || !FitsRange(provenance.polygons, artifact.polygons.size()) ||
                    RangeEnd(provenance.polygons) > RangeEnd(tile.polygons))
                    return Failure<void>(NavigationErrors::NavMeshArtifactCorrupt);
                expectedPolygon = RangeEnd(provenance.polygons);
            }
            if (expectedPolygon != RangeEnd(tile.polygons))
                return Failure<void>(NavigationErrors::NavMeshArtifactCorrupt);
            return Result<void>::Success();
        }

        [[nodiscard]] Result<void> ValidateProviderPayloads(const NavMeshArtifactView &artifact, const NavMeshTileDescriptor &tile,
                                                            const NavMeshArtifactLimits &limits) {
            const NavMeshProviderPayloadDescriptor *previous{};
            for (const auto &payload : Slice(artifact.providerPayloads, tile.providerPayloads)) {
                if (!IsPresent(payload.providerFingerprint) || !IsPresent(payload.payloadDigest) || payload.formatVersion == 0 ||
                    !IsKnown(payload.byteOrder) || !IsKnown(payload.compression))
                    return Failure<void>(NavigationErrors::NavMeshProviderPayloadIncompatible);
                if (payload.encodedBytes == 0 || payload.decodedBytes == 0 || payload.encodedBytes > limits.maxProviderEncodedBytes ||
                    payload.decodedBytes > limits.maxProviderDecodedBytes ||
                    (payload.compression == NavMeshCompression::None && payload.encodedBytes != payload.decodedBytes) ||
                    payload.byteOffset > artifact.providerPayloadBytes.size() ||
                    payload.encodedBytes > static_cast<std::uint64_t>(artifact.providerPayloadBytes.size()) - payload.byteOffset)
                    return Failure<void>(NavigationErrors::NavMeshArtifactCorrupt);
                if (previous != nullptr && previous->providerFingerprint >= payload.providerFingerprint)
                    return Failure<void>(NavigationErrors::NavMeshArtifactCorrupt);

                const auto bytes = artifact.providerPayloadBytes.subspan(static_cast<std::size_t>(payload.byteOffset),
                                                                         static_cast<std::size_t>(payload.encodedBytes));
                if (ComputeSha256(bytes) != payload.payloadDigest)
                    return Failure<void>(NavigationErrors::NavMeshArtifactCorrupt);
                previous = &payload;
            }
            return Result<void>::Success();
        }

        struct TableCursors final {
            std::uint64_t vertices{};
            std::uint64_t polygons{};
            std::uint64_t polygonVertexIndices{};
            std::uint64_t polygonAdjacencies{};
            std::uint64_t offMeshLinks{};
            std::uint64_t provenance{};
            std::uint64_t providerPayloads{};
            std::uint64_t providerBytes{};
            std::uint64_t providerDecodedBytes{};
        };

        [[nodiscard]] bool Advance(const NavMeshTableRange range, std::uint64_t &cursor) noexcept {
            if (range.first != cursor)
                return false;
            cursor = RangeEnd(range);
            return true;
        }

        [[nodiscard]] Result<void> AdvanceTileRanges(const NavMeshArtifactView &artifact, const NavMeshTileDescriptor &tile,
                                                     TableCursors &cursors) {
            if (!Advance(tile.vertices, cursors.vertices) || !Advance(tile.polygons, cursors.polygons) ||
                !Advance(tile.polygonVertexIndices, cursors.polygonVertexIndices) ||
                !Advance(tile.polygonAdjacencies, cursors.polygonAdjacencies) || !Advance(tile.offMeshLinks, cursors.offMeshLinks) ||
                !Advance(tile.provenance, cursors.provenance) || !Advance(tile.providerPayloads, cursors.providerPayloads))
                return Failure<void>(NavigationErrors::NavMeshArtifactCorrupt);

            for (const auto &payload : Slice(artifact.providerPayloads, tile.providerPayloads)) {
                if (payload.byteOffset != cursors.providerBytes ||
                    !CheckedAdd(cursors.providerBytes, payload.encodedBytes, cursors.providerBytes) ||
                    !CheckedAdd(cursors.providerDecodedBytes, payload.decodedBytes, cursors.providerDecodedBytes))
                    return Failure<void>(NavigationErrors::NavMeshArtifactCorrupt);
            }
            return Result<void>::Success();
        }

        [[nodiscard]] Result<void> ValidatePartitions(const NavMeshArtifactView &artifact, const NavMeshArtifactLimits &limits) {
            TableCursors cursors{};
            const NavMeshTileDescriptor *previous{};
            for (std::size_t index = 0; index < artifact.tiles.size(); ++index) {
                const auto &tile = artifact.tiles[index];
                if (previous != nullptr && previous->key >= tile.key)
                    return Failure<void>(NavigationErrors::NavMeshArtifactCorrupt);
                if (const auto advanced = AdvanceTileRanges(artifact, tile, cursors); advanced.HasError())
                    return advanced;
                if (const auto valid = ValidateNavMeshTile(artifact, index, limits); valid.HasError())
                    return valid;
                previous = &tile;
            }

            const auto &header = artifact.header;
            if (cursors.vertices != header.vertexCount || cursors.polygons != header.polygonCount ||
                cursors.polygonVertexIndices != header.polygonVertexIndexCount ||
                cursors.polygonAdjacencies != header.polygonAdjacencyCount || cursors.offMeshLinks != header.offMeshLinkCount ||
                cursors.provenance != header.provenanceCount || cursors.providerPayloads != header.providerPayloadCount ||
                cursors.providerBytes != header.providerEncodedBytes || cursors.providerDecodedBytes != header.providerDecodedBytes)
                return Failure<void>(NavigationErrors::NavMeshArtifactCorrupt);
            return Result<void>::Success();
        }

        template <typename T> [[nodiscard]] std::vector<T> Own(const std::span<const T> values) {
            return {values.begin(), values.end()};
        }
    }  // namespace

    /** @copydoc NavMeshProfileDescriptor::operator== */
    bool NavMeshProfileDescriptor::operator==(const NavMeshProfileDescriptor &other) const noexcept {
        return id == other.id && buildGeometry.radiusMeters == other.buildGeometry.radiusMeters &&
               buildGeometry.heightMeters == other.buildGeometry.heightMeters &&
               buildGeometry.maxSlopeDegrees == other.buildGeometry.maxSlopeDegrees &&
               buildGeometry.stepHeightMeters == other.buildGeometry.stepHeightMeters &&
               buildGeometry.cellSizeMeters == other.buildGeometry.cellSizeMeters &&
               buildGeometry.cellHeightMeters == other.buildGeometry.cellHeightMeters &&
               buildGeometry.minimumRegionSizeMeters == other.buildGeometry.minimumRegionSizeMeters && contentDigest == other.contentDigest;
    }

    /** @copydoc ValidateNavMeshArtifactHeader */
    Result<void> ValidateNavMeshArtifactHeader(const NavMeshArtifactHeader &header, const NavMeshArtifactLimits &limits) {
        if (!IsValidLimits(limits) || !header.profile.id.IsValid() || !IsValid(header.profile.buildGeometry) ||
            !IsPresent(header.profile.contentDigest) || !IsPresent(header.payloadDigest) ||
            !std::isfinite(header.coordinateFrame.tileSizeMeters) || header.coordinateFrame.tileSizeMeters <= 0.0F)
            return Failure<void>(NavigationErrors::NavMeshArtifactInvalid);
        if (header.formatVersion.major != CurrentNavMeshFormatVersion.major ||
            header.formatVersion.minor > CurrentNavMeshFormatVersion.minor || !IsKnown(header.byteOrder) ||
            header.byteOrder != NavMeshByteOrder::LittleEndian || !IsKnown(header.compression) ||
            header.coordinateFrame.representation != NavMeshCoordinateFrame::RightHandedYUpMeters)
            return Failure<void>(NavigationErrors::UnsupportedCookedVersion);
        if (header.compression == NavMeshCompression::None && header.portableEncodedBytes != header.portableDecodedBytes)
            return Failure<void>(NavigationErrors::NavMeshArtifactInvalid);
        return ValidateHeaderCounts(header, limits);
    }

    /** @copydoc ValidateNavMeshTile */
    Result<void> ValidateNavMeshTile(const NavMeshArtifactView &artifact, const std::size_t tileIndex,
                                     const NavMeshArtifactLimits &limits) {
        if (const auto header = ValidateNavMeshArtifactHeader(artifact.header, limits); header.HasError())
            return header;
        if (tileIndex >= artifact.tiles.size() || tileIndex >= artifact.observedTilePayloadDigests.size())
            return Failure<void>(NavigationErrors::NavMeshTileUnknown);

        const auto &tile = artifact.tiles[tileIndex];
        if (!tile.bounds.IsValid() || !MatchesTileGrid(artifact.header, tile) || tile.vertices.count == 0 || tile.polygons.count == 0 ||
            tile.polygonVertexIndices.count == 0 || tile.provenance.count == 0 || tile.vertices.count > limits.maxVertices ||
            tile.polygons.count > limits.maxPolygons || tile.polygonVertexIndices.count > limits.maxPolygonVertexIndices ||
            tile.polygonAdjacencies.count > limits.maxPolygonAdjacencies || tile.offMeshLinks.count > limits.maxOffMeshLinks ||
            tile.provenance.count > limits.maxProvenanceRows || tile.providerPayloads.count > limits.maxProviderPayloads ||
            !FitsRange(tile.vertices, artifact.vertices.size()) || !FitsRange(tile.polygons, artifact.polygons.size()) ||
            !FitsRange(tile.polygonVertexIndices, artifact.polygonVertexIndices.size()) ||
            !FitsRange(tile.polygonAdjacencies, artifact.polygonAdjacencies.size()) ||
            !FitsRange(tile.offMeshLinks, artifact.offMeshLinks.size()) || !FitsRange(tile.provenance, artifact.provenance.size()) ||
            !FitsRange(tile.providerPayloads, artifact.providerPayloads.size()) || !IsPresent(tile.payloadDigest) ||
            tile.payloadDigest != artifact.observedTilePayloadDigests[tileIndex])
            return Failure<void>(NavigationErrors::NavMeshArtifactCorrupt);

        for (const auto vertex : Slice(artifact.vertices, tile.vertices)) {
            if (!Math::IsFinite(vertex) || !IsInside(vertex, tile.bounds))
                return Failure<void>(NavigationErrors::NavMeshArtifactCorrupt);
        }
        if (const auto polygons = ValidatePolygons(artifact, tile, limits); polygons.HasError())
            return polygons;
        if (const auto links = ValidateLinks(artifact, tile); links.HasError())
            return links;
        if (const auto provenance = ValidateProvenance(artifact, tile); provenance.HasError())
            return provenance;
        return ValidateProviderPayloads(artifact, tile, limits);
    }

    /** @copydoc NavMeshData::Create */
    Result<NavMeshData> NavMeshData::Create(const NavMeshArtifactView &artifact, const NavMeshArtifactLimits &limits) {
        if (const auto header = ValidateNavMeshArtifactHeader(artifact.header, limits); header.HasError())
            return Result<NavMeshData>::Failure(header.ErrorValue());
        if (const auto shape = ValidateViewShape(artifact); shape.HasError())
            return Result<NavMeshData>::Failure(shape.ErrorValue());
        if (const auto partitions = ValidatePartitions(artifact, limits); partitions.HasError())
            return Result<NavMeshData>::Failure(partitions.ErrorValue());

        return Result<NavMeshData>::Success(
            NavMeshData{artifact.header, Own(artifact.tiles), Own(artifact.vertices), Own(artifact.polygons),
                        Own(artifact.polygonVertexIndices), Own(artifact.polygonAdjacencies), Own(artifact.offMeshLinks),
                        Own(artifact.provenance), Own(artifact.providerPayloads), Own(artifact.providerPayloadBytes)});
    }

    /** @copydoc NavMeshData::Header */
    const NavMeshArtifactHeader &NavMeshData::Header() const noexcept {
        return header_;
    }

    /** @copydoc NavMeshData::Tiles */
    std::span<const NavMeshTileDescriptor> NavMeshData::Tiles() const noexcept {
        return tiles_;
    }

    /** @copydoc NavMeshData::ResolveTile */
    Result<NavMeshTileView> NavMeshData::ResolveTile(const NavMeshTileKey key) const {
        const auto found = std::ranges::lower_bound(tiles_, key, {}, &NavMeshTileDescriptor::key);
        if (found == tiles_.end() || found->key != key)
            return Failure<NavMeshTileView>(NavigationErrors::NavMeshTileUnknown);
        return Result<NavMeshTileView>::Success({
            .descriptor = &*found,
            .vertices = Slice(std::span<const Math::Vec3>{vertices_}, found->vertices),
            .polygons = Slice(std::span<const NavMeshPolygon>{polygons_}, found->polygons),
            .polygonVertexIndices = Slice(std::span<const std::uint32_t>{polygonVertexIndices_}, found->polygonVertexIndices),
            .polygonAdjacencies = Slice(std::span<const std::uint32_t>{polygonAdjacencies_}, found->polygonAdjacencies),
            .offMeshLinks = Slice(std::span<const NavMeshOffMeshLink>{offMeshLinks_}, found->offMeshLinks),
            .provenance = Slice(std::span<const NavMeshSourceProvenance>{provenance_}, found->provenance),
            .providerPayloads = Slice(std::span<const NavMeshProviderPayloadDescriptor>{providerPayloads_}, found->providerPayloads),
        });
    }

    /** @copydoc NavMeshData::ResolveProviderPayload */
    Result<NavMeshProviderPayloadView> NavMeshData::ResolveProviderPayload(const NavMeshTileKey tile,
                                                                           const NavMeshProviderPayloadCompatibility &compatibility) const {
        if (!IsPresent(compatibility.providerFingerprint) || compatibility.formatVersion == 0 || !IsKnown(compatibility.byteOrder) ||
            !IsKnown(compatibility.compression))
            return Failure<NavMeshProviderPayloadView>(NavigationErrors::NavMeshArtifactInvalid);
        const auto resolved = ResolveTile(tile);
        if (resolved.HasError())
            return Result<NavMeshProviderPayloadView>::Failure(resolved.ErrorValue());

        for (const auto &payload : resolved.Value().providerPayloads) {
            if (payload.providerFingerprint != compatibility.providerFingerprint)
                continue;
            if (payload.formatVersion != compatibility.formatVersion || payload.byteOrder != compatibility.byteOrder ||
                payload.compression != compatibility.compression)
                return Failure<NavMeshProviderPayloadView>(NavigationErrors::NavMeshProviderPayloadIncompatible);
            return Result<NavMeshProviderPayloadView>::Success({
                .descriptor = &payload,
                .bytes = std::span<const std::byte>{providerPayloadBytes_}.subspan(static_cast<std::size_t>(payload.byteOffset),
                                                                                   static_cast<std::size_t>(payload.encodedBytes)),
            });
        }
        return Failure<NavMeshProviderPayloadView>(NavigationErrors::NavMeshProviderPayloadUnavailable);
    }

    NavMeshData::NavMeshData(NavMeshArtifactHeader header, std::vector<NavMeshTileDescriptor> tiles, std::vector<Math::Vec3> vertices,
                             std::vector<NavMeshPolygon> polygons, std::vector<std::uint32_t> polygonVertexIndices,
                             std::vector<std::uint32_t> polygonAdjacencies, std::vector<NavMeshOffMeshLink> offMeshLinks,
                             std::vector<NavMeshSourceProvenance> provenance,
                             std::vector<NavMeshProviderPayloadDescriptor> providerPayloads,
                             std::vector<std::byte> providerPayloadBytes) noexcept
        : header_(std::move(header)), tiles_(std::move(tiles)), vertices_(std::move(vertices)), polygons_(std::move(polygons)),
          polygonVertexIndices_(std::move(polygonVertexIndices)), polygonAdjacencies_(std::move(polygonAdjacencies)),
          offMeshLinks_(std::move(offMeshLinks)), provenance_(std::move(provenance)), providerPayloads_(std::move(providerPayloads)),
          providerPayloadBytes_(std::move(providerPayloadBytes)) {}
}  // namespace Horo::Navigation
