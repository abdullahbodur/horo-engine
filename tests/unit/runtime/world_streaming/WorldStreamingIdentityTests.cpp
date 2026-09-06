#include "Horo/WorldStreaming/WorldStreamingIdentity.h"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <limits>
#include <set>
#include <string_view>
#include <type_traits>
#include <unordered_set>

namespace Horo::WorldStreaming {
    namespace {
        template <typename Identity> Identity IdentityFrom(const std::uint64_t value) {
            const auto result = Identity::Create(value);
            REQUIRE(result.HasValue());
            return result.Value();
        }

        WorldPartitionId World() {
            const auto result =
                WorldPartitionId::Create({0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff});
            REQUIRE(result.HasValue());
            return result.Value();
        }

        TEST_CASE("World streaming identities expose explicit invalid representations", "[unit][world_streaming][identity]") {
            REQUIRE_FALSE(WorldPartitionId{}.IsValid());
            REQUIRE_FALSE(StreamingLayerId{}.IsValid());
            REQUIRE_FALSE(StreamingSourceId{}.IsValid());
            REQUIRE_FALSE(PartitionEpoch{}.IsValid());
            REQUIRE_FALSE(StreamingGeneration{}.IsValid());
            REQUIRE_FALSE(StreamingCellId{}.IsValid());
            REQUIRE_FALSE(StreamingFence{}.IsValid());

            REQUIRE(WorldPartitionId::Create({}).HasError());
            REQUIRE(StreamingLayerId::Create(StreamingLayerId::InvalidValue).HasError());
            REQUIRE(StreamingSourceId::Create(0).HasError());
            REQUIRE(PartitionEpoch::Create(0).HasError());
            REQUIRE(StreamingGeneration::Create(0).HasError());
            REQUIRE(StreamingLayerId::Create(0).HasValue());

            static_assert(!std::is_convertible_v<std::uint16_t, StreamingLayerId>);
            static_assert(!std::is_convertible_v<std::uint64_t, StreamingSourceId>);
            static_assert(!std::is_same_v<PartitionEpoch, StreamingGeneration>);
        }

        TEST_CASE("Partition generations advance monotonically and never wrap", "[unit][world_streaming][identity]") {
            const auto epoch = IdentityFrom<PartitionEpoch>(41);
            const auto generation = IdentityFrom<StreamingGeneration>(99);
            REQUIRE(NextPartitionEpoch(epoch).Value().Value() == 42);
            REQUIRE(NextStreamingGeneration(generation).Value().Value() == 100);
            REQUIRE(NextPartitionEpoch({}).HasError());
            REQUIRE(NextStreamingGeneration({}).HasError());

            const auto lastEpoch = IdentityFrom<PartitionEpoch>(std::numeric_limits<std::uint64_t>::max());
            const auto lastGeneration = IdentityFrom<StreamingGeneration>(std::numeric_limits<std::uint64_t>::max());
            REQUIRE(NextPartitionEpoch(lastEpoch).ErrorValue().code.Value() == WorldStreamingErrors::GenerationExhausted.code.Value());
            REQUIRE(NextStreamingGeneration(lastGeneration).ErrorValue().code.Value() ==
                    WorldStreamingErrors::GenerationExhausted.code.Value());
        }

        TEST_CASE("World layer and source identities use canonical bytes", "[unit][world_streaming][identity]") {
            const auto world = World();
            REQUIRE(SerializeWorldPartitionId(world) == world.Bytes());
            REQUIRE(DeserializeWorldPartitionId(world.Bytes()).Value() == world);
            REQUIRE(DeserializeWorldPartitionId({}).ErrorValue().code.Value() ==
                    WorldStreamingErrors::SerializedIdentityInvalid.code.Value());

            const auto layer = StreamingLayerId::Create(0x1234).Value();
            const SerializedStreamingLayerId layerBytes{0x34, 0x12};
            REQUIRE(SerializeStreamingLayerId(layer) == layerBytes);
            REQUIRE(DeserializeStreamingLayerId(layerBytes).Value() == layer);
            REQUIRE(DeserializeStreamingLayerId({0xff, 0xff}).HasError());

            const auto source = IdentityFrom<StreamingSourceId>(0x0102030405060708ULL);
            const SerializedStreamingSourceId sourceBytes{0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01};
            REQUIRE(SerializeStreamingSourceId(source) == sourceBytes);
            REQUIRE(DeserializeStreamingSourceId(sourceBytes).Value() == source);
            REQUIRE(DeserializeStreamingSourceId({}).HasError());
        }

