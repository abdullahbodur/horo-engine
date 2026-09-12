#pragma once

#include <catch2/catch_test_macros.hpp>
#include <cstdint>

namespace Horo::Tests {
    /** @brief Forms one non-zero typed identity and fails the active test if construction regresses. */
    template <typename Identity> [[nodiscard]] Identity IdentityValue(const std::uint64_t value) {
        auto result = Identity::Create(value);
        REQUIRE(result.HasValue());
        return result.Value();
    }
}  // namespace Horo::Tests
