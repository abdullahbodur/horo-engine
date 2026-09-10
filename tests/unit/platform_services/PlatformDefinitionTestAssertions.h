#pragma once

#include "Horo/Foundation/ErrorCode.h"

#include <catch2/catch_test_macros.hpp>
#include <string_view>

namespace Horo::PlatformServices::TestAssertions {
    inline void CheckError(const auto &result, const ErrorCodeDescriptor &descriptor) {
        REQUIRE(result.HasError());
        CHECK(result.ErrorValue().code.Value() == descriptor.code.Value());
    }

    inline void CheckFieldError(const auto &result, const ErrorCodeDescriptor &descriptor, const std::string_view field) {
        CheckError(result, descriptor);
        REQUIRE(result.ErrorValue().diagnostics.size() == 1);
        CHECK(result.ErrorValue().diagnostics.front().location.source == field);
    }
}  // namespace Horo::PlatformServices::TestAssertions
