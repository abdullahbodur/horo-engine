#include "Horo/WorldStreaming/WorldPartitionDescriptor.h"
#include "Horo/WorldStreaming/WorldStreamingErrors.h"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <limits>
#include <string>
#include <type_traits>
#include <vector>

namespace Horo::WorldStreaming {
    namespace {
        WorldPartitionId Partition() {
            const auto result = WorldPartitionId::Create({1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0});
            REQUIRE(result.HasValue());
            return result.Value();
        }

        Assets::AssetId Asset(const std::uint8_t firstByte) {
            std::array<std::uint8_t, 16> bytes{};
            bytes[0] = firstByte;
            return Assets::AssetId::FromBytes(bytes);
        }

        WorldCellQuantizationPolicy Grid(const Math::WorldCoordinate64 origin = {}, const std::int64_t cellSize = 100,
                                         const WorldCellBounds bounds = {-2, 2, -2, 2, -2, 2}, const std::uint8_t lodLevels = 3) {
            auto result = WorldCellQuantizationPolicy::Create(origin, cellSize, bounds, lodLevels);
            REQUIRE(result.HasValue());
            return result.Value();
        }

        std::vector<WorldLayerDescriptor> Layers() {
            return {
                {StreamingLayerId::Create(7).Value(), "gameplay", WorldLayerOwnership::GameplayScript, WorldLayerFlags::Optional, 0.5F},
                {StreamingLayerId::Create(2).Value(), "base", WorldLayerOwnership::WorldStreaming,
                 WorldLayerFlags::Persistent | WorldLayerFlags::ServerOnly, 1.0F},
            };
        }

        std::vector<WorldPartitionCellDescriptor> Cells() {
            return {
                {{1, 0, 0, 2, StreamingLayerId::Create(7).Value()}, {Asset(2)}},
                {{-1, 1, 0, 0, StreamingLayerId::Create(2).Value()}, {Asset(1)}},
            };
        }

        constexpr WorldPartitionDescriptorLimits Limits{8, 16, 128};

        Result<WorldPartitionDescriptor> Create(
            std::span<const WorldLayerDescriptor> layers, std::span<const WorldPartitionCellDescriptor> cells,
            const WorldPartitionDescriptorLimits limits = Limits,
            const WorldPartitionBounds bounds = {Math::WorldCoordinate64::FromMillimeters(-200, -200, -200),
                                                 Math::WorldCoordinate64::FromMillimeters(299, 299, 299)}) {
            const auto grid = Grid();
            return WorldPartitionDescriptor::Create({}, Partition(), bounds, grid, layers, cells, limits);
        }

        TEST_CASE("Partition descriptor owns canonically ordered immutable input", "[unit][world_streaming][partition]") {
            auto layers = Layers();
            auto cells = Cells();
            auto result = Create(layers, cells);
            REQUIRE(result.HasValue());
            auto descriptor = std::move(result).Value();

            layers[0].name = "mutated";
            cells[0].package.chunkAsset = Asset(9);

            REQUIRE(descriptor.Version() == WorldPartitionSchemaVersion{});
            REQUIRE(descriptor.Partition() == Partition());
            REQUIRE(descriptor.Layers().size() == 2);
            REQUIRE(descriptor.Layers()[0].id.Value() == 2);
            REQUIRE(descriptor.Layers()[1].id.Value() == 7);
            REQUIRE(descriptor.Layers()[1].name == "gameplay");
            REQUIRE(descriptor.Cells()[0].id.layer.Value() == 2);
            REQUIRE(descriptor.Cells()[1].package.chunkAsset == Asset(2));
            static_assert(!std::is_copy_constructible_v<WorldPartitionDescriptor>);
            static_assert(std::is_move_constructible_v<WorldPartitionDescriptor>);
            static_assert(!std::is_move_assignable_v<WorldPartitionDescriptor>);

            std::vector<WorldPartitionDescriptor> storage;
            storage.push_back(std::move(descriptor));
            auto second = Create(Layers(), Cells());
            REQUIRE(second.HasValue());
            storage.push_back(std::move(second).Value());
            REQUIRE(storage.size() == 2);
            REQUIRE(storage[0].Layers()[0].id.Value() == 2);
        }

