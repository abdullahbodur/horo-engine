#pragma once

#include "Horo/Runtime/Save/SaveCanonicalCodec.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <ranges>
#include <span>
#include <type_traits>

namespace Horo::Runtime::CanonicalCodecDetail {
    [[nodiscard]] inline bool ValidLimits(const CanonicalCodecLimits &value) noexcept {
        return value.maximumBytes && value.maximumDecodedBytes && value.maximumStringBytes && value.maximumCollectionElements &&
               value.maximumFields && value.maximumNestingDepth && value.maximumStringBytes <= value.maximumBytes &&
               value.maximumCollectionElements <= std::numeric_limits<std::uint32_t>::max() &&
               value.maximumFields <= std::numeric_limits<std::uint32_t>::max();
    }

    template <typename Unsigned> [[nodiscard]] std::array<std::byte, sizeof(Unsigned)> ToLittleEndian(const Unsigned value) noexcept {
        static_assert(std::is_unsigned_v<Unsigned>);
        std::array<std::byte, sizeof(Unsigned)> output{};
        for (std::size_t index = 0; index < output.size(); ++index)
            output[index] = static_cast<std::byte>(value >> (index * 8U));
        return output;
    }

    template <typename Unsigned> [[nodiscard]] Unsigned FromLittleEndian(const std::array<std::byte, sizeof(Unsigned)> &bytes) noexcept {
        static_assert(std::is_unsigned_v<Unsigned>);
        Unsigned output{};
        for (std::size_t index = 0; index < bytes.size(); ++index)
            output |= static_cast<Unsigned>(std::to_integer<std::uint8_t>(bytes[index])) << (index * 8U);
        return output;
    }

    [[nodiscard]] inline bool BytesLess(const std::span<const std::byte> left, const std::span<const std::byte> right) noexcept {
        return std::ranges::lexicographical_compare(left, right);
    }

    [[nodiscard]] inline bool BytesEqual(const std::span<const std::byte> left, const std::span<const std::byte> right) noexcept {
        return std::ranges::equal(left, right);
    }

    [[nodiscard]] inline std::size_t MaximumDepth(const std::span<const CanonicalEncodedValue> values) noexcept {
        std::size_t depth{};
        for (const auto &value : values)
            depth = std::max(depth, value.StructuralDepth());
        return depth;
    }
}  // namespace Horo::Runtime::CanonicalCodecDetail
