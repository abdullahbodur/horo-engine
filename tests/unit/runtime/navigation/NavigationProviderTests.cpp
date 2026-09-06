#include "Horo/Navigation/Backends/NullProvider.h"
#include "Horo/Navigation/NavigationErrors.h"
#include "navigation/DeterministicNavigationProvider.h"
#include "navigation/NavigationTestAssertions.h"

#include <array>
#include <atomic>
#include <bit>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <thread>

namespace Horo::Navigation {
    namespace {
        using TestSupport::RequireError;

        NavigationPathRequest Request(const Math::Vec3 start = {}, const Math::Vec3 destination = {}) {
            return {
                .world = NavigationWorldId::Create(1).Value(),
                .topology = NavigationGeneration::Create(1).Value(),
                .start = start,
                .destination = destination,
                .requirement =
                    {
                        .query = NavigationQueryKind::Path,
                        .quality = NavigationQualityLevel::Balanced,
                        .limits = {.maximumNodeExpansions = 32, .maximumResultPoints = 8, .maximumSearchDistanceMeters = 100.0F},
                    },
            };
        }

    }  // namespace

    TEST_CASE("Null navigation provider reports explicit absence without plausible paths", "[unit][navigation][headless]") {
        auto created = CreateNullNavigationQueryBackend();
        REQUIRE(created.HasValue());
        auto provider = std::move(created).Value();

        const auto capabilities = provider->Capabilities();
        REQUIRE(ValidateNavigationProviderCapabilities(capabilities));
        REQUIRE(QueryNavigationSupport(capabilities, NavigationQueryKind::Path, NavigationQualityLevel::Balanced) ==
                NavigationSupport::Available);
        RequireError(provider->FindPath(Request({1.0F, 2.0F, 3.0F}, {7.0F, 2.0F, 9.0F}), {}), NavigationErrors::NoNavigationData);
    }

    TEST_CASE("Deterministic navigation fixture returns exact declared path bits", "[unit][navigation][headless]") {
        const TestSupport::NavigationPathFixture fixture{
            .start = {1.0F, 0.0F, 2.0F},
            .destination = {5.0F, 0.0F, 8.0F},
            .path = {.points = {{1.0F, 0.0F, 2.0F}, {3.5F, 0.0F, 4.25F}, {5.0F, 0.0F, 8.0F}}, .lengthMeters = 7.25F},
        };
        const TestSupport::DeterministicNavigationQueryBackend provider{std::span{&fixture, 1}};

        const auto first = provider.FindPath(Request(fixture.start, fixture.destination), {});
        const auto second = provider.FindPath(Request(fixture.start, fixture.destination), {});
        REQUIRE(first.HasValue());
        REQUIRE(second.HasValue());
        REQUIRE(first.Value().points == second.Value().points);
        REQUIRE(first.Value().points == fixture.path.points);
        REQUIRE(std::bit_cast<std::uint32_t>(first.Value().lengthMeters) == std::bit_cast<std::uint32_t>(fixture.path.lengthMeters));
        for (std::size_t index = 0; index < fixture.path.points.size(); ++index) {
            REQUIRE(std::bit_cast<std::uint32_t>(first.Value().points[index].x) ==
                    std::bit_cast<std::uint32_t>(fixture.path.points[index].x));
            REQUIRE(std::bit_cast<std::uint32_t>(first.Value().points[index].y) ==
                    std::bit_cast<std::uint32_t>(fixture.path.points[index].y));
            REQUIRE(std::bit_cast<std::uint32_t>(first.Value().points[index].z) ==
                    std::bit_cast<std::uint32_t>(fixture.path.points[index].z));
        }
    }

    TEST_CASE("Deterministic navigation fixture makes misses and injected faults distinct", "[unit][navigation][headless]") {
        using TestSupport::NavigationFixtureFault;
        TestSupport::DeterministicNavigationQueryBackend provider{std::span<const TestSupport::NavigationPathFixture>{}};

        RequireError(provider.FindPath(Request(), {}), NavigationErrors::NoNavigationData);
        for (const auto [fault, error] : std::array{
                 std::pair{NavigationFixtureFault::Allocation, &NavigationErrors::CapacityExceeded},
                 std::pair{NavigationFixtureFault::StaleTopology, &NavigationErrors::StaleSnapshot},
                 std::pair{NavigationFixtureFault::Cancellation, &NavigationErrors::QueryCancelled},
             }) {
            provider.SetFault(fault);
            RequireError(provider.FindPath(Request(), {}), *error);
        }

        provider.SetFault(NavigationFixtureFault::None);
        CancellationSource cancellation;
        cancellation.RequestCancellation();
        RequireError(provider.FindPath(Request(), cancellation.Token()), NavigationErrors::QueryCancelled);
    }

    TEST_CASE("Deterministic navigation fault injection is safe during concurrent queries", "[unit][navigation][headless]") {
        using TestSupport::NavigationFixtureFault;
        TestSupport::DeterministicNavigationQueryBackend provider{std::span<const TestSupport::NavigationPathFixture>{}};
        std::atomic<bool> observedUnexpectedError{false};

        std::thread writer{[&provider] {
            for (std::size_t iteration = 0; iteration < 1'000; ++iteration) {
                provider.SetFault(NavigationFixtureFault::Cancellation);
                provider.SetFault(NavigationFixtureFault::None);
            }
        }};
        std::thread reader{[&provider, &observedUnexpectedError] {
            for (std::size_t iteration = 0; iteration < 1'000; ++iteration) {
                const auto result = provider.FindPath(Request(), {});
                if (!result.HasError() || (result.ErrorValue().code.Value() != NavigationErrors::NoNavigationData.code.Value() &&
                                           result.ErrorValue().code.Value() != NavigationErrors::QueryCancelled.code.Value())) {
                    observedUnexpectedError.store(true, std::memory_order_relaxed);
                    return;
                }
            }
        }};

        writer.join();
        reader.join();
        REQUIRE_FALSE(observedUnexpectedError.load(std::memory_order_relaxed));
    }
}  // namespace Horo::Navigation