        TEST_CASE("Cell identity round trips the exact signed ADR-023 tuple", "[unit][world_streaming][identity]") {
            const StreamingCellId cell{
                .x = -1,
                .y = std::numeric_limits<std::int32_t>::min(),
                .z = std::numeric_limits<std::int32_t>::max(),
                .lod = 0xfe,
                .layer = StreamingLayerId::Create(0x1234).Value(),
            };
            const SerializedStreamingCellId expected{0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x80,
                                                     0xff, 0xff, 0xff, 0x7f, 0xfe, 0x34, 0x12};
            REQUIRE(SerializeStreamingCellId(cell) == expected);
            REQUIRE(DeserializeStreamingCellId(expected).Value() == cell);
            REQUIRE(DeserializeStreamingCellId({0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff}).HasError());

            REQUIRE(cell != StreamingCellId{cell.x + 1, cell.y, cell.z, cell.lod, cell.layer});
            REQUIRE(cell != StreamingCellId{cell.x, cell.y, cell.z, static_cast<std::uint8_t>(cell.lod - 1), cell.layer});
            REQUIRE(cell != StreamingCellId{cell.x, cell.y, cell.z, cell.lod, StreamingLayerId::Create(7).Value()});
        }

        TEST_CASE("Hashes include every stable identity field", "[unit][world_streaming][identity]") {
            const auto layer = StreamingLayerId::Create(3).Value();
            const StreamingCellId first{1, 2, 3, 4, layer};
            const StreamingCellId second{1, 2, 4, 4, layer};
            std::unordered_set<StreamingCellId, StreamingCellIdHash> cells{first, second};
            REQUIRE(cells.size() == 2);
            REQUIRE(cells.contains(first));
            REQUIRE(WorldPartitionIdHash{}(World()) == WorldPartitionIdHash{}(World()));
            REQUIRE(StreamingLayerIdHash{}(layer) == StreamingLayerIdHash{}(layer));
            REQUIRE(StreamingSourceIdHash{}(IdentityFrom<StreamingSourceId>(7)) ==
                    StreamingSourceIdHash{}(IdentityFrom<StreamingSourceId>(7)));
        }

        TEST_CASE("Streaming fence binds world incarnation cell and attempt", "[unit][world_streaming][identity]") {
            const StreamingFence fence{
                .partition = World(),
                .epoch = IdentityFrom<PartitionEpoch>(2),
                .cell = {1, -2, 3, 0, StreamingLayerId::Create(0).Value()},
                .generation = IdentityFrom<StreamingGeneration>(5),
            };
            REQUIRE(fence.IsValid());
            REQUIRE(fence != StreamingFence{fence.partition, fence.epoch, fence.cell, IdentityFrom<StreamingGeneration>(6)});
            REQUIRE_FALSE(StreamingFence{fence.partition, {}, fence.cell, fence.generation}.IsValid());
        }

        TEST_CASE("World streaming errors expose unique stable descriptors", "[unit][world_streaming][errors]") {
            const std::array descriptors{
                &WorldStreamingErrors::IdentityInvalid,
                &WorldStreamingErrors::SerializedIdentityInvalid,
                &WorldStreamingErrors::GenerationExhausted,
            };
            std::set<std::string_view> codes;
            for (const ErrorCodeDescriptor *descriptor : descriptors) {
                REQUIRE(descriptor->domain.Value() == "horo.world_streaming");
                REQUIRE(codes.insert(descriptor->code.Value()).second);
                REQUIRE_FALSE(descriptor->summary.empty());
                REQUIRE_FALSE(descriptor->remediationHint.empty());
            }
            REQUIRE(WorldStreamingErrors::GenerationExhausted.defaultSeverity == ErrorSeverity::Critical);
        }
    }  // namespace
}  // namespace Horo::WorldStreaming
