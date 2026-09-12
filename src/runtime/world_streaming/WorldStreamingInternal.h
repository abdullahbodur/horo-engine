#pragma once

#include "Horo/Foundation/Result.h"

#include <cstdint>
#include <limits>

namespace Horo::WorldStreaming::Internal {
    template <typename T> [[nodiscard]] Result<T> Failure(const ErrorCodeDescriptor &descriptor) {
        return Result<T>::Failure(MakeError(descriptor));
    }

    [[nodiscard]] inline std::uint64_t UnsignedMagnitude(const std::int64_t value) noexcept {
        return value >= 0 ? static_cast<std::uint64_t>(value) : static_cast<std::uint64_t>(-(value + 1)) + 1U;
    }

    [[nodiscard]] inline bool CheckedAdd(const std::int64_t left, const std::int64_t right, std::int64_t &sum) noexcept {
        if ((right > 0 && left > std::numeric_limits<std::int64_t>::max() - right) ||
            (right < 0 && left < std::numeric_limits<std::int64_t>::min() - right))
            return false;
        sum = left + right;
        return true;
    }
}  // namespace Horo::WorldStreaming::Internal
