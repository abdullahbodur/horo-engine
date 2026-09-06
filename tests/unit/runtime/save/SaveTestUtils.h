#pragma once

#include <array>
#include <cstdint>

namespace Horo::Runtime::Test {
    template <typename Identity> Identity Id(const std::uint8_t suffix) {
        std::array<std::uint8_t, 16> bytes{};
        bytes.back() = suffix;
        return Identity::FromBytes(bytes).Value();
    }

    template <typename Version> Version V(const std::uint32_t value) {
        return Version::Create(value).Value();
    }
}  // namespace Horo::Runtime::Test
