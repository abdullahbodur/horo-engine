#pragma once

#include "Horo/Foundation/Result.h"

#include <catch2/catch_test_macros.hpp>

namespace Horo::Navigation::TestSupport {
    template <typename T> void RequireError(const Result<T> &result, const ErrorCodeDescriptor &expected) {
        REQUIRE(result.HasError());
        REQUIRE(result.ErrorValue().domain.Value() == expected.domain.Value());
        REQUIRE(result.ErrorValue().code.Value() == expected.code.Value());
    }
}  // namespace Horo::Navigation::TestSupport
