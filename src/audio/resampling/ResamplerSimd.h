#pragma once

#include <cstdint>
#include <span>

namespace Horo::Audio::Detail {
    /** @brief Prepared implementation pointer; history, ring index and coefficient rows are prevalidated. */
    using ResamplerEvaluator = double (*)(std::span<const float> history, std::uint32_t oldest, const float *lower, const float *upper,
                                          double blend) noexcept;
    /** @brief Resolve baseline compiled SIMD without probing on the callback. @return Native evaluator, or null if unavailable. */
    [[nodiscard]] ResamplerEvaluator NativeResamplerEvaluator() noexcept;
}  // namespace Horo::Audio::Detail
