#include "Horo/WorldStreaming/WorldStreamingIdentity.h"

#include <algorithm>
#include <bit>
#include <span>

namespace Horo::WorldStreaming {
    namespace {
        constexpr std::uint64_t FnvOffset = 14695981039346656037ULL;
        constexpr std::uint64_t FnvPrime = 1099511628211ULL;

        [[nodiscard]] std::size_t HashBytes(const std::span<const std::uint8_t> bytes) noexcept {
            std::uint64_t hash = FnvOffset;
            for (const std::uint8_t byte : bytes) {
                hash ^= byte;
                hash *= FnvPrime;
            }
            return static_cast<std::size_t>(hash);
        }

        void StoreLittleEndian32(SerializedStreamingCellId &bytes, const std::size_t offset, const std::uint32_t value) noexcept {
            for (std::size_t index = 0; index < sizeof(value); ++index)
                bytes[offset + index] = static_cast<std::uint8_t>(value >> (index * 8U));
        }

        [[nodiscard]] std::uint32_t LoadLittleEndian32(const SerializedStreamingCellId &bytes, const std::size_t offset) noexcept {
            std::uint32_t value{};
            for (std::size_t index = 0; index < sizeof(value); ++index)
                value |= static_cast<std::uint32_t>(bytes[offset + index]) << (index * 8U);
            return value;
        }

        template <std::size_t Size, typename Unsigned>
        [[nodiscard]] std::array<std::uint8_t, Size> SerializeUnsignedLittleEndian(const Unsigned value) noexcept {
            std::array<std::uint8_t, Size> bytes{};
            for (std::size_t index = 0; index < Size; ++index)
                bytes[index] = static_cast<std::uint8_t>(value >> (index * 8U));
            return bytes;
        }

        template <typename Unsigned, std::size_t Size>
        [[nodiscard]] Unsigned DeserializeUnsignedLittleEndian(const std::array<std::uint8_t, Size> &bytes) noexcept {
            Unsigned value{};
            for (std::size_t index = 0; index < Size; ++index)
                value |= static_cast<Unsigned>(bytes[index]) << (index * 8U);
            return value;
        }

        template <typename Identity> [[nodiscard]] Result<Identity> DeserializeChecked(const auto &bytes) {
            const auto parsed = Identity::Create(DeserializeUnsignedLittleEndian<std::uint64_t>(bytes));
            if (parsed.HasError())
                return Result<Identity>::Failure(MakeError(WorldStreamingErrors::SerializedIdentityInvalid));
            return parsed;
        }
    }  // namespace

    /** @copydoc WorldPartitionId::Create */
    Result<WorldPartitionId> WorldPartitionId::Create(const SerializedWorldPartitionId &bytes) {
        if (std::ranges::all_of(bytes, [](const std::uint8_t value) {
            return value == 0;
        }))
            return Result<WorldPartitionId>::Failure(MakeError(WorldStreamingErrors::IdentityInvalid));
        return Result<WorldPartitionId>::Success(WorldPartitionId{bytes});
    }

    /** @copydoc WorldPartitionId::Bytes */
    const SerializedWorldPartitionId &WorldPartitionId::Bytes() const noexcept {
        return bytes_;
    }

    /** @copydoc WorldPartitionId::IsValid */
    bool WorldPartitionId::IsValid() const noexcept {
        return std::ranges::any_of(bytes_, [](const std::uint8_t value) {
            return value != 0;
        });
    }

    /** @copydoc StreamingLayerId::Create */
    Result<StreamingLayerId> StreamingLayerId::Create(const std::uint16_t value) {
        if (value == InvalidValue)
            return Result<StreamingLayerId>::Failure(MakeError(WorldStreamingErrors::IdentityInvalid));
        return Result<StreamingLayerId>::Success(StreamingLayerId{value});
    }

    /** @copydoc StreamingFence::IsValid */
    bool StreamingFence::IsValid() const noexcept {
        return partition.IsValid() && epoch.IsValid() && cell.IsValid() && generation.IsValid();
    }

    /** @copydoc WorldPartitionIdHash::operator() */
    std::size_t WorldPartitionIdHash::operator()(const WorldPartitionId &value) const noexcept {
        return HashBytes(value.Bytes());
    }

    /** @copydoc StreamingCellIdHash::operator() */
    std::size_t StreamingCellIdHash::operator()(const StreamingCellId &value) const noexcept {
        const auto bytes = SerializeStreamingCellId(value);
        return HashBytes(bytes);
    }

    /** @copydoc StreamingLayerIdHash::operator() */
    std::size_t StreamingLayerIdHash::operator()(const StreamingLayerId value) const noexcept {
        const auto bytes = SerializeStreamingLayerId(value);
        return HashBytes(bytes);
    }