        TEST_CASE("Partition cells use the stable layer LOD Z Y X ordering", "[unit][world_streaming][partition]") {
            const auto layer = StreamingLayerId::Create(2).Value();
            const std::vector<WorldLayerDescriptor> layers{
                {layer, "base", WorldLayerOwnership::WorldStreaming, WorldLayerFlags::None, 1.0F}};
            const std::vector<WorldPartitionCellDescriptor> cells{
                {{0, 0, 1, 0, layer}, {Asset(6)}}, {{0, 1, 0, 0, layer}, {Asset(5)}}, {{0, 0, 0, 1, layer}, {Asset(4)}},
                {{1, 0, 0, 0, layer}, {Asset(3)}}, {{0, 0, 0, 0, layer}, {Asset(2)}}, {{-1, 0, 0, 0, layer}, {Asset(1)}},
            };

            auto result = Create(layers, cells);
            REQUIRE(result.HasValue());
            const auto ordered = result.Value().Cells();
            REQUIRE(ordered[0].id.x == -1);
            REQUIRE(ordered[1].id.x == 0);
            REQUIRE(ordered[2].id.x == 1);
            REQUIRE(ordered[3].id.y == 1);
            REQUIRE(ordered[4].id.z == 1);
            REQUIRE(ordered[5].id.lod == 1);
        }

        TEST_CASE("Partition descriptor rejects unsupported identity version and empty storage", "[unit][world_streaming][partition]") {
            const auto layers = Layers();
            const auto cells = Cells();
            const auto grid = Grid();

            REQUIRE(WorldPartitionDescriptor::Create({2, 0}, Partition(), {}, grid, layers, cells, Limits).ErrorValue().code.Value() ==
                    WorldStreamingErrors::PartitionVersionUnsupported.code.Value());
            REQUIRE(WorldPartitionDescriptor::Create({}, {}, {}, grid, layers, cells, Limits).HasError());
            REQUIRE(Create({}, cells).ErrorValue().code.Value() == WorldStreamingErrors::PartitionDescriptorInvalid.code.Value());
            REQUIRE(Create(layers, {}).HasError());
            REQUIRE(Create(layers, cells, {0, 1, 1}).HasError());
        }

        TEST_CASE("Partition bounds must be ordered and fit a checked grid envelope", "[unit][world_streaming][partition]") {
            const auto layers = Layers();
            const auto cells = Cells();
            REQUIRE(Create(layers, cells, Limits,
                           {Math::WorldCoordinate64::FromMillimeters(1, 0, 0), Math::WorldCoordinate64::FromMillimeters(0, 0, 0)})
                        .HasError());
            REQUIRE(Create(layers, cells, Limits,
                           {Math::WorldCoordinate64::FromMillimeters(-201, 0, 0), Math::WorldCoordinate64::FromMillimeters(0, 0, 0)})
                        .ErrorValue()
                        .code.Value() == WorldStreamingErrors::PartitionBoundsInvalid.code.Value());

            const auto overflowGrid = Grid(Math::WorldCoordinate64::FromMillimeters(std::numeric_limits<std::int64_t>::max(), 0, 0),
                                           std::numeric_limits<std::int64_t>::max(), {1, 1, 0, 0, 0, 0}, 1);
            REQUIRE(WorldPartitionDescriptor::Create({}, Partition(), {}, overflowGrid, layers, cells, Limits).HasError());

            const auto zeroCellGrid = Grid({}, 100, {0, 0, 0, 0, 0, 0}, 1);
            const std::vector<WorldPartitionCellDescriptor> zeroCell{{{0, 0, 0, 0, StreamingLayerId::Create(2).Value()}, {Asset(1)}}};
            REQUIRE(WorldPartitionDescriptor::Create({}, Partition(),
                                                     {Math::WorldCoordinate64::FromMillimeters(0, 0, 0),
                                                      Math::WorldCoordinate64::FromMillimeters(99, 99, 99)},
                                                     zeroCellGrid, layers, zeroCell, Limits)
                        .HasValue());

            const auto negativeOverflowGrid = Grid(Math::WorldCoordinate64::FromMillimeters(std::numeric_limits<std::int64_t>::min(), 0, 0),
                                                   std::numeric_limits<std::int64_t>::max(), {-1, -1, 0, 0, 0, 0}, 1);
            REQUIRE(WorldPartitionDescriptor::Create({}, Partition(), {}, negativeOverflowGrid, layers, cells, Limits).HasError());
        }

