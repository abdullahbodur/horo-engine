#pragma once

#include <cstdint>
#include <functional>
#include <span>

namespace Horo::Audio::Detail {
    /** @brief Prepared implementation callable; history, ring index and coefficient rows are prevalidated. */
    using ResamplerEvaluator =
        std::function<double(std::span<const float> history, std::uint32_t oldest, const float *lower, const float *upper, double blend)>;
    /** @brief Resolve baseline compiled SIMD without probing on the callback. @return Native evaluator, or null if unavailable. */
    [[nodiscard]] ResamplerEvaluator NativeResamplerEvaluator() noexcept;
}  // namespace Horo::Audio::Detail
