#include "Horo/Audio/AudioResampler.h"

#include "ResamplerKernel.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace Horo::Audio {
    namespace {
        /** @brief Check the declared readable/writable prefix without accessing its samples. */
        template <typename Sample> bool ValidPlanes(const std::span<const std::span<Sample>> planes, const std::uint32_t frames) {
            return std::ranges::all_of(planes, [frames](const auto plane) {
                return plane.size() >= frames &&
                       (frames == 0 || (plane.data() != nullptr && reinterpret_cast<std::uintptr_t>(plane.data()) % 64 == 0));
            });
        }

        /** @brief Detect intersecting bounded sample prefixes using a total address representation. */
        bool Overlap(const std::span<const float> left, const std::uint32_t leftFrames, const std::span<const float> right,
                     const std::uint32_t rightFrames) {
            if (leftFrames == 0 || rightFrames == 0) {
                return false;
            }
            const auto a = reinterpret_cast<std::uintptr_t>(left.data());
            const auto b = reinterpret_cast<std::uintptr_t>(right.data());
            return a <= b ? b - a < static_cast<std::uint64_t>(leftFrames) * sizeof(float)
                          : a - b < static_cast<std::uint64_t>(rightFrames) * sizeof(float);
        }

        /** @brief Reject aliasing that could overwrite later input or another output channel. */
        bool SeparatePlanes(const AudioResamplerInput input, const AudioResamplerOutput output) {
            for (std::size_t channel = 0; channel < output.planes.size(); ++channel) {
                for (const auto source : input.planes) {
                    if (Overlap(source, input.frames, output.planes[channel], output.capacity)) {
                        return false;
                    }
                }
                for (std::size_t earlier = 0; earlier < channel; ++earlier) {
                    if (Overlap(output.planes[earlier], output.capacity, output.planes[channel], output.capacity)) {
                        return false;
                    }
                }
            }
            return true;
        }

        /** @brief Apply finite/subnormal safety without clipping ordinary internal headroom. */
        float SafeSample(const double value, std::uint32_t &faults) {
            if (!std::isfinite(value) || std::abs(value) > std::numeric_limits<float>::max()) {
                ++faults;
                return 0.0F;
            }
            return std::abs(value) < std::numeric_limits<float>::min() ? 0.0F : static_cast<float>(value);
        }
    }  // namespace

    /** @brief One owner mutates bounded relative phase and ring history; no ever-growing sample counter exists. */
    struct AudioResampler::State {
        AudioResamplerPlan plan;
        Detail::ResamplerKernel kernel;
        std::vector<float> history;
        std::uint32_t oldest{};
        std::uint32_t needed{};
        std::uint32_t padding{};
        double fraction{};
        bool draining{};
        bool seenInput{};

        /** @brief Prepare retained history outside the callback. */
        State(AudioResamplerPlan value, Detail::ResamplerKernel prepared)
            : plan(value), kernel(std::move(prepared)), history(static_cast<std::size_t>(plan.Descriptor().channels) * plan.Taps()) {
            Reset();
        }

        /** @brief Reset the timeline without changing the admitted rate, layout or quality. */
        void Reset() noexcept {
            std::ranges::fill(history, 0.0F);
            oldest = 0;
            needed = plan.Taps() / 2 + 1;
            padding = plan.Taps();
            fraction = 0.0;
            draining = false;
            seenInput = false;
        }

        /** @brief Validate the entire call's shape before any state or output mutation. */
        bool ValidCall(const AudioResamplerInput input, const AudioResamplerOutput output) const {
            const auto &descriptor = plan.Descriptor();
            return input.planes.size() == descriptor.channels && output.planes.size() == descriptor.channels &&
                   input.frames <= descriptor.maximumOutputFrames * 64U + plan.Taps() &&
                   output.capacity <= descriptor.maximumOutputFrames && ValidPlanes(input.planes, input.frames) &&
                   ValidPlanes(output.planes, output.capacity) && SeparatePlanes(input, output);
        }

        /** @brief Consume one real or padded frame into every channel's ring at the same phase. */
        void Push(const AudioResamplerInput input, AudioResamplerProgress &progress) {
            const bool real = progress.consumed < input.frames;
            for (std::size_t channel = 0; channel < input.planes.size(); ++channel) {
                const float sample = real ? input.planes[channel][progress.consumed] : 0.0F;
                history[channel * plan.Taps() + oldest] = SafeSample(sample, progress.sanitizedSamples);
            }
            oldest = (oldest + 1) % plan.Taps();
            --needed;
            if (real) {
                ++progress.consumed;
                seenInput = true;
            } else {
                --padding;
            }
        }

        /** @brief Adopt the end marker only after its entire supplied input has been consumed. */
        void ObserveEnd(const AudioResamplerInput input, const AudioResamplerProgress &progress) {
            if (input.endOfStream && progress.consumed == input.frames) {
                draining = true;
                if (!seenInput) {
                    padding = 0;
                }
            }
        }

        /** @brief Report the latched terminal state after all bounded tail padding has been consumed. */
        [[nodiscard]] bool Complete() const noexcept {
            return draining && padding == 0;
        }

        /** @brief Fill only the history needed for one output; starvation preserves all fractional state. */
        bool Fill(const AudioResamplerInput input, AudioResamplerProgress &progress) {
            ObserveEnd(input, progress);
            while (needed != 0) {
                if (progress.consumed == input.frames && (!draining || padding == 0)) {
                    return false;
                }
                Push(input, progress);
                ObserveEnd(input, progress);
            }
            return !draining || padding != 0;
        }

        /** @brief Produce one shared-phase output frame and advance only the bounded relative input position. */
        void Emit(const AudioResamplerOutput output, AudioResamplerProgress &progress) {
            for (std::size_t channel = 0; channel < output.planes.size(); ++channel) {
                const auto samples = std::span<const float>{history}.subspan(channel * plan.Taps(), plan.Taps());
                output.planes[channel][progress.produced] =
                    SafeSample(kernel.Evaluate(samples, oldest, fraction), progress.sanitizedSamples);
            }
            ++progress.produced;
            const double advanced = fraction + plan.InputStep();
            needed = static_cast<std::uint32_t>(advanced);
            fraction = advanced - needed;
        }
    };

    /** @copydoc AudioResampler::Create */
    Result<AudioResampler> AudioResampler::Create(const AudioResamplerPlan &plan, const std::uint64_t maximumCoefficientBytes,
                                                  const AudioResamplerExecution execution) {
        auto kernel = Detail::ResamplerKernel::Prepare(plan, maximumCoefficientBytes, execution);
        if (kernel.HasError()) {
            return Result<AudioResampler>::Failure(kernel.ErrorValue());
        }
        return Result<AudioResampler>::Success(AudioResampler{std::make_unique<State>(plan, std::move(kernel).Value())});
    }

    /** @copydoc AudioResampler::SupportsSimd */
    bool AudioResampler::SupportsSimd() noexcept {
        return Detail::NativeResamplerEvaluator() != nullptr;
    }

    /** @copydoc AudioResampler::AudioResampler */
    AudioResampler::AudioResampler(std::unique_ptr<State> state) : state_(std::move(state)) {}

    /** @copydoc AudioResampler::~AudioResampler */
    AudioResampler::~AudioResampler() = default;
    /** @copydoc AudioResampler::AudioResampler */
    AudioResampler::AudioResampler(AudioResampler &&) noexcept = default;
    /** @copydoc AudioResampler::operator= */
    AudioResampler &AudioResampler::operator=(AudioResampler &&) noexcept = default;

    /** @copydoc AudioResampler::Reset */
    void AudioResampler::Reset() noexcept {
        if (state_) {
            state_->Reset();
        }
    }

    /** @copydoc AudioResampler::Process */
    AudioResamplerProgress AudioResampler::Process(const AudioResamplerInput input, const AudioResamplerOutput output) noexcept {
        if (!state_ || (state_->draining && input.frames != 0)) {
            return {};
        }
        if (!state_->ValidCall(input, output)) {
            return {.status = AudioResamplerStatus::InvalidBuffer};
        }
        if (state_->Complete()) {
            return {.status = AudioResamplerStatus::Complete};
        }
        AudioResamplerProgress progress{.status = AudioResamplerStatus::OutputFull};
        while (progress.produced < output.capacity) {
            if (!state_->Fill(input, progress)) {
                progress.status = state_->Complete() ? AudioResamplerStatus::Complete : AudioResamplerStatus::InputNeeded;
                break;
            }
            state_->Emit(output, progress);
        }
        return progress;
    }
}  // namespace Horo::Audio