        TEST_CASE("Layer validation rejects unknown malformed duplicate and over-capacity definitions",
                  "[unit][world_streaming][partition]") {
            const auto cells = Cells();

            SECTION("unknown enum and flags") {
                auto layers = Layers();
                layers[0].ownership = static_cast<WorldLayerOwnership>(99);
                REQUIRE(Create(layers, cells).HasError());
                layers = Layers();
                layers[0].flags = static_cast<WorldLayerFlags>(1U << 31U);
                REQUIRE(Create(layers, cells).HasError());
            }
            SECTION("invalid names and priorities") {
                auto layers = Layers();
                layers[0].name.clear();
                REQUIRE(Create(layers, cells).HasError());
                layers = Layers();
                layers[0].name = std::string{"bad\0name", 8};
                REQUIRE(Create(layers, cells).HasError());
                layers = Layers();
                layers[0].priorityMultiplier = std::numeric_limits<float>::infinity();
                REQUIRE(Create(layers, cells).HasError());
            }
            SECTION("duplicates and hard limits") {
                auto layers = Layers();
                layers[1].id = layers[0].id;
                REQUIRE(Create(layers, cells).ErrorValue().code.Value() == WorldStreamingErrors::PartitionIdentityConflict.code.Value());
                layers = Layers();
                REQUIRE(Create(layers, cells, {1, 16, 128}).ErrorValue().code.Value() ==
                        WorldStreamingErrors::PartitionCapacityExceeded.code.Value());
                REQUIRE(Create(layers, cells, {8, 16, 4}).HasError());
            }
        }

        TEST_CASE("Cell validation enforces grid layer package identity and capacity", "[unit][world_streaming][partition]") {
            const auto layers = Layers();

            SECTION("invalid cell fields") {
                auto cells = Cells();
                cells[0].id.lod = 3;
                REQUIRE(Create(layers, cells).HasError());
                cells = Cells();
                cells[0].id.x = 3;
                REQUIRE(Create(layers, cells).HasError());
                cells = Cells();
                cells[0].id.layer = StreamingLayerId::Create(4).Value();
                REQUIRE(Create(layers, cells).HasError());
                cells = Cells();
                cells[0].package.chunkAsset = {};
                REQUIRE(Create(layers, cells).HasError());
            }
            SECTION("duplicates and capacity") {
                auto cells = Cells();
                cells.push_back(cells[0]);
                REQUIRE(Create(layers, cells).ErrorValue().code.Value() == WorldStreamingErrors::PartitionIdentityConflict.code.Value());
                cells = Cells();
                REQUIRE(Create(layers, cells, {8, 1, 128}).ErrorValue().code.Value() ==
                        WorldStreamingErrors::PartitionCapacityExceeded.code.Value());
            }
        }

        TEST_CASE("Failed descriptor construction does not mutate caller storage", "[unit][world_streaming][partition]") {
            auto layers = Layers();
            auto cells = Cells();
            const auto originalLayers = layers;
            const auto originalCells = cells;
            cells.push_back(cells.front());
            const auto expectedCells = cells;

            REQUIRE(Create(layers, cells).HasError());
            REQUIRE(layers == originalLayers);
            REQUIRE(cells == expectedCells);
            REQUIRE(originalCells.size() == 2);
        }
    }  // namespace
}  // namespace Horo::WorldStreaming
