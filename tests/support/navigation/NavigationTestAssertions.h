#pragma once

#include "Horo/Foundation/Result.h"
#include "Horo/Foundation/Sha256.h"

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>

namespace Horo::Navigation::TestSupport {
    template <typename Identity> [[nodiscard]] Identity Id(const std::uint64_t value) {
        return Identity::Create(value).Value();
    }

    [[nodiscard]] inline Sha256Digest Digest(const std::uint8_t seed) {
        Sha256Digest digest{};
        for (std::size_t index = 0; index < digest.bytes.size(); ++index)
            digest.bytes[index] = static_cast<std::uint8_t>(seed + index);
        return digest;
    }

    template <typename T> void RequireError(const Result<T> &result, const ErrorCodeDescriptor &expected) {
        REQUIRE(result.HasError());
        REQUIRE(result.ErrorValue().domain.Value() == expected.domain.Value());
        REQUIRE(result.ErrorValue().code.Value() == expected.code.Value());
    }
}  // namespace Horo::Navigation::TestSupport
