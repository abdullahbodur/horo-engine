#include "Horo/Navigation/Backends/RecastDetourProvider.h"

#include "Horo/Navigation/NavigationErrors.h"
#include "runtime/navigation/backends/recast_detour/RecastDetourProviderValidation.h"

#include <DetourAlloc.h>
#include <DetourNavMesh.h>
#include <DetourNavMeshBuilder.h>
#include <DetourNavMeshQuery.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <memory>
#include <new>
#include <tuple>
#include <utility>
#include <vector>

namespace Horo::Navigation {
    namespace {
        constexpr std::uint16_t NullPolygonIndex = 0xffffU;
        constexpr std::uint16_t TraversablePolygonFlag = 1U;
        constexpr std::size_t MaximumVerticesPerPolygon = 6;

        template <typename T> [[nodiscard]] Result<T> Failure(const ErrorCodeDescriptor &descriptor) {
            return Result<T>::Failure(MakeError(descriptor));
        }

        struct NavMeshDeleter final {
            void operator()(dtNavMesh *mesh) const noexcept {
                dtFreeNavMesh(mesh);
            }
        };

        struct QueryDeleter final {
            void operator()(dtNavMeshQuery *query) const noexcept {
                dtFreeNavMeshQuery(query);
            }
        };

        struct TileDataDeleter final {
            void operator()(unsigned char *data) const noexcept {
                dtFree(data);
            }
        };

        using NavMeshPtr = std::unique_ptr<dtNavMesh, NavMeshDeleter>;
        using QueryPtr = std::unique_ptr<dtNavMeshQuery, QueryDeleter>;
        using TileDataPtr = std::unique_ptr<unsigned char, TileDataDeleter>;

        struct NativeTopologyInput final {
            std::vector<unsigned short> vertices;
            std::vector<unsigned short> polygons;
            std::vector<unsigned short> polygonFlags;
            std::vector<unsigned char> polygonAreas;
            Math::Vec3 minimum;
            Math::Vec3 maximum;
        };

        struct PolygonEdge final {
            std::uint32_t first{};
            std::uint32_t second{};
            std::uint32_t polygon{};
            std::uint8_t edge{};
            bool ascending{};
        };

        struct QuerySlot final {
            QuerySlot() = default;

            ~QuerySlot() {
                leased.store(false);
            }

            QuerySlot(const QuerySlot &) = delete;
            QuerySlot &operator=(const QuerySlot &) = delete;

            QuerySlot(QuerySlot &&other) noexcept
                : query(std::move(other.query)), polygonPath(std::move(other.polygonPath)), straightPoints(std::move(other.straightPoints)),
                  straightFlags(std::move(other.straightFlags)), straightPolygons(std::move(other.straightPolygons)),
                  leased(other.leased.load()) {
                other.leased.store(false);
            }

            QuerySlot &operator=(QuerySlot &&) = delete;

            QueryPtr query;
            std::vector<dtPolyRef> polygonPath;
            std::vector<float> straightPoints;
            std::vector<unsigned char> straightFlags;
            std::vector<dtPolyRef> straightPolygons;
            std::atomic<bool> leased{false};
        };

        class QueryLease final {
        public:
            explicit QueryLease(QuerySlot *slot) noexcept : slot_(slot) {}

            QueryLease(const QueryLease &) = delete;
            QueryLease &operator=(const QueryLease &) = delete;
            QueryLease(QueryLease &&) = delete;
            QueryLease &operator=(QueryLease &&) = delete;

            ~QueryLease() {
                if (slot_ != nullptr)
                    slot_->leased.store(false);
            }

            [[nodiscard]] QuerySlot *Get() const noexcept {
                return slot_;
            }

        private:
            QuerySlot *slot_{};
        };

