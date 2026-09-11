#include "Horo/WorldStreaming/WorldPartitionRegistry.h"
#include "Horo/WorldStreaming/WorldStreamingErrors.h"
#include "WorldStreamingTestUtils.h"

#include <array>
#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <limits>
#include <thread>
#include <vector>

namespace Horo::WorldStreaming {
    namespace {
        using TestSupport::Asset;
        using TestSupport::IdentityFrom;
        using TestSupport::Layer;
        using TestSupport::RequireError;
        using TestSupport::World;

        StreamingRuntimeOwnerToken Owner(const std::uint64_t epoch = 4) {
            return {.partition = World(), .epoch = IdentityFrom<PartitionEpoch>(epoch), .owner = IdentityFrom<StreamingRuntimeOwnerId>(9)};
        }

        WorldCellQuantizationPolicy Grid() {
            return WorldCellQuantizationPolicy::Create(Math::WorldCoordinate64::FromMillimeters(-100, -100, -100), 100,
                                                       {-1, 3, -1, 3, -1, 3}, 2)
                .Value();
        }

        Result<WorldPartitionDescriptor> Descriptor(const std::uint8_t assetOffset = 0, const WorldPartitionId partition = World()) {
            const auto base = Layer(2);
            const auto detail = Layer(7);
            const std::array layers{
                WorldLayerDescriptor{base, "base", WorldLayerOwnership::WorldStreaming, WorldLayerFlags::None, 1.0F},
                WorldLayerDescriptor{detail, "detail", WorldLayerOwnership::WorldStreaming, WorldLayerFlags::Optional, 0.5F},
            };
            const std::array cells{
                WorldPartitionCellDescriptor{{1, 0, 0, 1, detail}, {Asset(static_cast<std::uint8_t>(assetOffset + 4))}},
                WorldPartitionCellDescriptor{{1, 0, 0, 0, base}, {Asset(static_cast<std::uint8_t>(assetOffset + 3))}},
                WorldPartitionCellDescriptor{{0, 0, 0, 0, base}, {Asset(static_cast<std::uint8_t>(assetOffset + 2))}},
                WorldPartitionCellDescriptor{{-1, 0, 0, 0, base}, {Asset(static_cast<std::uint8_t>(assetOffset + 1))}},
            };
            return WorldPartitionDescriptor::Create({}, partition,
                                                    {Math::WorldCoordinate64::FromMillimeters(-100, -100, -100),
                                                     Math::WorldCoordinate64::FromMillimeters(299, 299, 299)},
                                                    Grid(), layers, cells, {8, 8, 64});
        }

        std::unique_ptr<WorldPartitionRegistry> Registry(const WorldPartitionRegistryLimits limits = {8, 4}) {
            auto result = WorldPartitionRegistry::Create(IdentityFrom<WorldPartitionRegistryId>(12), Owner(), limits);
            REQUIRE(result.HasValue());
            return std::move(result).Value();
        }

        TEST_CASE("Partition registry publishes a canonical generation-pinned immutable snapshot",
                  "[unit][world_streaming][partition_registry]") {
            auto registry = Registry();
            REQUIRE(registry->Publish(Descriptor().Value(), IdentityFrom<WorldPartitionRegistryRevision>(1)).HasValue());
            const auto snapshot = registry->Snapshot().Value();

            REQUIRE(snapshot.IsValid());
            REQUIRE(snapshot.Binding().owner == Owner());
            REQUIRE(snapshot.Binding().revision.Value() == 1);
            REQUIRE(snapshot.Cells().size() == 4);
            REQUIRE(snapshot.Cells()[0].id.x == -1);
            REQUIRE(snapshot.Cells()[3].id.lod == 1);
        }

