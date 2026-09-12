#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <ranges>
#include <span>

namespace Horo::Network::Detail {
    template <std::size_t Size> [[nodiscard]] bool HasNonZeroByte(const std::array<std::byte, Size> &bytes) noexcept {
        return std::ranges::any_of(bytes, [](const std::byte value) {
            return value != std::byte{};
        });
    }

    template <typename Identity, std::size_t Capacity>
    [[nodiscard]] bool ValidCanonicalIdentities(const std::array<Identity, Capacity> &values, const std::size_t count) noexcept {
        if (count > Capacity)
            return false;
        const auto valid = std::span{values}.first(count);
        if (!std::ranges::all_of(valid, [](const Identity value) {
            return value.IsValid();
        }))
            return false;
        return std::adjacent_find(valid.begin(), valid.end(), [](const Identity left, const Identity right) {
            return left.Value() >= right.Value();
        }) == valid.end();
    }
}  // namespace Horo::Network::Detail
