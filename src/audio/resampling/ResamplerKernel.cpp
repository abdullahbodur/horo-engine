#include "ResamplerKernel.h"

#include "Horo/Audio/AudioErrors.h"

#include <cmath>
#include <numbers>
#include <utility>

namespace Horo::Audio::Detail {
    namespace {
        constexpr std::uint32_t PhaseIntervals = 1024;

        /** @brief Sample a finite Hann-windowed low-pass sinc; all transcendental work is control-thread-only. */
        double WindowedSinc(const double distance, const double radius, const double cutoff) {
            if (std::abs(distance) >= radius) {
                return 0.0;
            }
            const double argument = std::numbers::pi * distance * cutoff;
            const double sinc = argument == 0.0 ? 1.0 : std::sin(argument) / argument;
            return cutoff * sinc * (0.5 + 0.5 * std::cos(std::numbers::pi * distance / radius));
        }

        /** @brief Prepare one phase with unity DC gain, including the endpoint row used for interpolation. */
        void PreparePhase(const std::span<float> row, const double fraction, const double cutoff) {
            const double radius = static_cast<double>(row.size()) / 2.0;
            double total = 0.0;
            for (std::size_t tap = 0; tap < row.size(); ++tap) {
                const double distance = static_cast<double>(tap) - radius + 1.0 - fraction;
                row[tap] = static_cast<float>(WindowedSinc(distance, radius, cutoff));
                total += row[tap];
            }
            for (auto &coefficient : row) {
                coefficient = static_cast<float>(coefficient / total);
            }
        }
    }  // namespace

    /** @copydoc ResamplerKernel::Prepare */
    Result<ResamplerKernel> ResamplerKernel::Prepare(const AudioResamplerPlan &plan, const std::uint64_t maximumBytes) {
        const auto count = static_cast<std::uint64_t>(PhaseIntervals + 1) * plan.Taps();
        if (count * sizeof(float) > maximumBytes) {
            return Result<ResamplerKernel>::Failure(MakeError(AudioErrors::ResamplerBudgetExceeded));
        }
        std::vector<float> coefficients(static_cast<std::size_t>(count));
        const double cutoff = 0.9 / std::fmax(1.0, plan.InputStep());
        for (std::uint32_t phase = 0; phase <= PhaseIntervals; ++phase) {
            const double fraction = static_cast<double>(phase) / PhaseIntervals;
            const auto row = std::span{coefficients}.subspan(static_cast<std::size_t>(phase) * plan.Taps(), plan.Taps());
            if (plan.Descriptor().quality == AudioResamplerQuality::Linear) {
                row[0] = static_cast<float>(1.0 - fraction);
                row[1] = static_cast<float>(fraction);
            } else {
                PreparePhase(row, fraction, cutoff);
            }
        }
        return Result<ResamplerKernel>::Success(ResamplerKernel{std::move(coefficients), plan.Taps()});
    }

    /** @copydoc ResamplerKernel::Evaluate */
    double ResamplerKernel::Evaluate(const std::span<const float> history, std::uint32_t oldest, const double fraction) const noexcept {
        const double position = fraction * PhaseIntervals;
        const auto phase = static_cast<std::uint32_t>(position);
        const double blend = position - phase;
        const auto offset = static_cast<std::size_t>(phase) * taps_;
        double value = 0.0;
        for (std::uint32_t tap = 0; tap < taps_; ++tap) {
            const double weight = std::lerp(static_cast<double>(coefficients_[offset + tap]),
                                            static_cast<double>(coefficients_[offset + taps_ + tap]), blend);
            value += history[oldest] * weight;
            ++oldest;
            if (oldest == taps_) {
                oldest = 0;
            }
        }
        return value;
    }

    /** @copydoc ResamplerKernel::StorageBytes */
    std::uint64_t ResamplerKernel::StorageBytes() const noexcept {
        return coefficients_.size() * sizeof(float);
    }

    /** @copydoc ResamplerKernel::ResamplerKernel */
    ResamplerKernel::ResamplerKernel(std::vector<float> coefficients, const std::uint32_t taps)
        : coefficients_(std::move(coefficients)), taps_(taps) {}
}  // namespace Horo::Audio::Detail
