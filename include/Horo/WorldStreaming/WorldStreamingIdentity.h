#pragma once

/**
 * @file WorldStreamingIdentity.h
 * @brief Stable world, cell, layer, source, epoch, and generation identities.
 */

#include "Horo/Foundation/StrongId.h"
#include "Horo/WorldStreaming/WorldStreamingErrors.h"

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace Horo::WorldStreaming {
    namespace Detail {
        /** @brief Tag that keeps stable source identities distinct from other non-zero counters. */
        struct StreamingSourceIdTag;
        /** @brief Tag that keeps partition epochs distinct from other non-zero counters. */
        struct PartitionEpochTag;
        /** @brief Tag that keeps streaming generations distinct from other non-zero counters. */
        struct StreamingGenerationTag;

    }  // namespace Detail

    /** @brief Canonical 16-byte world GUID representation used by world.index. */
    using SerializedWorldPartitionId = std::array<std::uint8_t, 16>;
    /** @brief Canonical little-endian representation of a streaming cell tuple. */
    using SerializedStreamingCellId = std::array<std::uint8_t, 15>;
    /** @brief Canonical little-endian representation of a layer identity. */
    using SerializedStreamingLayerId = std::array<std::uint8_t, 2>;
    /** @brief Canonical little-endian representation of a stable source identity. */
    using SerializedStreamingSourceId = std::array<std::uint8_t, 8>;

    /** @brief Stable world.index worldGuid identity; the all-zero GUID is reserved as invalid. */
    class WorldPartitionId final {
    public:
        /** @brief Constructs the reserved invalid world identity. */
        WorldPartitionId() = default;

        /**
         * @brief Validates canonical world GUID bytes.
         * @param bytes Exact persistent worldGuid byte sequence.
         * @return Stable identity or WorldStreamingErrors::IdentityInvalid for the all-zero value.
         */
        [[nodiscard]] static Result<WorldPartitionId> Create(const SerializedWorldPartitionId &bytes);

        /** @brief Returns the exact persistent byte sequence. @return Borrowed bytes owned by this value. */
        [[nodiscard]] const SerializedWorldPartitionId &Bytes() const noexcept;
        /** @brief Checks representation, not whether the partition is mounted. @return True unless all bytes are zero. */
        [[nodiscard]] bool IsValid() const noexcept;

        [[nodiscard]] constexpr auto operator<=>(const WorldPartitionId &) const noexcept = default;

    private:
        explicit constexpr WorldPartitionId(const SerializedWorldPartitionId &bytes) noexcept : bytes_(bytes) {}

        SerializedWorldPartitionId bytes_{};
    };

    /** @brief Typed manifest layer identity; zero is valid and 0xffff is the reserved invalid value. */
    class StreamingLayerId final {
    public:
        /** @brief Reserved invalid layer value, outside the supported manifest identity range. */
        static constexpr std::uint16_t InvalidValue = std::numeric_limits<std::uint16_t>::max();

        /** @brief Constructs the reserved invalid layer identity. */
        StreamingLayerId() = default;

        /**
         * @brief Validates a manifest layer identity.
         * @param value Layer value; zero is valid and InvalidValue is reserved.
         * @return Typed identity or WorldStreamingErrors::IdentityInvalid.
         */
        [[nodiscard]] static Result<StreamingLayerId> Create(std::uint16_t value);

        /** @brief Returns the manifest value. @return InvalidValue only for an invalid identity. */
        [[nodiscard]] constexpr std::uint16_t Value() const noexcept {
            return value_;
        }

        /** @brief Checks representation; manifest membership is validated separately. @return Whether the value is not reserved. */
        [[nodiscard]] constexpr bool IsValid() const noexcept {
            return value_ != InvalidValue;
        }

        [[nodiscard]] constexpr auto operator<=>(const StreamingLayerId &) const noexcept = default;

    private:
        explicit constexpr StreamingLayerId(const std::uint16_t value) noexcept : value_(value) {}

        std::uint16_t value_{InvalidValue};
    };

    /** @brief Stable host-authored streaming relevance source identity; zero is reserved as invalid. */
    using StreamingSourceId = Foundation::Detail::NonZeroId64<Detail::StreamingSourceIdTag, WorldStreamingErrors::IdentityInvalid>;

    /** @brief Mounted partition incarnation; zero is invalid and issued values never wrap or repeat. */
    using PartitionEpoch = Foundation::Detail::NonZeroId64<Detail::PartitionEpochTag, WorldStreamingErrors::IdentityInvalid>;

    /** @brief One cell residency attempt; zero is invalid and issued values never wrap or repeat. */
    using StreamingGeneration = Foundation::Detail::NonZeroId64<Detail::StreamingGenerationTag, WorldStreamingErrors::IdentityInvalid>;

    /** @brief Exact ADR-023 cell tuple within one partition. */
    struct StreamingCellId final {
        std::int32_t x{};         /**< Signed grid X; negative cells preserve floor-quantized identity. */
        std::int32_t y{};         /**< Signed grid Y; negative cells preserve floor-quantized identity. */
        std::int32_t z{};         /**< Signed grid Z; negative cells preserve floor-quantized identity. */
        std::uint8_t lod{};       /**< Manifest LOD level; descriptor membership is checked separately. */
        StreamingLayerId layer{}; /**< Typed manifest layer identity. */

        /** @brief Checks representation, not manifest bounds or membership. @return Whether the layer representation is valid. */
        [[nodiscard]] constexpr bool IsValid() const noexcept {
            return layer.IsValid();
        }

        [[nodiscard]] constexpr auto operator<=>(const StreamingCellId &) const noexcept = default;
    };

    /** @brief Complete fence for one cell attempt in one mounted partition incarnation. */
    struct StreamingFence final {
        WorldPartitionId partition;     /**< Stable worldGuid identity. */
        PartitionEpoch epoch;           /**< Exact mounted incarnation. */
        StreamingCellId cell;           /**< Exact persistent spatial tuple. */
        StreamingGeneration generation; /**< Exact residency attempt. */

        /** @brief Checks fence representation, not current authority state. @return True when every component is valid. */
        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] constexpr auto operator<=>(const StreamingFence &) const noexcept = default;
    };

    /** @brief Hash adapter for WorldPartitionId. */
    struct WorldPartitionIdHash final {
        /** @brief Hashes canonical GUID bytes. @param value World identity. @return Deterministic FNV-1a hash. */
        [[nodiscard]] std::size_t operator()(const WorldPartitionId &value) const noexcept;
    };

    /** @brief Hash adapter for StreamingCellId. */
    struct StreamingCellIdHash final {
        /** @brief Hashes the exact canonical cell tuple. @param value Cell identity. @return Deterministic FNV-1a hash. */
        [[nodiscard]] std::size_t operator()(const StreamingCellId &value) const noexcept;
    };

    /** @brief Hash adapter for StreamingLayerId. */
    struct StreamingLayerIdHash final {
        /** @brief Hashes the canonical layer bytes. @param value Layer identity. @return Deterministic FNV-1a hash. */
        [[nodiscard]] std::size_t operator()(StreamingLayerId value) const noexcept;
    };

    /** @brief Hash adapter for StreamingSourceId. */
    struct StreamingSourceIdHash final {
        /** @brief Hashes canonical source bytes. @param value Source identity. @return Deterministic FNV-1a hash. */
        [[nodiscard]] std::size_t operator()(StreamingSourceId value) const noexcept;
    };

    /** @brief Returns the next non-wrapping partition epoch. @param current Current valid epoch. @return Next epoch or GenerationExhausted.
     */
    [[nodiscard]] Result<PartitionEpoch> NextPartitionEpoch(PartitionEpoch current);
    /** @brief Returns the next non-wrapping cell-attempt generation. @param current Current valid generation. @return Next generation or
     * GenerationExhausted. */
    [[nodiscard]] Result<StreamingGeneration> NextStreamingGeneration(StreamingGeneration current);

    /** @brief Encodes exact worldGuid bytes. @param value World identity. @return Canonical 16-byte representation. */
    [[nodiscard]] SerializedWorldPartitionId SerializeWorldPartitionId(const WorldPartitionId &value) noexcept;
    /** @brief Decodes and validates exact worldGuid bytes. @param bytes Canonical bytes. @return Typed world identity or
     * SerializedIdentityInvalid. */
    [[nodiscard]] Result<WorldPartitionId> DeserializeWorldPartitionId(const SerializedWorldPartitionId &bytes);
    /** @brief Encodes a cell tuple in ADR-023 little-endian field order. @param value Valid cell identity. @return Canonical 15-byte
     * representation. */
    [[nodiscard]] SerializedStreamingCellId SerializeStreamingCellId(const StreamingCellId &value) noexcept;
    /** @brief Decodes and validates an ADR-023 cell tuple. @param bytes Canonical bytes. @return Typed cell identity or
     * SerializedIdentityInvalid. */
    [[nodiscard]] Result<StreamingCellId> DeserializeStreamingCellId(const SerializedStreamingCellId &bytes);
    /** @brief Encodes a layer identity as little-endian uint16. @param value Layer identity. @return Canonical bytes. */
    [[nodiscard]] SerializedStreamingLayerId SerializeStreamingLayerId(StreamingLayerId value) noexcept;
    /** @brief Decodes and validates a little-endian layer identity. @param bytes Canonical bytes. @return Typed layer or
     * SerializedIdentityInvalid. */
    [[nodiscard]] Result<StreamingLayerId> DeserializeStreamingLayerId(const SerializedStreamingLayerId &bytes);
    /** @brief Encodes a source identity as little-endian uint64. @param value Source identity. @return Canonical bytes. */
    [[nodiscard]] SerializedStreamingSourceId SerializeStreamingSourceId(StreamingSourceId value) noexcept;
    /** @brief Decodes and validates a little-endian source identity. @param bytes Canonical bytes. @return Typed source or
     * SerializedIdentityInvalid. */
    [[nodiscard]] Result<StreamingSourceId> DeserializeStreamingSourceId(const SerializedStreamingSourceId &bytes);
}  // namespace Horo::WorldStreaming