    /** @copydoc StreamingSourceIdHash::operator() */
    std::size_t StreamingSourceIdHash::operator()(const StreamingSourceId value) const noexcept {
        const auto bytes = SerializeStreamingSourceId(value);
        return HashBytes(bytes);
    }

    /** @copydoc NextPartitionEpoch */
    Result<PartitionEpoch> NextPartitionEpoch(const PartitionEpoch current) {
        if (!current.IsValid())
            return Result<PartitionEpoch>::Failure(MakeError(WorldStreamingErrors::IdentityInvalid));
        if (current.Value() == std::numeric_limits<std::uint64_t>::max())
            return Result<PartitionEpoch>::Failure(MakeError(WorldStreamingErrors::GenerationExhausted));
        return PartitionEpoch::Create(current.Value() + 1);
    }

    /** @copydoc NextStreamingGeneration */
    Result<StreamingGeneration> NextStreamingGeneration(const StreamingGeneration current) {
        if (!current.IsValid())
            return Result<StreamingGeneration>::Failure(MakeError(WorldStreamingErrors::IdentityInvalid));
        if (current.Value() == std::numeric_limits<std::uint64_t>::max())
            return Result<StreamingGeneration>::Failure(MakeError(WorldStreamingErrors::GenerationExhausted));
        return StreamingGeneration::Create(current.Value() + 1);
    }

    /** @copydoc SerializeWorldPartitionId */
    SerializedWorldPartitionId SerializeWorldPartitionId(const WorldPartitionId &value) noexcept {
        return value.Bytes();
    }

    /** @copydoc DeserializeWorldPartitionId */
    Result<WorldPartitionId> DeserializeWorldPartitionId(const SerializedWorldPartitionId &bytes) {
        const auto parsed = WorldPartitionId::Create(bytes);
        if (parsed.HasError())
            return Result<WorldPartitionId>::Failure(MakeError(WorldStreamingErrors::SerializedIdentityInvalid));
        return parsed;
    }

    /** @copydoc SerializeStreamingCellId */
    SerializedStreamingCellId SerializeStreamingCellId(const StreamingCellId &value) noexcept {
        SerializedStreamingCellId bytes{};
        StoreLittleEndian32(bytes, 0, std::bit_cast<std::uint32_t>(value.x));
        StoreLittleEndian32(bytes, 4, std::bit_cast<std::uint32_t>(value.y));
        StoreLittleEndian32(bytes, 8, std::bit_cast<std::uint32_t>(value.z));
        bytes[12] = value.lod;
        const auto layer = SerializeStreamingLayerId(value.layer);
        bytes[13] = layer[0];
        bytes[14] = layer[1];
        return bytes;
    }

    /** @copydoc DeserializeStreamingCellId */
    Result<StreamingCellId> DeserializeStreamingCellId(const SerializedStreamingCellId &bytes) {
        const SerializedStreamingLayerId layerBytes{bytes[13], bytes[14]};
        const auto layer = DeserializeStreamingLayerId(layerBytes);
        if (layer.HasError())
            return Result<StreamingCellId>::Failure(MakeError(WorldStreamingErrors::SerializedIdentityInvalid));
        return Result<StreamingCellId>::Success({
            .x = std::bit_cast<std::int32_t>(LoadLittleEndian32(bytes, 0)),
            .y = std::bit_cast<std::int32_t>(LoadLittleEndian32(bytes, 4)),
            .z = std::bit_cast<std::int32_t>(LoadLittleEndian32(bytes, 8)),
            .lod = bytes[12],
            .layer = layer.Value(),
        });
    }

    /** @copydoc SerializeStreamingLayerId */
    SerializedStreamingLayerId SerializeStreamingLayerId(const StreamingLayerId value) noexcept {
        return SerializeUnsignedLittleEndian<2>(value.Value());
    }

    /** @copydoc DeserializeStreamingLayerId */
    Result<StreamingLayerId> DeserializeStreamingLayerId(const SerializedStreamingLayerId &bytes) {
        const auto parsed = StreamingLayerId::Create(DeserializeUnsignedLittleEndian<std::uint16_t>(bytes));
        if (parsed.HasError())
            return Result<StreamingLayerId>::Failure(MakeError(WorldStreamingErrors::SerializedIdentityInvalid));
        return parsed;
    }

    /** @copydoc SerializeStreamingSourceId */
    SerializedStreamingSourceId SerializeStreamingSourceId(const StreamingSourceId value) noexcept {
        return SerializeUnsignedLittleEndian<8>(value.Value());
    }

    /** @copydoc DeserializeStreamingSourceId */
    Result<StreamingSourceId> DeserializeStreamingSourceId(const SerializedStreamingSourceId &bytes) {
        return DeserializeChecked<StreamingSourceId>(bytes);
    }
}  // namespace Horo::WorldStreaming
