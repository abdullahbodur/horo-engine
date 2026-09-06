#pragma once

#include "Horo/Foundation/ErrorCode.h"
#include "Horo/Foundation/Result.h"

#include <catch2/catch_test_macros.hpp>
#include <cstdint>

namespace Horo::Network::TestSupport {
    template <typename Identity> Identity WireIdentity(const std::uint16_t value) {
        return Identity::Create(value).Value();
    }

    template <typename T> void RequireError(const Result<T> &result, const ErrorCodeDescriptor &descriptor) {
        REQUIRE(result.HasError());
        REQUIRE(result.ErrorValue().code.Value() == descriptor.code.Value());
    }
}  // namespace Horo::Network::TestSupport
