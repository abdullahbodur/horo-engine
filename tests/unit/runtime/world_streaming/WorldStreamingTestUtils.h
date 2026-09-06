#pragma once

#include <catch2/catch_test_macros.hpp>
#include <cstdint>

namespace Horo::WorldStreaming::TestSupport {
    template <typename Identity> Identity IdentityFrom(const std::uint64_t value) {
        const auto result = Identity::Create(value);
        REQUIRE(result.HasValue());
        return result.Value();
    }
}  // namespace Horo::WorldStreaming::TestSupport