        TEST_CASE("Exact lookup and resolve reject malformed unknown and cross-generation handles",
                  "[unit][world_streaming][partition_registry][lookup]") {
            auto registry = Registry();
            REQUIRE(registry->Publish(Descriptor().Value(), IdentityFrom<WorldPartitionRegistryRevision>(1)).HasValue());
            const auto first = registry->Snapshot().Value();
            const StreamingCellId cell{0, 0, 0, 0, Layer(2)};
            const auto handle = first.Find(cell).Value();
            REQUIRE(first.Resolve(handle).Value()->package.chunkAsset == Asset(2));

            RequireError(first.Find({8, 0, 0, 0, Layer(2)}), WorldStreamingErrors::PartitionRegistryUnavailable);
            RequireError(first.Find({}), WorldStreamingErrors::PartitionRegistryInvalid);

            REQUIRE(registry->Publish(Descriptor(10).Value(), IdentityFrom<WorldPartitionRegistryRevision>(2)).HasValue());
            const auto second = registry->Snapshot().Value();
            REQUIRE(first.Resolve(handle).Value()->package.chunkAsset == Asset(2));
            RequireError(second.Resolve(handle), WorldStreamingErrors::PartitionRegistryStale);
            REQUIRE(second.Resolve(second.Find(cell).Value()).Value()->package.chunkAsset == Asset(12));
        }

        TEST_CASE("Spatial query is inclusive bounded allocation-free and canonically ordered",
                  "[unit][world_streaming][partition_registry][query]") {
            auto registry = Registry();
            REQUIRE(registry->Publish(Descriptor().Value(), IdentityFrom<WorldPartitionRegistryRevision>(1)).HasValue());
            const auto snapshot = registry->Snapshot().Value();
            std::array<WorldPartitionCellHandle, 4> output{};

            const WorldPartitionSpatialQuery query{{Math::WorldCoordinate64::FromMillimeters(-1, -50, -50),
                                                    Math::WorldCoordinate64::FromMillimeters(100, 50, 50)},
                                                   Layer(2),
                                                   0};
            const auto count = snapshot.Query(query, output);
            REQUIRE(count.Value() == 2);
            REQUIRE(output[0].cell.x == 0);
            REQUIRE(output[1].cell.x == 1);
            REQUIRE(snapshot.Resolve(output[1]).HasValue());
        }

        TEST_CASE("Spatial query validates filters and preserves caller output on capacity failure",
                  "[unit][world_streaming][partition_registry][query][failure]") {
            auto registry = Registry({8, 2});
            REQUIRE(registry->Publish(Descriptor().Value(), IdentityFrom<WorldPartitionRegistryRevision>(1)).HasValue());
            const auto snapshot = registry->Snapshot().Value();
            const WorldPartitionCellHandle sentinel{snapshot.Binding(), 99, {9, 9, 9, 0, Layer(2)}};
            std::array output{sentinel, sentinel};
            const WorldPartitionBounds whole{Math::WorldCoordinate64::FromMillimeters(-1000, -1000, -1000),
                                             Math::WorldCoordinate64::FromMillimeters(1000, 1000, 1000)};

            RequireError(snapshot.Query({whole, std::nullopt, std::nullopt}, output),
                         WorldStreamingErrors::PartitionRegistryCapacityExceeded);
            REQUIRE(output[0] == sentinel);
            RequireError(snapshot.Query({whole, Layer(99), std::nullopt}, output), WorldStreamingErrors::PartitionRegistryUnsupported);
            RequireError(snapshot.Query({whole, std::nullopt, 2}, output), WorldStreamingErrors::PartitionRegistryUnsupported);
            RequireError(snapshot.Query({{whole.maximum, whole.minimum}, std::nullopt, std::nullopt}, output),
                         WorldStreamingErrors::PartitionRegistryInvalid);
        }

        TEST_CASE("Failed publication preserves the current immutable registry state",
                  "[unit][world_streaming][partition_registry][replacement]") {
            auto registry = Registry();
            REQUIRE(registry->Publish(Descriptor().Value(), IdentityFrom<WorldPartitionRegistryRevision>(1)).HasValue());
            const auto before = registry->Snapshot().Value();

            auto staleCandidate = Descriptor(10).Value();
            const auto staleCandidateCount = staleCandidate.Cells().size();
            RequireError(registry->Publish(std::move(staleCandidate), IdentityFrom<WorldPartitionRegistryRevision>(3)),
                         WorldStreamingErrors::PartitionRegistryStale);
            REQUIRE(staleCandidate.Cells().size() == staleCandidateCount);
            RequireError(registry->Publish(Descriptor(10, World(2)).Value(), IdentityFrom<WorldPartitionRegistryRevision>(2)),
                         WorldStreamingErrors::PartitionRegistryInvalid);
            const auto after = registry->Snapshot().Value();
            REQUIRE(after.Binding() == before.Binding());
            REQUIRE(after.Cells()[0].package.chunkAsset == before.Cells()[0].package.chunkAsset);
        }