        [[nodiscard]] Result<void> ValidateCreateInfo(const RecastDetourProviderCreateInfo &info) {
            if (!Detail::HasValidIdentityAndTopologyBounds(info))
                return Failure<void>(NavigationErrors::CapabilityDescriptorInvalid);
            if (!Detail::HasValidAgentSettings(info))
                return Failure<void>(NavigationErrors::CapabilityDescriptorInvalid);
            if (!Detail::HasValidQuerySettings(info))
                return Failure<void>(NavigationErrors::CapabilityDescriptorInvalid);
            if (!Detail::HasValidMemoryBudget(info))
                return Failure<void>(NavigationErrors::CapacityExceeded);
            if (!std::ranges::all_of(info.vertices, [](const Math::Vec3 vertex) {
                return Math::IsFinite(vertex);
            }))
                return Failure<void>(NavigationErrors::ProviderFailed);
            return Result<void>::Success();
        }

        [[nodiscard]] bool IsCounterClockwiseConvex(const GroundedNavigationPolygon &polygon,
                                                    const std::span<const Math::Vec3> vertices) noexcept {
            for (std::uint8_t corner = 0; corner < polygon.vertexCount; ++corner) {
                const Math::Vec3 &first = vertices[polygon.vertexIndices[corner]];
                const Math::Vec3 &second = vertices[polygon.vertexIndices[(corner + 1U) % polygon.vertexCount]];
                const Math::Vec3 &third = vertices[polygon.vertexIndices[(corner + 2U) % polygon.vertexCount]];
                if (const double cross = ((static_cast<double>(second.x) - first.x) * (static_cast<double>(third.z) - second.z)) -
                                         ((static_cast<double>(second.z) - first.z) * (static_cast<double>(third.x) - second.x));
                    !std::isfinite(cross) || cross <= std::numeric_limits<double>::epsilon())
                    return false;
            }
            return true;
        }

        [[nodiscard]] Result<void> AppendValidatedPolygonEdges(const RecastDetourProviderCreateInfo &info, const std::size_t polygonIndex,
                                                               std::vector<PolygonEdge> &edges) {
            const GroundedNavigationPolygon &polygon = info.polygons[polygonIndex];
            if (polygon.vertexCount < 3 || polygon.vertexCount > MaximumVerticesPerPolygon || !polygon.area.IsValid())
                return Failure<void>(NavigationErrors::ProviderFailed);
            if (!std::ranges::all_of(polygon.vertexIndices.begin() + polygon.vertexCount, polygon.vertexIndices.end(),
                                     [](const std::uint32_t index) {
                return index == 0;
            }))
                return Failure<void>(NavigationErrors::ProviderFailed);

            for (std::uint8_t edgeIndex = 0; edgeIndex < polygon.vertexCount; ++edgeIndex) {
                const std::uint32_t first = polygon.vertexIndices[edgeIndex];
                const std::uint32_t second = polygon.vertexIndices[(edgeIndex + 1U) % polygon.vertexCount];
                if (const auto priorEnd = polygon.vertexIndices.begin() + edgeIndex;
                    first >= info.vertices.size() || second >= info.vertices.size() || first == second ||
                    std::find(polygon.vertexIndices.begin(), priorEnd, first) != priorEnd)
                    return Failure<void>(NavigationErrors::ProviderFailed);
                edges.push_back({.first = std::min(first, second),
                                 .second = std::max(first, second),
                                 .polygon = static_cast<std::uint32_t>(polygonIndex),
                                 .edge = edgeIndex,
                                 .ascending = first < second});
            }
            if (!IsCounterClockwiseConvex(polygon, info.vertices))
                return Failure<void>(NavigationErrors::ProviderFailed);
            return Result<void>::Success();
        }

        [[nodiscard]] Result<std::vector<PolygonEdge>> ValidatePolygons(const RecastDetourProviderCreateInfo &info) {
            std::vector<PolygonEdge> edges;
            edges.reserve(info.polygons.size() * MaximumVerticesPerPolygon);
            for (std::size_t polygonIndex = 0; polygonIndex < info.polygons.size(); ++polygonIndex) {
                if (auto appended = AppendValidatedPolygonEdges(info, polygonIndex, edges); appended.HasError())
                    return Result<std::vector<PolygonEdge>>::Failure(appended.ErrorValue());
            }
            std::ranges::sort(edges, [](const PolygonEdge &left, const PolygonEdge &right) {
                return std::tuple{left.first, left.second, left.polygon, left.edge, static_cast<std::uint8_t>(left.ascending)} <
                       std::tuple{right.first, right.second, right.polygon, right.edge, static_cast<std::uint8_t>(right.ascending)};
            });
            for (std::size_t index = 0; index < edges.size();) {
                std::size_t end = index + 1U;
                while (end < edges.size() && edges[end].first == edges[index].first && edges[end].second == edges[index].second)
                    ++end;
                if (end - index > 2U)
                    return Failure<std::vector<PolygonEdge>>(NavigationErrors::ProviderFailed);
                if (end - index == 2U && edges[index].ascending == edges[index + 1U].ascending)
                    return Failure<std::vector<PolygonEdge>>(NavigationErrors::ProviderFailed);
                index = end;
            }
            return Result<std::vector<PolygonEdge>>::Success(std::move(edges));
        }

