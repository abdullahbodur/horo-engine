#include "Horo/Navigation/Backends/RecastDetourProvider.h"
#include "Horo/Navigation/NavigationErrors.h"
#include "navigation/NavigationTestAssertions.h"

#include <array>
#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <limits>
#include <thread>
#include <utility>
#include <vector>

namespace Horo::Navigation {
    namespace {
        using TestSupport::RequireError;

        struct SquareTopology final {
            std::array<Math::Vec3, 4> vertices{{{0.0F, 0.0F, 0.0F}, {10.0F, 0.0F, 0.0F}, {10.0F, 0.0F, 10.0F}, {0.0F, 0.0F, 10.0F}}};
            std::array<GroundedNavigationPolygon, 2> polygons{{
                {
                    .vertexIndices = {0, 1, 2, 0, 0, 0},
                    .vertexCount = 3,
                    .area = NavigationAreaId::Create(1).Value(),
                },
                {
                    .vertexIndices = {0, 2, 3, 0, 0, 0},
                    .vertexCount = 3,
                    .area = NavigationAreaId::Create(1).Value(),
                },
            }};
        };

        [[nodiscard]] RecastDetourProviderCreateInfo CreateInfo(const SquareTopology &topology) {
            return {
                .world = NavigationWorldId::Create(7).Value(),
                .topology = NavigationGeneration::Create(11).Value(),
                .vertices = topology.vertices,
                .polygons = topology.polygons,
                .maximumQueryNodes = 64,
                .maximumResultPoints = 16,
                .maximumConcurrentQueries = 2,
            };
        }

        [[nodiscard]] NavigationPathRequest Request(const RecastDetourProviderCreateInfo &info) {
            return {
                .world = info.world,
                .topology = info.topology,
                .start = {8.0F, 0.0F, 2.0F},
                .destination = {2.0F, 0.0F, 8.0F},
                .requirement =
                    {
                        .query = NavigationQueryKind::Path,
                        .quality = NavigationQualityLevel::Balanced,
                        .limits = {.maximumNodeExpansions = 32, .maximumResultPoints = 8, .maximumSearchDistanceMeters = 100.0F},
                    },
            };
        }
    }  // namespace

    TEST_CASE("Recast Detour provider translates neutral topology and path requests", "[unit][navigation][provider]") {
        const SquareTopology topology;
        const auto info = CreateInfo(topology);
        auto created = CreateRecastDetourNavigationQueryBackend(info);
        REQUIRE(created.HasValue());
        auto provider = std::move(created).Value();

        const auto capabilities = provider->Capabilities();
        REQUIRE(ValidateNavigationProviderCapabilities(capabilities));
        REQUIRE(QueryNavigationSupport(capabilities, NavigationQueryKind::Path, NavigationQualityLevel::Balanced) ==
                NavigationSupport::Available);
        const auto path = provider->FindPath(Request(info), {});
        REQUIRE(path.HasValue());
        REQUIRE(path.Value().points.size() >= 2);
        REQUIRE(path.Value().points.front() == Request(info).start);
        REQUIRE(path.Value().points.back() == Request(info).destination);
        REQUIRE(path.Value().lengthMeters > 0.0F);
    }

    TEST_CASE("Recast Detour provider rejects malformed topology without publishing partial state", "[unit][navigation][provider]") {
        SquareTopology topology;
        auto info = CreateInfo(topology);
        topology.polygons.front().vertexIndices[2] = 99;
        RequireError(CreateRecastDetourNavigationQueryBackend(info), NavigationErrors::ProviderFailed);

        SquareTopology nonFiniteTopology;
        nonFiniteTopology.vertices.front().x = std::numeric_limits<float>::quiet_NaN();
        info = CreateInfo(nonFiniteTopology);
        RequireError(CreateRecastDetourNavigationQueryBackend(info), NavigationErrors::ProviderFailed);

        const SquareTopology validTopology;
        info = CreateInfo(validTopology);
        info.maximumOwnedBytes = 1;
        RequireError(CreateRecastDetourNavigationQueryBackend(info), NavigationErrors::CapacityExceeded);

        info = CreateInfo(validTopology);
        auto recovered = CreateRecastDetourNavigationQueryBackend(info);
        REQUIRE(recovered.HasValue());
        REQUIRE(recovered.Value()->FindPath(Request(info), {}).HasValue());
    }

    TEST_CASE("Recast Detour provider fences cancellation world topology and request bounds", "[unit][navigation][provider]") {
        const SquareTopology topology;
        const auto info = CreateInfo(topology);
        auto created = CreateRecastDetourNavigationQueryBackend(info);
        REQUIRE(created.HasValue());
        auto provider = std::move(created).Value();

        CancellationSource cancellation;
        cancellation.RequestCancellation();
        RequireError(provider->FindPath(Request(info), cancellation.Token()), NavigationErrors::QueryCancelled);

        auto request = Request(info);
        request.world = NavigationWorldId::Create(8).Value();
        RequireError(provider->FindPath(request, {}), NavigationErrors::InvalidWorld);
        request = Request(info);
        request.topology = NavigationGeneration::Create(12).Value();
        RequireError(provider->FindPath(request, {}), NavigationErrors::StaleSnapshot);
        request = Request(info);
        request.requirement.limits.maximumResultPoints = info.maximumResultPoints + 1U;
        RequireError(provider->FindPath(request, {}), NavigationErrors::QueryLimitExceeded);
    }

    TEST_CASE("Recast Detour provider construction and teardown repeat across scene reload", "[unit][navigation][provider]") {
        const SquareTopology topology;
        const auto info = CreateInfo(topology);
        for (std::size_t reload = 0; reload < 32; ++reload) {
            auto created = CreateRecastDetourNavigationQueryBackend(info);
            REQUIRE(created.HasValue());
            const auto path = created.Value()->FindPath(Request(info), {});
            REQUIRE(path.HasValue());
        }
    }

    TEST_CASE("Recast Detour query leases never share native query scratch", "[unit][navigation][provider]") {
        const SquareTopology topology;
        auto info = CreateInfo(topology);
        info.maximumConcurrentQueries = 1;
        auto created = CreateRecastDetourNavigationQueryBackend(info);
        REQUIRE(created.HasValue());
        auto provider = std::move(created).Value();
        std::atomic<bool> unexpected{false};

        auto query = [&] {
            for (std::size_t iteration = 0; iteration < 500; ++iteration) {
                const auto result = provider->FindPath(Request(info), {});
                if (result.HasValue())
                    continue;
                if (result.ErrorValue().code.Value() != NavigationErrors::AdmissionRejected.code.Value())
                    unexpected.store(true, std::memory_order_relaxed);
            }
        };
        std::array<std::thread, 4> workers{std::thread{query}, std::thread{query}, std::thread{query}, std::thread{query}};
        for (std::thread &worker : workers)
            worker.join();
        REQUIRE_FALSE(unexpected.load(std::memory_order_relaxed));
    }
}  // namespace Horo::Navigation
