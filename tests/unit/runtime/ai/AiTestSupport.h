#pragma once

#include "Horo/Foundation/Result.h"

#include <catch2/catch_test_macros.hpp>
#include <cstdint>

namespace Horo::AI::TestSupport {
    template <typename Identity> [[nodiscard]] Identity MakeIdentity(const std::uint64_t value) {
        const auto identity = Identity::Create(value);
        REQUIRE(identity.HasValue());
        return identity.Value();
    }

    template <typename T> void ExpectError(const Result<T> &result, const ErrorCodeDescriptor &descriptor) {
        REQUIRE(result.HasError());
        CHECK(result.ErrorValue().code.Value() == descriptor.code.Value());
    }
}  // namespace Horo::AI::TestSupport