        [[nodiscard]] Result<void> TranslateVertices(const RecastDetourProviderCreateInfo &info, NativeTopologyInput &translated) {
            translated.minimum = info.vertices.front();
            translated.maximum = info.vertices.front();
            for (const Math::Vec3 vertex : info.vertices) {
                translated.minimum.x = std::min(translated.minimum.x, vertex.x);
                translated.minimum.y = std::min(translated.minimum.y, vertex.y);
                translated.minimum.z = std::min(translated.minimum.z, vertex.z);
                translated.maximum.x = std::max(translated.maximum.x, vertex.x);
                translated.maximum.y = std::max(translated.maximum.y, vertex.y);
                translated.maximum.z = std::max(translated.maximum.z, vertex.z);
            }

            translated.vertices.resize(info.vertices.size() * 3U);
            for (std::size_t index = 0; index < info.vertices.size(); ++index) {
                const Math::Vec3 relative = info.vertices[index] - translated.minimum;
                const std::array<double, 3> quantized{std::round(relative.x / info.cellSizeMeters),
                                                      std::round(relative.y / info.cellHeightMeters),
                                                      std::round(relative.z / info.cellSizeMeters)};
                if (std::ranges::any_of(quantized, [](const double value) {
                    return value < 0.0 || value > static_cast<double>(std::numeric_limits<unsigned short>::max());
                }))
                    return Failure<void>(NavigationErrors::ProviderFailed);
                translated.vertices[(index * 3U) + 0U] = static_cast<unsigned short>(quantized[0]);
                translated.vertices[(index * 3U) + 1U] = static_cast<unsigned short>(quantized[1]);
                translated.vertices[(index * 3U) + 2U] = static_cast<unsigned short>(quantized[2]);
            }
            return Result<void>::Success();
        }

        [[nodiscard]] Result<void> TranslatePolygonData(const RecastDetourProviderCreateInfo &info, NativeTopologyInput &translated) {
            translated.polygons.assign(info.polygons.size() * MaximumVerticesPerPolygon * 2U, NullPolygonIndex);
            translated.polygonFlags.assign(info.polygons.size(), TraversablePolygonFlag);
            translated.polygonAreas.resize(info.polygons.size());
            std::vector<std::uint64_t> areas;
            areas.reserve(info.polygons.size());
            for (const GroundedNavigationPolygon &polygon : info.polygons)
                areas.push_back(polygon.area.Value());
            std::ranges::sort(areas);
            areas.erase(std::ranges::unique(areas).begin(), areas.end());
            if (areas.size() > 64U)
                return Failure<void>(NavigationErrors::ProviderFailed);
            for (std::size_t polygonIndex = 0; polygonIndex < info.polygons.size(); ++polygonIndex) {
                const GroundedNavigationPolygon &polygon = info.polygons[polygonIndex];
                const std::size_t offset = polygonIndex * MaximumVerticesPerPolygon * 2U;
                for (std::uint8_t vertexIndex = 0; vertexIndex < polygon.vertexCount; ++vertexIndex)
                    translated.polygons[offset + vertexIndex] = static_cast<unsigned short>(polygon.vertexIndices[vertexIndex]);
                translated.polygonAreas[polygonIndex] =
                    static_cast<unsigned char>(std::ranges::lower_bound(areas, polygon.area.Value()) - areas.begin());
            }
            return Result<void>::Success();
        }

