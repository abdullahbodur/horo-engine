#include "ResamplerSimd.h"

#include <cmath>

#if defined(__aarch64__) && defined(__ARM_NEON)
#include <arm_neon.h>
#elif defined(__SSE2__) || defined(_M_X64)
#include <emmintrin.h>
#endif

namespace Horo::Audio::Detail {
#if (defined(__aarch64__) && defined(__ARM_NEON)) || defined(__SSE2__) || defined(_M_X64)
    namespace {
        /** @brief Sum one contiguous ring segment with bounded two-lane loads and an explicit odd tail. */
        double DotSegment(const std::span<const float> samples, const float *lower, const float *upper, const double blend) noexcept {
            std::size_t index = 0;
#if defined(__aarch64__) && defined(__ARM_NEON)
            auto sum = vdupq_n_f64(0.0);
            const auto weight = vdupq_n_f64(blend);
            for (; index + 1 < samples.size(); index += 2) {
                const auto a = vcvt_f64_f32(vld1_f32(lower + index));
                const auto b = vcvt_f64_f32(vld1_f32(upper + index));
                const auto coefficient = vaddq_f64(a, vmulq_f64(vsubq_f64(b, a), weight));
                sum = vaddq_f64(sum, vmulq_f64(vcvt_f64_f32(vld1_f32(samples.data() + index)), coefficient));
            }
            double result = vgetq_lane_f64(sum, 0) + vgetq_lane_f64(sum, 1);
#else
            auto sum = _mm_setzero_pd();
            const auto weight = _mm_set1_pd(blend);
            for (; index + 1 < samples.size(); index += 2) {
                const auto a = _mm_set_pd(lower[index + 1], lower[index]);
                const auto b = _mm_set_pd(upper[index + 1], upper[index]);
                const auto coefficient = _mm_add_pd(a, _mm_mul_pd(_mm_sub_pd(b, a), weight));
                sum = _mm_add_pd(sum, _mm_mul_pd(_mm_set_pd(samples[index + 1], samples[index]), coefficient));
            }
            double result = _mm_cvtsd_f64(sum) + _mm_cvtsd_f64(_mm_unpackhi_pd(sum, sum));
#endif
            if (index < samples.size()) {
                result += samples[index] * std::lerp(static_cast<double>(lower[index]), static_cast<double>(upper[index]), blend);
            }
            return result;
        }

        /** @brief Evaluate both contiguous halves without vector loads crossing the ring boundary. */
        double EvaluateNative(const std::span<const float> history, const std::uint32_t oldest, const float *lower, const float *upper,
                              const double blend) noexcept {
            const auto first = history.subspan(oldest);
            return DotSegment(first, lower, upper, blend) +
                   DotSegment(history.first(oldest), lower + first.size(), upper + first.size(), blend);
        }
    }  // namespace
#endif

    /** @copydoc NativeResamplerEvaluator */
    ResamplerEvaluator NativeResamplerEvaluator() noexcept {
#if (defined(__aarch64__) && defined(__ARM_NEON)) || defined(__SSE2__) || defined(_M_X64)
        return &EvaluateNative;
#else
        return nullptr;
#endif
    }
}  // namespace Horo::Audio::Detail
