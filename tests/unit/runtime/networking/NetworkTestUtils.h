#pragma once

#include "Horo/Foundation/ErrorCode.h"
#include "Horo/Foundation/Result.h"
#include "Horo/Network/NetworkLifecycle.h"

#include <catch2/catch_test_macros.hpp>
#include <cstdint>

namespace Horo::Network::TestSupport {
    /** @brief Returns the shared generation-tagged connection used by session-layer tests. */
    inline ConnectionHandle Connection(const std::uint32_t generation = 3) {
        return ConnectionHandle::Create(2, generation).Value();
    }

    /** @brief Returns the shared non-zero session operation generation used by session-layer tests. */
    inline NetworkOperationGeneration Session(const std::uint64_t generation = 7) {
        return NetworkOperationGeneration::Create(generation).Value();
    }

    template <typename Identity> Identity WireIdentity(const std::uint16_t value) {
        return Identity::Create(value).Value();
    }

    template <typename T> void RequireError(const Result<T> &result, const ErrorCodeDescriptor &descriptor) {
        REQUIRE(result.HasError());
        REQUIRE(result.ErrorValue().code.Value() == descriptor.code.Value());
    }
}  // namespace Horo::Network::TestSupport