        void LinkPolygonNeighbors(const std::vector<PolygonEdge> &edges, NativeTopologyInput &translated) {
            std::size_t index{};
            while (index + 1U < edges.size()) {
                const PolygonEdge &first = edges[index];
                const PolygonEdge &second = edges[index + 1U];
                if (first.first != second.first || first.second != second.second) {
                    ++index;
                    continue;
                }
                const std::size_t firstOffset = (first.polygon * MaximumVerticesPerPolygon * 2U) + MaximumVerticesPerPolygon + first.edge;
                const std::size_t secondOffset =
                    (second.polygon * MaximumVerticesPerPolygon * 2U) + MaximumVerticesPerPolygon + second.edge;
                translated.polygons[firstOffset] = static_cast<unsigned short>(second.polygon);
                translated.polygons[secondOffset] = static_cast<unsigned short>(first.polygon);
                index += 2U;
            }
        }

        [[nodiscard]] Result<NativeTopologyInput> TranslateTopology(const RecastDetourProviderCreateInfo &info) {
            auto validatedEdges = ValidatePolygons(info);
            if (validatedEdges.HasError())
                return Result<NativeTopologyInput>::Failure(validatedEdges.ErrorValue());

            NativeTopologyInput translated;
            if (auto vertices = TranslateVertices(info, translated); vertices.HasError())
                return Result<NativeTopologyInput>::Failure(vertices.ErrorValue());
            if (auto polygons = TranslatePolygonData(info, translated); polygons.HasError())
                return Result<NativeTopologyInput>::Failure(polygons.ErrorValue());
            LinkPolygonNeighbors(validatedEdges.Value(), translated);
            return Result<NativeTopologyInput>::Success(std::move(translated));
        }

        [[nodiscard]] Result<NavMeshPtr> BuildNavMesh(const RecastDetourProviderCreateInfo &info, const NativeTopologyInput &input) {
            dtNavMeshCreateParams parameters{};
            parameters.verts = input.vertices.data();
            parameters.vertCount = static_cast<int>(info.vertices.size());
            parameters.polys = input.polygons.data();
            parameters.polyFlags = input.polygonFlags.data();
            parameters.polyAreas = input.polygonAreas.data();
            parameters.polyCount = static_cast<int>(info.polygons.size());
            parameters.nvp = static_cast<int>(MaximumVerticesPerPolygon);
            parameters.walkableHeight = info.walkableHeightMeters;
            parameters.walkableRadius = info.walkableRadiusMeters;
            parameters.walkableClimb = info.walkableClimbMeters;
            parameters.cs = info.cellSizeMeters;
            parameters.ch = info.cellHeightMeters;
            parameters.buildBvTree = true;
            const std::array<float, 3> minimum{input.minimum.x, input.minimum.y, input.minimum.z};
            const std::array<float, 3> maximum{input.maximum.x, input.maximum.y, input.maximum.z};
            std::ranges::copy(minimum, parameters.bmin);
            std::ranges::copy(maximum, parameters.bmax);

            unsigned char *rawTileData{};
            int tileDataSize{};
            if (!dtCreateNavMeshData(&parameters, &rawTileData, &tileDataSize) || rawTileData == nullptr || tileDataSize <= 0)
                return Failure<NavMeshPtr>(NavigationErrors::ProviderFailed);
            TileDataPtr tileData{rawTileData};
            if (static_cast<std::size_t>(tileDataSize) > info.maximumOwnedBytes)
                return Failure<NavMeshPtr>(NavigationErrors::CapacityExceeded);

            NavMeshPtr mesh{dtAllocNavMesh()};
            if (!mesh)
                return Failure<NavMeshPtr>(NavigationErrors::CapacityExceeded);
            if (const dtStatus initialized = mesh->init(tileData.get(), tileDataSize, DT_TILE_FREE_DATA); dtStatusFailed(initialized))
                return Failure<NavMeshPtr>(dtStatusDetail(initialized, DT_OUT_OF_MEMORY) ? NavigationErrors::CapacityExceeded
                                                                                         : NavigationErrors::ProviderFailed);
            if (tileData.release() != rawTileData)
                return Failure<NavMeshPtr>(NavigationErrors::ProviderFailed);
            return Result<NavMeshPtr>::Success(std::move(mesh));
        }