        TEST_CASE("Registry rejects invalid and over-capacity construction or publication",
                  "[unit][world_streaming][partition_registry][limits]") {
            RequireError(WorldPartitionRegistry::Create({}, Owner(), {8, 4}), WorldStreamingErrors::PartitionRegistryInvalid);
            RequireError(WorldPartitionRegistry::Create(IdentityFrom<WorldPartitionRegistryId>(1), Owner(), {0, 0}),
                         WorldStreamingErrors::PartitionRegistryInvalid);

            auto registry = Registry({3, 2});
            RequireError(registry->Publish(Descriptor().Value(), IdentityFrom<WorldPartitionRegistryRevision>(1)),
                         WorldStreamingErrors::PartitionRegistryCapacityExceeded);
            RequireError(registry->Snapshot(), WorldStreamingErrors::PartitionRegistryUnavailable);
        }

        TEST_CASE("Cancellation and shutdown close new admission but retained snapshots remain usable",
                  "[unit][world_streaming][partition_registry][lifecycle]") {
            auto registry = Registry();
            REQUIRE(registry->Publish(Descriptor().Value(), IdentityFrom<WorldPartitionRegistryRevision>(1)).HasValue());
            const auto retained = registry->Snapshot().Value();
            registry->BeginCancellation();
            registry->BeginCancellation();
            REQUIRE(registry->Lifecycle() == WorldPartitionRegistryState::Cancelling);
            RequireError(registry->Snapshot(), WorldStreamingErrors::PartitionRegistryLifecycleUnavailable);
            RequireError(registry->Publish(Descriptor(10).Value(), IdentityFrom<WorldPartitionRegistryRevision>(2)),
                         WorldStreamingErrors::PartitionRegistryLifecycleUnavailable);
            REQUIRE(retained.Find({0, 0, 0, 0, Layer(2)}).HasValue());

            registry->Shutdown();
            registry->Shutdown();
            REQUIRE(registry->Lifecycle() == WorldPartitionRegistryState::Closed);
            REQUIRE(retained.Find({0, 0, 0, 0, Layer(2)}).HasValue());
        }

        TEST_CASE("Concurrent readers pin whole old or new registry publications",
                  "[unit][world_streaming][partition_registry][concurrency]") {
            auto registry = Registry();
            REQUIRE(registry->Publish(Descriptor().Value(), IdentityFrom<WorldPartitionRegistryRevision>(1)).HasValue());
            std::atomic_bool valid{true};
            std::thread reader([registryPointer = registry.get(), &valid] {
                for (std::size_t index = 0; index < 2'000; ++index) {
                    const auto captured = registryPointer->Snapshot();
                    if (captured.HasError()) {
                        valid.store(false);
                        return;
                    }
                    const auto snapshot = captured.Value();
                    const auto revision = snapshot.Binding().revision.Value();
                    const auto asset = snapshot.Find({0, 0, 0, 0, Layer(2)});
                    if (asset.HasError() || (revision != 1 && revision != 2)) {
                        valid.store(false);
                        return;
                    }
                }
            });
            REQUIRE(registry->Publish(Descriptor(10).Value(), IdentityFrom<WorldPartitionRegistryRevision>(2)).HasValue());
            reader.join();
            REQUIRE(valid.load());
        }

        TEST_CASE("Registry revisions are non-zero and never wrap", "[unit][world_streaming][partition_registry][revision]") {
            RequireError(NextWorldPartitionRegistryRevision({}), WorldStreamingErrors::IdentityInvalid);
            RequireError(NextWorldPartitionRegistryRevision(
                             IdentityFrom<WorldPartitionRegistryRevision>(std::numeric_limits<std::uint64_t>::max())),
                         WorldStreamingErrors::GenerationExhausted);
            REQUIRE(NextWorldPartitionRegistryRevision(IdentityFrom<WorldPartitionRegistryRevision>(7)).Value().Value() == 8);
        }
    }  // namespace
}  // namespace Horo::WorldStreaming
