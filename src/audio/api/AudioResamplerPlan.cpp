#include "Horo/Audio/AudioResamplerPlan.h"

#include "Horo/Audio/AudioErrors.h"

#include <array>
#include <cmath>

namespace Horo::Audio {
    namespace {
        /** @brief Expand sinc support for downsampling while retaining the two-tap linear kernel. */
        std::uint32_t KernelWidth(const AudioResamplerQuality quality, const double step) {
            constexpr std::array<std::uint32_t, 3> widths{2, 32, 64};
            const auto stretch =
                quality == AudioResamplerQuality::Linear ? 1U : static_cast<std::uint32_t>(std::ceil(std::fmax(1.0, step)));
            return widths[static_cast<std::size_t>(quality)] * stretch;
        }

        /** @brief Admit the baseline explicit source/device sample-rate range. */
        bool ValidRate(const std::uint32_t rate) {
            return rate >= 8'000 && rate <= 384'000;
        }

        /** @brief Bound descriptor arithmetic and reject unknown stage/quality enumerators. */
        bool ValidShape(const AudioResamplerDescriptor &descriptor) {
            return descriptor.stage <= AudioResamplerStage::MixToDevice && descriptor.quality <= AudioResamplerQuality::Sinc64 &&
                   ValidRate(descriptor.inputRate) && ValidRate(descriptor.outputRate) && descriptor.channels > 0 &&
                   descriptor.channels <= 64 && descriptor.maximumOutputFrames > 0 && descriptor.maximumOutputFrames <= 16'384;
        }

        /** @brief Compare fully computed requirements with caller-owned reservations. */
        bool FitsBudget(const AudioResamplerBudget &required, const AudioResamplerBudget &budget) {
            return required.maximumSampleProducts <= budget.maximumSampleProducts &&
                   required.maximumHistoryBytes <= budget.maximumHistoryBytes &&
                   required.maximumLookAheadFrames <= budget.maximumLookAheadFrames;
        }
    }  // namespace

    /** @copydoc AudioResamplerPlan::Prepare */
    Result<AudioResamplerPlan> AudioResamplerPlan::Prepare(const AudioResamplerDescriptor &descriptor, const AudioResamplerBudget &budget) {
        if (!ValidShape(descriptor) || !(descriptor.pitch >= 0.125 && descriptor.pitch <= 8.0)) {
            return Result<AudioResamplerPlan>::Failure(MakeError(AudioErrors::ResamplerInvalid));
        }
        if (descriptor.playbackSpeed != 1.0 || (descriptor.stage == AudioResamplerStage::MixToDevice && descriptor.pitch != 1.0)) {
            return Result<AudioResamplerPlan>::Failure(MakeError(AudioErrors::OperationUnsupported));
        }
        const double step = static_cast<double>(descriptor.inputRate) / descriptor.outputRate * descriptor.pitch;
        if (step < 1.0 / 64.0 || step > 64.0) {
            return Result<AudioResamplerPlan>::Failure(MakeError(AudioErrors::ResamplerInvalid));
        }
        const auto taps = KernelWidth(descriptor.quality, step);
        const auto channels = static_cast<std::uint64_t>(descriptor.channels);
        const AudioResamplerBudget required{
            .maximumSampleProducts = channels * descriptor.maximumOutputFrames * taps,
            .maximumHistoryBytes = channels * taps * sizeof(float),
            .maximumLookAheadFrames = taps / 2,
        };
        if (!FitsBudget(required, budget)) {
            return Result<AudioResamplerPlan>::Failure(MakeError(AudioErrors::ResamplerBudgetExceeded));
        }
        return Result<AudioResamplerPlan>::Success(AudioResamplerPlan{descriptor, step, taps, required});
    }

    /** @copydoc AudioResamplerPlan::AudioResamplerPlan */
    AudioResamplerPlan::AudioResamplerPlan(AudioResamplerDescriptor descriptor, const double step, const std::uint32_t taps,
                                           AudioResamplerBudget requirements)
        : descriptor_(descriptor), step_(step), taps_(taps), requirements_(requirements) {}

    /** @copydoc AudioResamplerPlan::Descriptor */
    const AudioResamplerDescriptor &AudioResamplerPlan::Descriptor() const noexcept {
        return descriptor_;
    }

    /** @copydoc AudioResamplerPlan::InputStep */
    double AudioResamplerPlan::InputStep() const noexcept {
        return step_;
    }

    /** @copydoc AudioResamplerPlan::Taps */
    std::uint32_t AudioResamplerPlan::Taps() const noexcept {
        return taps_;
    }

    /** @copydoc AudioResamplerPlan::Requirements */
    const AudioResamplerBudget &AudioResamplerPlan::Requirements() const noexcept {
        return requirements_;
    }
}  // namespace Horo::Audio