        [[nodiscard]] Result<std::vector<QuerySlot>> BuildQuerySlots(const RecastDetourProviderCreateInfo &info, const dtNavMesh &mesh) {
            std::vector<QuerySlot> slots;
            slots.reserve(info.maximumConcurrentQueries);
            for (std::uint32_t index = 0; index < info.maximumConcurrentQueries; ++index) {
                slots.emplace_back();
                QuerySlot &slot = slots.back();
                slot.query.reset(dtAllocNavMeshQuery());
                if (!slot.query || dtStatusFailed(slot.query->init(&mesh, static_cast<int>(info.maximumQueryNodes))))
                    return Failure<std::vector<QuerySlot>>(NavigationErrors::CapacityExceeded);
                slot.polygonPath.resize(info.maximumQueryNodes);
                slot.straightPoints.resize(static_cast<std::size_t>(info.maximumResultPoints) * 3U);
                slot.straightFlags.resize(info.maximumResultPoints);
                slot.straightPolygons.resize(info.maximumResultPoints);
            }
            return Result<std::vector<QuerySlot>>::Success(std::move(slots));
        }

        [[nodiscard]] QuerySlot *TryLease(std::vector<QuerySlot> &slots) noexcept {
            for (QuerySlot &slot : slots) {
                bool expected = false;
                if (slot.leased.compare_exchange_strong(expected, true))
                    return &slot;
            }
            return nullptr;
        }

        [[nodiscard]] Result<void> ValidateRequest(const NavigationPathRequest &request, const NavigationWorldId world,
                                                   const NavigationGeneration topology,
                                                   const NavigationProviderCapabilities &capabilities) {
            if (!request.world.IsValid() || request.world != world)
                return Failure<void>(NavigationErrors::InvalidWorld);
            if (!request.topology.IsValid() || request.topology != topology)
                return Failure<void>(NavigationErrors::StaleSnapshot);
            if (!Math::IsFinite(request.start) || !Math::IsFinite(request.destination))
                return Failure<void>(NavigationErrors::CapabilityDescriptorInvalid);
            if (const auto admitted = AdmitNavigationQuery(capabilities, capabilities.revision, request.requirement); admitted.HasError())
                return admitted;
            if (const double distance = std::hypot(static_cast<double>(request.destination.x) - request.start.x,
                                                   static_cast<double>(request.destination.y) - request.start.y,
                                                   static_cast<double>(request.destination.z) - request.start.z);
                !std::isfinite(distance) || distance > request.requirement.limits.maximumSearchDistanceMeters)
                return Failure<void>(NavigationErrors::QueryLimitExceeded);
            return Result<void>::Success();
        }

        [[nodiscard]] Result<float> PathLength(const std::vector<Math::Vec3> &points) {
            double length{};
            for (std::size_t index = 1; index < points.size(); ++index) {
                const Math::Vec3 &current = points[index];
                const Math::Vec3 &previous = points[index - 1U];
                length += std::hypot(static_cast<double>(current.x) - previous.x, static_cast<double>(current.y) - previous.y,
                                     static_cast<double>(current.z) - previous.z);
            }
            if (!std::isfinite(length) || length > std::numeric_limits<float>::max())
                return Failure<float>(NavigationErrors::ProviderFailed);
            return Result<float>::Success(static_cast<float>(length));
        }

        struct QueryEndpoints final {
            dtPolyRef startPolygon{};
            dtPolyRef destinationPolygon{};
            std::array<float, 3> start{};
            std::array<float, 3> destination{};
        };

        [[nodiscard]] Result<QueryEndpoints> ResolveEndpoints(const QuerySlot &slot, const NavigationPathRequest &request,
                                                              const Math::Vec3 halfExtents, const dtQueryFilter &filter) {
            const std::array<float, 3> start{request.start.x, request.start.y, request.start.z};
            const std::array<float, 3> destination{request.destination.x, request.destination.y, request.destination.z};
            const std::array<float, 3> extents{halfExtents.x, halfExtents.y, halfExtents.z};
            QueryEndpoints endpoints;
            if (const dtStatus status =
                    slot.query->findNearestPoly(start.data(), extents.data(), &filter, &endpoints.startPolygon, endpoints.start.data());
                dtStatusFailed(status))
                return Failure<QueryEndpoints>(NavigationErrors::ProviderFailed);
            if (const dtStatus status = slot.query->findNearestPoly(destination.data(), extents.data(), &filter,
                                                                    &endpoints.destinationPolygon, endpoints.destination.data());
                dtStatusFailed(status))
                return Failure<QueryEndpoints>(NavigationErrors::ProviderFailed);
            if (endpoints.startPolygon == 0 || endpoints.destinationPolygon == 0)
                return Failure<QueryEndpoints>(NavigationErrors::NoNavigationData);
            return Result<QueryEndpoints>::Success(endpoints);
        }

