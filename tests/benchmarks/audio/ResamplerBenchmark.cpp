#include "Horo/Audio/AudioResampler.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <ranges>
#include <stdexcept>
#include <vector>

namespace {
    using namespace Horo::Audio;
    constexpr std::uint32_t Frames = 256;
    constexpr std::size_t Iterations = 1000;

    struct alignas(64) Plane {
        std::array<float, 2048> samples{};
    };

    /** @brief Report the distribution and enforce this fixture's per-instance P99 qualification envelope. */
    void Report(const AudioResamplerPlan &plan, const AudioResamplerExecution execution, std::vector<double> &elapsed,
                const double checksum) {
        const double mean = std::accumulate(elapsed.begin(), elapsed.end(), 0.0) / Iterations;
        std::ranges::sort(elapsed);
        const double p99 = elapsed[Iterations * 99 / 100];
        const double deadline = 1'000'000.0 * Frames / 48'000.0;
        const auto &descriptor = plan.Descriptor();
        std::cout << static_cast<unsigned>(descriptor.quality) << ',' << static_cast<unsigned>(execution) << ',' << descriptor.channels
                  << ',' << descriptor.inputRate << ',' << plan.Taps() << ',' << mean << ',' << p99 << ',' << elapsed.back() << ','
                  << p99 / deadline * 100.0 << ',' << checksum << '\n';
        if (p99 > deadline * 0.1) {
            throw std::runtime_error("Measured P99 exceeds this fixture's 10% callback budget");
        }
    }

    /** @brief Measure complete Process calls, excluding preparation, input generation and result formatting. */
    void Measure(const AudioResamplerQuality quality, const AudioResamplerExecution execution, const std::uint32_t channels,
                 const std::uint32_t inputRate) {
        const auto plan = AudioResamplerPlan::Prepare({.quality = quality,
                                                       .inputRate = inputRate,
                                                       .outputRate = 48'000,
                                                       .channels = channels,
                                                       .maximumOutputFrames = Frames},
                                                      {1ULL << 34, 1ULL << 24, 4096});
        if (plan.HasError()) {
            throw std::runtime_error("Benchmark plan admission failed");
        }
        auto prepared = AudioResampler::Create(plan.Value(), 32ULL << 20, execution);
        if (prepared.HasError()) {
            throw std::runtime_error("Benchmark execution preparation failed");
        }
        auto processor = std::move(prepared).Value();
        std::array<Plane, 8> inputs;
        std::array<Plane, 8> outputs;
        std::array<std::span<const float>, 8> sources;
        std::array<std::span<float>, 8> destinations;
        std::ranges::for_each(std::views::iota(std::size_t{0}, static_cast<std::size_t>(channels)), [&](const std::size_t channel) {
            std::ranges::transform(std::views::iota(std::size_t{0}, inputs[channel].samples.size()), inputs[channel].samples.begin(),
                                   [channel](const std::size_t frame) {
                return static_cast<float>(std::sin(static_cast<double>(frame) * 0.013 + channel));
            });
            sources[channel] = inputs[channel].samples;
            destinations[channel] = outputs[channel].samples;
        });
        const auto offered = Frames * static_cast<std::uint32_t>(std::ceil(plan.Value().InputStep())) + plan.Value().Taps();
        const AudioResamplerInput input{std::span{sources}.first(channels), offered, false};
        const AudioResamplerOutput output{std::span{destinations}.first(channels), Frames};
        std::ranges::for_each(std::views::iota(0U, 100U), [&](const unsigned) {
            static_cast<void>(processor.Process(input, output));
        });
        std::vector<double> elapsed(Iterations);
        double checksum = 0.0;
        std::ranges::for_each(elapsed, [&](double &duration) {
            const auto start = std::chrono::steady_clock::now();
            const auto result = processor.Process(input, output);
            const auto finish = std::chrono::steady_clock::now();
            const std::array resultIsValid{result.produced == Frames, result.sanitizedSamples == 0};
            if (!std::ranges::all_of(resultIsValid, std::identity{})) {
                throw std::runtime_error("Benchmark processing failed");
            }
            duration = std::chrono::duration<double, std::micro>(finish - start).count();
            checksum += outputs[0].samples[0];
        });
        Report(plan.Value(), execution, elapsed, checksum);
    }
}  // namespace

int main() {
#ifndef NDEBUG
    std::cerr << "Use a Release build without coverage/sanitizers for CPU qualification.\n";
    return 2;
#else
    std::cout << "quality,execution,channels,input_hz,taps,mean_us,p99_us,max_us,p99_deadline_percent,checksum\n"
              << std::fixed << std::setprecision(3);
    const std::array qualities{AudioResamplerQuality::Linear, AudioResamplerQuality::Sinc32, AudioResamplerQuality::Sinc64};
    const std::array executions{AudioResamplerExecution::Scalar, AudioResamplerExecution::Simd};
    std::ranges::for_each(qualities, [&](const AudioResamplerQuality quality) {
        std::ranges::for_each(executions, [&](const AudioResamplerExecution execution) {
            if (execution == AudioResamplerExecution::Simd && !AudioResampler::SupportsSimd()) {
                return;
            }
            Measure(quality, execution, 2, 44'100);
            Measure(quality, execution, 8, 96'000);
        });
    });
#endif
}
