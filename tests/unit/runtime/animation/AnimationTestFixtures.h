#pragma once

#include "Horo/Assets/AssetId.h"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>

/** @brief Returns allocations observed by the animation API test executable. */
[[nodiscard]] std::size_t HoroAnimationTestAllocationCount() noexcept;

namespace Horo::Animation::Test {
    template <typename Identity> Identity Id(const std::uint64_t value) {
        auto created = Identity::Create(value);
        REQUIRE(created.HasValue());
        return created.Value();
    }

    template <typename Identity> Identity Asset(const std::uint8_t suffix) {
        std::array<std::uint8_t, 16> bytes{};
        bytes.back() = suffix;
        auto created = Identity::Create(Assets::AssetId::FromBytes(bytes));
        REQUIRE(created.HasValue());
        return created.Value();
    }
}  // namespace Horo::Animation::Test