        [[nodiscard]] Result<int> FindCorridor(QuerySlot &slot, const QueryEndpoints &endpoints, const dtQueryFilter &filter,
                                               const std::uint32_t maximumNodes) {
            int polygonCount{};
            const auto scratchCapacity = static_cast<std::uint32_t>(slot.polygonPath.size());
            const auto boundedNodes = static_cast<int>(std::min(maximumNodes, scratchCapacity));
            if (const dtStatus status =
                    slot.query->findPath(endpoints.startPolygon, endpoints.destinationPolygon, endpoints.start.data(),
                                         endpoints.destination.data(), &filter, slot.polygonPath.data(), &polygonCount, boundedNodes);
                dtStatusFailed(status))
                return Failure<int>(dtStatusDetail(status, DT_OUT_OF_NODES) || dtStatusDetail(status, DT_BUFFER_TOO_SMALL)
                                        ? NavigationErrors::CapacityExceeded
                                        : NavigationErrors::ProviderFailed);
            if (polygonCount == 0 || slot.polygonPath[polygonCount - 1] != endpoints.destinationPolygon)
                return Failure<int>(NavigationErrors::NoNavigationData);
            return Result<int>::Success(polygonCount);
        }

        [[nodiscard]] Result<NavigationPath> BuildPath(QuerySlot &slot, const QueryEndpoints &endpoints,
                                                       const NavigationPathRequest &request, const int polygonCount) {
            int pointCount{};
            const auto scratchCapacity = static_cast<std::uint32_t>(slot.straightPoints.size() / 3U);
            const auto boundedPoints = static_cast<int>(std::min(request.requirement.limits.maximumResultPoints, scratchCapacity));
            if (const dtStatus status =
                    slot.query->findStraightPath(endpoints.start.data(), endpoints.destination.data(), slot.polygonPath.data(),
                                                 polygonCount, slot.straightPoints.data(), slot.straightFlags.data(),
                                                 slot.straightPolygons.data(), &pointCount, boundedPoints);
                dtStatusFailed(status) || dtStatusDetail(status, DT_BUFFER_TOO_SMALL))
                return Failure<NavigationPath>(dtStatusDetail(status, DT_BUFFER_TOO_SMALL) ? NavigationErrors::CapacityExceeded
                                                                                           : NavigationErrors::ProviderFailed);
            try {
                NavigationPath path;
                path.points.reserve(static_cast<std::size_t>(pointCount));
                for (int index = 0; index < pointCount; ++index) {
                    const std::size_t offset = static_cast<std::size_t>(index) * 3U;
                    path.points.push_back(
                        {slot.straightPoints[offset], slot.straightPoints[offset + 1U], slot.straightPoints[offset + 2U]});
                }
                if (path.points.empty())
                    return Failure<NavigationPath>(NavigationErrors::NoNavigationData);
                path.points.front() = request.start;
                path.points.back() = request.destination;
                auto length = PathLength(path.points);
                if (length.HasError())
                    return Result<NavigationPath>::Failure(length.ErrorValue());
                path.lengthMeters = length.Value();
                return Result<NavigationPath>::Success(std::move(path));
            } catch (const std::bad_alloc &) {
                return Failure<NavigationPath>(NavigationErrors::CapacityExceeded);
            }
        }

