#include "Horo/Assets/AssetProvider.h"
#include "Horo/WorldStreaming/StreamingCellAssetRequest.h"
#include "Horo/WorldStreaming/WorldStreamingErrors.h"
#include "StreamingCellCandidateTestSupport.h"
#include "WorldStreamingTestUtils.h"

#include <array>
#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <string>
#include <thread>
#include <type_traits>

namespace Horo::WorldStreaming {
    namespace {
        using namespace CandidateTestSupport;
        using TestSupport::Asset;
        using TestSupport::IdentityFrom;
        using TestSupport::RequireError;

        Assets::AssetRecord Record(const Assets::AssetId id, const std::size_t index) {
            const auto source = "assets/cell" + std::to_string(index) + ".bin";
            return {id, Assets::AssetTypeId::Parse("core.cell").Value(), ProjectPath::Parse(source).Value(),
                    ProjectPath::Parse(source + ".horo").Value()};
        }

        Assets::AssetRegistrySnapshot Registry(const std::span<const Assets::AssetId> ids) {
            Assets::AssetRegistry registry;
            std::vector<Assets::AssetRecord> records;
            for (std::size_t index{}; index < ids.size(); ++index)
                records.push_back(Record(ids[index], index));
            REQUIRE(registry.Publish(std::move(records)).status == Assets::AssetRegistryBuildStatus::Complete);
            return registry.Snapshot();
        }

        StreamingCellAssetRequestContext Context() {
            return {IdentityFrom<StreamingCellAssetRequestId>(3), Operation(), 3, StreamingCellAssetRequestLifecycle::Active};
        }

        struct RequestFixture final {
            std::array<Assets::AssetId, 3> ids{Asset(4), Asset(5), Asset(6)};
            CookedWorldIndexManifest manifest{Manifest()};
            StreamingCellCandidate candidate{Candidate(manifest)};
            Assets::AssetRegistrySnapshot registry{Registry(ids)};
        };

        void WaitTerminal(StreamingCellAssetRequest &request) {
            while (request.State() == StreamingCellAssetRequestState::Loading ||
                   request.State() == StreamingCellAssetRequestState::Cancelling)
                std::this_thread::yield();
        }

        class BlockingProvider final : public Assets::IAssetProvider {
        public:
            Result<bool> Exists(Assets::AssetId, const CancellationToken &) const override {
                return Result<bool>::Success(true);
            }

            Result<std::vector<std::uint8_t>> Load(Assets::AssetId, const CancellationToken &cancellation) const override {
                entered.store(true);
                while (!cancellation.IsCancellationRequested())
                    std::this_thread::yield();
                return Result<std::vector<std::uint8_t>>::Failure(
                    Error{ErrorCode{"asset.load.cancelled"}, ErrorDomainId{"horo.asset"}, ErrorSeverity::Warning, "cancelled", {}});
            }

            mutable std::atomic<bool> entered{};
        };
    }  // namespace

    TEST_CASE("Cell asset request joins candidate and dependency bytes in canonical order", "[unit][world_streaming][asset_request]") {
        RequestFixture fixture;
        Assets::MemoryAssetProvider provider;
        provider.Insert(Asset(4), {4});
        provider.Insert(Asset(5), {5});
        provider.Insert(Asset(6), {6});
        JobSystem jobs{{2, 8}};
        Assets::AssetLoadService service{jobs, provider};
        auto result = RequestStreamingCellAssets(service, fixture.registry, fixture.manifest, fixture.candidate, Context());
        REQUIRE(result.HasValue());
        auto request = std::move(result).Value();
        WaitTerminal(request);
        auto batch = request.TakeResult();
        REQUIRE(batch.HasValue());
        REQUIRE(batch.Value().request == Context().request);
        REQUIRE(batch.Value().operation == Operation());
        REQUIRE(batch.Value().registryRevision == fixture.registry.Revision());
        REQUIRE(batch.Value().assets.size() == 3);
        REQUIRE(batch.Value().assets[0].asset == Asset(4));
        REQUIRE((batch.Value().assets[0].bytes == std::vector<std::uint8_t>{4}));
        REQUIRE(batch.Value().assets[1].asset == Asset(5));
        REQUIRE(batch.Value().assets[2].asset == Asset(6));
        RequireError(request.TakeResult(), WorldStreamingErrors::CellAssetRequestConsumed);
        static_assert(!std::is_copy_constructible_v<StreamingCellAssetRequest>);
        static_assert(std::is_move_constructible_v<StreamingCellAssetRequest>);
    }

    TEST_CASE("Cell asset request rejects stale bounded and unavailable input transactionally",
              "[unit][world_streaming][asset_request][failure]") {
        RequestFixture fixture;
        Assets::MemoryAssetProvider provider;
        JobSystem jobs{{1, 8}};
        Assets::AssetLoadService service{jobs, provider};
        auto context = Context();
        context.maximumRequests = 2;
        RequireError(RequestStreamingCellAssets(service, fixture.registry, fixture.manifest, fixture.candidate, context),
                     WorldStreamingErrors::CellAssetRequestCapacityExceeded);
        context = Context();
        context.operation = Operation(IdentityFrom<StreamingGeneration>(2));
        RequireError(RequestStreamingCellAssets(service, fixture.registry, fixture.manifest, fixture.candidate, context),
                     WorldStreamingErrors::CellAssetRequestStale);
        const std::array missing{Asset(4), Asset(5)};
        fixture.registry = Registry(missing);
        context = Context();
        RequireError(RequestStreamingCellAssets(service, fixture.registry, fixture.manifest, fixture.candidate, context),
                     WorldStreamingErrors::CellAssetRequestUnavailable);
        context.lifecycle = StreamingCellAssetRequestLifecycle::Closed;
        RequireError(RequestStreamingCellAssets(service, fixture.registry, fixture.manifest, fixture.candidate, context),
                     WorldStreamingErrors::CellAssetRequestLifecycleUnavailable);
    }

    TEST_CASE("Cell asset request propagates cancellation to every child", "[unit][world_streaming][asset_request][cancellation]") {
        RequestFixture fixture;
        BlockingProvider provider;
        JobSystem jobs{{1, 8}};
        Assets::AssetLoadService service{jobs, provider};
        auto result = RequestStreamingCellAssets(service, fixture.registry, fixture.manifest, fixture.candidate, Context());
        REQUIRE(result.HasValue());
        auto request = std::move(result).Value();
        while (!provider.entered.load())
            std::this_thread::yield();
        REQUIRE(request.RequestCancel().HasValue());
        WaitTerminal(request);
        REQUIRE(request.State() == StreamingCellAssetRequestState::Cancelled);
        REQUIRE(request.TakeResult().HasError());
    }
}  // namespace Horo::WorldStreaming
