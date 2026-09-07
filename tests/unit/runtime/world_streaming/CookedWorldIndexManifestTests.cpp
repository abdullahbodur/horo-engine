#include "Horo/WorldStreaming/CookedWorldIndexManifest.h"
#include "Horo/WorldStreaming/WorldStreamingErrors.h"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <limits>
#include <span>
#include <type_traits>
#include <vector>

namespace Horo::WorldStreaming {
    namespace {
        Assets::AssetId Asset(const std::uint8_t discriminator) {
            std::array<std::uint8_t, 16> bytes{};
            bytes.front() = discriminator;
            return Assets::AssetId::FromBytes(bytes);
        }

        WorldPartitionId Partition() {
            SerializedWorldPartitionId bytes{};
            bytes.front() = 1;
            return WorldPartitionId::Create(bytes).Value();
        }

        StreamingCellId Cell(const std::int32_t x) {
            return {x, 0, 0, 0, StreamingLayerId::Create(2).Value()};
        }

        WorldPartitionDescriptor Descriptor() {
            const auto grid = WorldCellQuantizationPolicy::Create({}, 100, {-2, 2, -2, 2, -2, 2}, 1).Value();
            const std::vector<WorldLayerDescriptor> layers{
                {StreamingLayerId::Create(2).Value(), "base", WorldLayerOwnership::WorldStreaming, WorldLayerFlags::Persistent, 1.0F}};
            const std::vector<WorldPartitionCellDescriptor> cells{{Cell(1), {Asset(3)}}, {Cell(-1), {Asset(1)}}, {Cell(0), {Asset(2)}}};
            auto result = WorldPartitionDescriptor::Create({}, Partition(),
                                                           {Math::WorldCoordinate64::FromMillimeters(-200, -200, -200),
                                                            Math::WorldCoordinate64::FromMillimeters(299, 299, 299)},
                                                           grid, layers, cells, {4, 8, 64});
            REQUIRE(result.HasValue());
            return std::move(result).Value();
        }

        Sha256Digest Hash(const std::uint8_t discriminator) {
            Sha256Digest hash{};
            hash.bytes.front() = discriminator;
            return hash;
        }

        std::vector<CookedWorldCellManifestEntry> Entries() {
            return {
                {Cell(1), 300, 200, 33, Hash(3), {Cell(0), Cell(-1)}},
                {Cell(0), 200, 125, 22, Hash(2), {}},
                {Cell(-1), 100, 75, 11, Hash(1), {}},
            };
        }

        constexpr CookedWorldIndexManifestLimits Limits{8, 4, 8, 1024, 2048};

        Result<CookedWorldIndexManifest> Create(WorldPartitionDescriptor &descriptor,
                                                const std::span<const CookedWorldCellManifestEntry> entries,
                                                const CookedWorldIndexManifestLimits limits = Limits) {
            return CookedWorldIndexManifest::Create(std::move(descriptor), entries, limits);
        }

        void RequireError(const Result<CookedWorldIndexManifest> &result, const ErrorCodeDescriptor &expected) {
            REQUIRE(result.HasError());
            REQUIRE(result.ErrorValue().code.Value() == expected.code.Value());
        }
    }  // namespace

    TEST_CASE("Cooked world index owns one canonical metadata record per descriptor cell", "[unit][world_streaming][cooked_manifest]") {
        auto descriptor = Descriptor();
        auto entries = Entries();
        auto result = Create(descriptor, entries);
        REQUIRE(result.HasValue());
        auto manifest = std::move(result).Value();

        entries.front().compressedSize = 1;
        entries.front().hardDependencies.clear();

        REQUIRE(manifest.Descriptor().Partition() == Partition());
        REQUIRE(manifest.Descriptor().Cells().front().package.chunkAsset == Asset(1));
        REQUIRE(manifest.Cells().size() == 3);
        REQUIRE(manifest.Cells()[0].cell == Cell(-1));
        REQUIRE(manifest.Cells()[1].cell == Cell(0));
        REQUIRE(manifest.Cells()[2].cell == Cell(1));
        REQUIRE(manifest.Cells()[2].compressedSize == 200);
        REQUIRE(manifest.Cells()[2].hardDependencies == std::vector{Cell(-1), Cell(0)});
        REQUIRE(manifest.TotalCompressedBytes() == 400);
        REQUIRE(manifest.TotalUncompressedBytes() == 600);

        static_assert(!std::is_copy_constructible_v<CookedWorldIndexManifest>);
        static_assert(std::is_move_constructible_v<CookedWorldIndexManifest>);
        static_assert(!std::is_move_assignable_v<CookedWorldIndexManifest>);
        std::vector<CookedWorldIndexManifest> storage;
        storage.push_back(std::move(manifest));
        REQUIRE(storage.front().Cells()[2].artifactHash == Hash(3));

        auto zeroDigestDescriptor = Descriptor();
        auto zeroDigestEntries = Entries();
        zeroDigestEntries.front().artifactHash = {};
        zeroDigestEntries.front().payloadCrc32 = 0;
        REQUIRE(Create(zeroDigestDescriptor, zeroDigestEntries).HasValue());
    }