        class RecastDetourNavigationQueryBackend final : public INavigationQueryBackend {
        public:
            RecastDetourNavigationQueryBackend(const RecastDetourProviderCreateInfo &info, NavMeshPtr mesh,
                                               std::vector<QuerySlot> slots) noexcept
                : world_(info.world), topology_(info.topology), nearestPointHalfExtents_(info.nearestPointHalfExtents),
                  capabilities_(MakeAvailablePathQueryCapabilities(info.capabilityRevision,
                                                                   {.maximumNodeExpansions = info.maximumQueryNodes,
                                                                    .maximumResultPoints = info.maximumResultPoints,
                                                                    .maximumSearchDistanceMeters = info.maximumSearchDistanceMeters},
                                                                   info.maximumConcurrentQueries)),
                  mesh_(std::move(mesh)), slots_(std::move(slots)) {}

            [[nodiscard]] NavigationProviderCapabilities Capabilities() const noexcept override {
                return capabilities_;
            }

            [[nodiscard]] Result<NavigationPath> FindPath(const NavigationPathRequest &request,
                                                          const CancellationToken &cancellation) const override {
                if (const auto validated = ValidateRequest(request, world_, topology_, capabilities_); validated.HasError())
                    return Result<NavigationPath>::Failure(validated.ErrorValue());
                if (cancellation.IsCancellationRequested())
                    return Failure<NavigationPath>(NavigationErrors::QueryCancelled);
                QueryLease lease{TryLease(slots_)};
                if (lease.Get() == nullptr)
                    return Failure<NavigationPath>(NavigationErrors::AdmissionRejected);

                QuerySlot &slot = *lease.Get();
                dtQueryFilter filter;
                filter.setIncludeFlags(TraversablePolygonFlag);
                auto endpoints = ResolveEndpoints(slot, request, nearestPointHalfExtents_, filter);
                if (endpoints.HasError())
                    return Result<NavigationPath>::Failure(endpoints.ErrorValue());
                if (cancellation.IsCancellationRequested())
                    return Failure<NavigationPath>(NavigationErrors::QueryCancelled);

                auto corridor = FindCorridor(slot, endpoints.Value(), filter, request.requirement.limits.maximumNodeExpansions);
                if (corridor.HasError())
                    return Result<NavigationPath>::Failure(corridor.ErrorValue());
                if (cancellation.IsCancellationRequested())
                    return Failure<NavigationPath>(NavigationErrors::QueryCancelled);
                auto path = BuildPath(slot, endpoints.Value(), request, corridor.Value());
                if (path.HasError())
                    return path;
                if (cancellation.IsCancellationRequested())
                    return Failure<NavigationPath>(NavigationErrors::QueryCancelled);
                return path;
            }

        private:
            NavigationWorldId world_;
            NavigationGeneration topology_;
            Math::Vec3 nearestPointHalfExtents_;
            NavigationProviderCapabilities capabilities_;
            NavMeshPtr mesh_;
            mutable std::vector<QuerySlot> slots_;
        };
    }  // namespace

    /** @copydoc CreateRecastDetourNavigationQueryBackend */
    Result<std::unique_ptr<INavigationQueryBackend>> CreateRecastDetourNavigationQueryBackend(const RecastDetourProviderCreateInfo &info) {
        if (const auto validated = ValidateCreateInfo(info); validated.HasError())
            return Result<std::unique_ptr<INavigationQueryBackend>>::Failure(validated.ErrorValue());
        try {
            auto translated = TranslateTopology(info);
            if (translated.HasError())
                return Result<std::unique_ptr<INavigationQueryBackend>>::Failure(translated.ErrorValue());
            auto mesh = BuildNavMesh(info, translated.Value());
            if (mesh.HasError())
                return Result<std::unique_ptr<INavigationQueryBackend>>::Failure(mesh.ErrorValue());
            auto slots = BuildQuerySlots(info, *mesh.Value());
            if (slots.HasError())
                return Result<std::unique_ptr<INavigationQueryBackend>>::Failure(slots.ErrorValue());
            auto provider = std::make_unique<RecastDetourNavigationQueryBackend>(info, std::move(mesh).Value(), std::move(slots).Value());
            return Result<std::unique_ptr<INavigationQueryBackend>>::Success(std::move(provider));
        } catch (const std::bad_alloc &) {
            return Failure<std::unique_ptr<INavigationQueryBackend>>(NavigationErrors::CapacityExceeded);
        }
    }
}  // namespace Horo::Navigation
