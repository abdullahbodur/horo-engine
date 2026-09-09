#include "Horo/WorldStreaming/FallbackStreamingProvider.h"
#include "WorldStreamingTestUtils.h"

#include <catch2/catch_test_macros.hpp>

namespace Horo::WorldStreaming {
    namespace {
        using namespace TestSupport;

        StreamingCellId Cell(const std::int32_t x = 4) {
            return {.x = x, .y = -2, .z = 1, .lod = 0, .layer = Layer()};
        }

        FallbackStreamingProviderDescriptor SingleCellDescriptor(const std::uint64_t revision = 1) {
            return {.owner = Owner(),
                    .revision = IdentityFrom<StreamingSourceRevision>(revision),
                    .mode = FallbackStreamingProviderMode::SingleCell,
                    .singleCell = Cell(),
                    .maximumPublishedCells = 1};
        }

        FallbackStreamingProviderDescriptor NullDescriptor(const std::uint64_t revision = 1) {
            return {.owner = Owner(),
                    .revision = IdentityFrom<StreamingSourceRevision>(revision),
                    .mode = FallbackStreamingProviderMode::Null,
                    .singleCell = std::nullopt,
                    .maximumPublishedCells = 0};
        }

        FallbackStreamingProvider Provider() {
            auto result = FallbackStreamingProvider::Create(SingleCellDescriptor());
            REQUIRE(result.HasValue());
            return std::move(result).Value();
        }

        TEST_CASE("Single-cell fallback exposes one desired cell without residency claims", "[unit][world_streaming][fallback]") {
            const auto provider = Provider();
            REQUIRE(provider.Mode() == FallbackStreamingProviderMode::SingleCell);
            REQUIRE(provider.State() == FallbackStreamingProviderState::Active);
            REQUIRE(provider.Owner() == Owner());
            REQUIRE(provider.Revision() == IdentityFrom<StreamingSourceRevision>(1));
            REQUIRE(provider.DesiredCells().size() == 1);
            REQUIRE(provider.DesiredCells().front() == Cell());
        }

        TEST_CASE("Null fallback is an explicit active composition with no desired cells", "[unit][world_streaming][fallback]") {
            auto result = FallbackStreamingProvider::Create(NullDescriptor());
            REQUIRE(result.HasValue());
            const auto provider = std::move(result).Value();
            REQUIRE(provider.Mode() == FallbackStreamingProviderMode::Null);
            REQUIRE(provider.State() == FallbackStreamingProviderState::Active);
            REQUIRE(provider.DesiredCells().empty());
        }

        TEST_CASE("Fallback ownership transfers without leaving live demand in the moved-from instance",
                  "[unit][world_streaming][fallback]") {
            auto source = Provider();
            const auto destination = std::move(source);
            REQUIRE(destination.Owner() == Owner());
            REQUIRE(destination.DesiredCells().size() == 1);
            REQUIRE_FALSE(source.Owner().IsValid());
            REQUIRE(source.State() == FallbackStreamingProviderState::Closed);
            REQUIRE(source.DesiredCells().empty());
        }

        TEST_CASE("Fallback creation rejects malformed unsupported and over-capacity descriptors", "[unit][world_streaming][fallback]") {
            auto descriptor = SingleCellDescriptor();
            const ErrorCodeDescriptor *expected = &WorldStreamingErrors::FallbackProviderInvalid;
            SECTION("invalid owner") {
                descriptor.owner = {};
            }
            SECTION("invalid revision") {
                descriptor.revision = {};
            }
            SECTION("missing cell") {
                descriptor.singleCell = std::nullopt;
            }
            SECTION("null with cell") {
                descriptor.mode = FallbackStreamingProviderMode::Null;
            }
            SECTION("unsupported mode") {
                descriptor.mode = static_cast<FallbackStreamingProviderMode>(255);
                expected = &WorldStreamingErrors::FallbackProviderUnsupported;
            }
            SECTION("cell capacity") {
                descriptor.maximumPublishedCells = 0;
                expected = &WorldStreamingErrors::FallbackProviderCapacityExceeded;
            }
            RequireError(FallbackStreamingProvider::Create(descriptor), *expected);
        }

        TEST_CASE("Fallback replacement is revision-fenced and transactional", "[unit][world_streaming][fallback]") {
            auto provider = Provider();
            auto replacement = NullDescriptor(2);
            REQUIRE(provider.Replace(replacement).HasValue());
            REQUIRE(provider.Mode() == FallbackStreamingProviderMode::Null);
            REQUIRE(provider.Revision() == IdentityFrom<StreamingSourceRevision>(2));
            REQUIRE(provider.DesiredCells().empty());

            replacement = SingleCellDescriptor(3);
            replacement.maximumPublishedCells = 0;
            RequireError(provider.Replace(replacement), WorldStreamingErrors::FallbackProviderCapacityExceeded);
            REQUIRE(provider.Mode() == FallbackStreamingProviderMode::Null);
            REQUIRE(provider.Revision() == IdentityFrom<StreamingSourceRevision>(2));

            replacement = SingleCellDescriptor(2);
            RequireError(provider.Replace(replacement), WorldStreamingErrors::FallbackProviderStale);
            replacement = SingleCellDescriptor(3);
            replacement.owner = Owner(2);
            RequireError(provider.Replace(replacement), WorldStreamingErrors::FallbackProviderStale);
        }

        TEST_CASE("Fallback cancellation and shutdown close demand without reviving stale owners", "[unit][world_streaming][fallback]") {
            auto provider = Provider();
            RequireError(provider.RequestCancellation(Owner(), IdentityFrom<StreamingSourceRevision>(2)),
                         WorldStreamingErrors::FallbackProviderStale);
            REQUIRE(provider.DesiredCells().size() == 1);

            REQUIRE(provider.RequestCancellation(Owner(), IdentityFrom<StreamingSourceRevision>(1)).HasValue());
            REQUIRE(provider.RequestCancellation(Owner(), IdentityFrom<StreamingSourceRevision>(1)).HasValue());
            REQUIRE(provider.State() == FallbackStreamingProviderState::Cancelling);
            REQUIRE(provider.DesiredCells().empty());
            RequireError(provider.Replace(SingleCellDescriptor(2)), WorldStreamingErrors::FallbackProviderLifecycleUnavailable);

            RequireError(provider.Shutdown(Owner(2)), WorldStreamingErrors::FallbackProviderStale);
            REQUIRE(provider.Shutdown(Owner()).HasValue());
            REQUIRE(provider.Shutdown(Owner()).HasValue());
            REQUIRE(provider.State() == FallbackStreamingProviderState::Closed);
            REQUIRE(provider.DesiredCells().empty());
            RequireError(provider.RequestCancellation(Owner(), IdentityFrom<StreamingSourceRevision>(1)),
                         WorldStreamingErrors::FallbackProviderLifecycleUnavailable);
        }
    }  // namespace
}  // namespace Horo::WorldStreaming