    TEST_CASE("Cooked manifest identity coverage is exact and failure does not consume its descriptor",
              "[unit][world_streaming][cooked_manifest]") {
        SECTION("missing record") {
            auto descriptor = Descriptor();
            auto entries = Entries();
            entries.pop_back();
            RequireError(Create(descriptor, entries), WorldStreamingErrors::CookedManifestIdentityConflict);
            REQUIRE(descriptor.Cells().size() == 3);
        }
        SECTION("duplicate record") {
            auto descriptor = Descriptor();
            auto entries = Entries();
            entries[1].cell = entries[0].cell;
            RequireError(Create(descriptor, entries), WorldStreamingErrors::CookedManifestIdentityConflict);
            REQUIRE(descriptor.Partition() == Partition());
        }
        SECTION("extra record") {
            auto descriptor = Descriptor();
            auto entries = Entries();
            entries.push_back({Cell(2), 1, 1, 0, {}, {}});
            RequireError(Create(descriptor, entries), WorldStreamingErrors::CookedManifestIdentityConflict);
            REQUIRE(descriptor.Cells().front().id == Cell(-1));
        }
    }

    TEST_CASE("Cooked manifest validates hard dependencies without changing caller order", "[unit][world_streaming][cooked_manifest]") {
        SECTION("canonical owned dependency order") {
            auto descriptor = Descriptor();
            auto entries = Entries();
            const auto original = entries;
            auto result = Create(descriptor, entries);
            REQUIRE(result.HasValue());
            REQUIRE(entries == original);
            REQUIRE(result.Value().Cells()[2].hardDependencies == std::vector{Cell(-1), Cell(0)});
        }
        SECTION("self dependency") {
            auto descriptor = Descriptor();
            auto entries = Entries();
            entries.front().hardDependencies = {Cell(1)};
            RequireError(Create(descriptor, entries), WorldStreamingErrors::CookedManifestDependencyInvalid);
            REQUIRE(descriptor.Cells().size() == 3);
        }
        SECTION("duplicate dependency") {
            auto descriptor = Descriptor();
            auto entries = Entries();
            entries.front().hardDependencies = {Cell(0), Cell(0)};
            RequireError(Create(descriptor, entries), WorldStreamingErrors::CookedManifestDependencyInvalid);
        }
        SECTION("dependency outside manifest") {
            auto descriptor = Descriptor();
            auto entries = Entries();
            entries.front().hardDependencies = {Cell(2)};
            RequireError(Create(descriptor, entries), WorldStreamingErrors::CookedManifestDependencyInvalid);
        }
        SECTION("invalid dependency identity") {
            auto descriptor = Descriptor();
            auto entries = Entries();
            entries.front().hardDependencies = {{0, 0, 0, 0, {}}};
            RequireError(Create(descriptor, entries), WorldStreamingErrors::CookedManifestDependencyInvalid);
        }
        SECTION("total dependency limit") {
            auto descriptor = Descriptor();
            const auto entries = Entries();
            RequireError(Create(descriptor, entries, {8, 4, 1, 1024, 2048}), WorldStreamingErrors::CookedManifestCapacityExceeded);
        }
    }

    TEST_CASE("Cooked manifest applies checked count and byte ceilings", "[unit][world_streaming][cooked_manifest]") {
        SECTION("invalid limits") {
            auto descriptor = Descriptor();
            const auto entries = Entries();
            RequireError(Create(descriptor, entries, {}), WorldStreamingErrors::CookedManifestInvalid);
            REQUIRE(descriptor.Cells().size() == 3);
        }
        SECTION("cell and dependency count limits") {
            const auto entries = Entries();
            {
                auto descriptor = Descriptor();
                RequireError(Create(descriptor, entries, {2, 4, 8, 1024, 2048}), WorldStreamingErrors::CookedManifestCapacityExceeded);
            }
            {
                auto descriptor = Descriptor();
                RequireError(Create(descriptor, entries, {8, 1, 8, 1024, 2048}), WorldStreamingErrors::CookedManifestCapacityExceeded);
            }
        }
        SECTION("aggregate byte limit") {
            const auto entries = Entries();
            {
                auto descriptor = Descriptor();
                RequireError(Create(descriptor, entries, {8, 4, 8, 399, 2048}), WorldStreamingErrors::CookedManifestCapacityExceeded);
            }
            {
                auto descriptor = Descriptor();
                RequireError(Create(descriptor, entries, {8, 4, 8, 1024, 599}), WorldStreamingErrors::CookedManifestCapacityExceeded);
            }
        }
        SECTION("checked arithmetic overflow") {
            auto descriptor = Descriptor();
            auto entries = Entries();
            entries[0].compressedSize = std::numeric_limits<std::uint64_t>::max();
            entries[1].compressedSize = 1;
            RequireError(Create(descriptor, entries, {8, 4, 8, std::numeric_limits<std::uint64_t>::max(), 2048}),
                         WorldStreamingErrors::CookedManifestCapacityExceeded);
        }
        SECTION("cell archives are never zero byte") {
            {
                auto descriptor = Descriptor();
                auto entries = Entries();
                entries.front().uncompressedSize = 0;
                RequireError(Create(descriptor, entries), WorldStreamingErrors::CookedManifestInvalid);
            }
            {
                auto descriptor = Descriptor();
                auto entries = Entries();
                entries.front().compressedSize = 0;
                RequireError(Create(descriptor, entries), WorldStreamingErrors::CookedManifestInvalid);
            }
        }
    }
}  // namespace Horo::WorldStreaming
