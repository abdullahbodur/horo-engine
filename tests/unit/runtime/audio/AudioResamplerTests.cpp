#include "Horo/Audio/AudioResampler.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <cmath>
#include <cstdlib>
#include <iterator>
#include <limits>
#include <new>
#include <ranges>
#include <vector>

namespace {
    std::atomic<std::size_t> allocations{};

    void *Allocate(const std::size_t size) {
        allocations.fetch_add(1, std::memory_order_relaxed);
        if (void *memory = std::malloc(std::max(size, std::size_t{1}))) {
            return memory;
        }
        throw std::bad_alloc{};
    }
}  // namespace

void *operator new(const std::size_t size) {
    return Allocate(size);
}

void *operator new[](const std::size_t size) {
    return Allocate(size);
}

void operator delete(void *memory) noexcept {
    std::free(memory);
}

void operator delete[](void *memory) noexcept {
    std::free(memory);
}

void operator delete(void *memory, std::size_t) noexcept {
    std::free(memory);
}

void operator delete[](void *memory, std::size_t) noexcept {
    std::free(memory);
}

namespace Horo::Audio {
    namespace {
        struct alignas(64) Block {
            std::array<float, 512> values{};
        };

        using Signal = std::array<std::vector<float>, 2>;

        AudioResamplerPlan Plan(const AudioResamplerQuality quality = AudioResamplerQuality::Linear, const double pitch = 1.0) {
            auto prepared = AudioResamplerPlan::Prepare({.quality = quality,
                                                         .inputRate = 48'000,
                                                         .outputRate = 48'000,
                                                         .channels = 2,
                                                         .maximumOutputFrames = 128,
                                                         .pitch = pitch},
                                                        {1ULL << 32, 1ULL << 24, 4096});
            REQUIRE(prepared.HasValue());
            return prepared.Value();
        }

        AudioResampler Create(const AudioResamplerPlan &plan, const AudioResamplerExecution execution = AudioResamplerExecution::Scalar) {
            auto result = AudioResampler::Create(plan, 32ULL << 20, execution);
            REQUIRE(result.HasValue());
            return std::move(result).Value();
        }

        Signal Render(AudioResampler &processor, const Signal &signal, const std::uint32_t inputChunk, const std::uint32_t outputChunk) {
            std::array<Block, 2> input;
            std::array<Block, 2> output;
            const std::array<std::span<const float>, 2> sources{input[0].values, input[1].values};
            const std::array<std::span<float>, 2> destinations{output[0].values, output[1].values};
            Signal rendered;
            std::size_t offset = 0;
            for (unsigned attempt = 0; attempt < 100'000; ++attempt) {
                const auto frames = static_cast<std::uint32_t>(std::min<std::size_t>(inputChunk, signal[0].size() - offset));
                std::copy_n(signal[0].begin() + static_cast<std::ptrdiff_t>(offset), frames, input[0].values.begin());
                std::copy_n(signal[1].begin() + static_cast<std::ptrdiff_t>(offset), frames, input[1].values.begin());
                const auto before = allocations.load(std::memory_order_relaxed);
                const auto result = processor.Process({sources, frames, offset + frames == signal[0].size()}, {destinations, outputChunk});
                const auto after = allocations.load(std::memory_order_relaxed);
                REQUIRE(before == after);
                REQUIRE(result.status != AudioResamplerStatus::InvalidBuffer);
                REQUIRE(result.status != AudioResamplerStatus::InvalidState);
                REQUIRE(result.sanitizedSamples == 0);
                offset += result.consumed;
                rendered[0].insert(rendered[0].end(), output[0].values.begin(), output[0].values.begin() + result.produced);
                rendered[1].insert(rendered[1].end(), output[1].values.begin(), output[1].values.begin() + result.produced);
                if (result.status == AudioResamplerStatus::Complete) {
                    REQUIRE(offset == signal[0].size());
                    return rendered;
                }
            }
            FAIL("Resampler did not finish bounded input and tail");
            return rendered;
        }

        Signal Source() {
            Signal signal;
            std::ranges::transform(std::views::iota(0U, 257U), std::back_inserter(signal[0]), [](const unsigned frame) {
                return static_cast<float>(std::sin(frame * 0.1));
            });
            std::ranges::transform(std::views::iota(0U, 257U), std::back_inserter(signal[1]), [](const unsigned frame) {
                return static_cast<float>(std::cos(frame * 0.07)) * 2.0F;
            });
            return signal;
        }

        TEST_CASE("Streaming linear unity conversion preserves samples and channel headroom", "[unit][audio][resampler]") {
            auto processor = Create(Plan());
            const auto source = Source();
            REQUIRE(Render(processor, source, 17, 7) == source);
            processor.Reset();
            REQUIRE(Render(processor, source, 128, 128) == source);
        }

        TEST_CASE("Streaming phases tails and output are invariant to block partitioning", "[unit][audio][resampler]") {
            const auto source = Source();
            const auto quality = GENERATE(AudioResamplerQuality::Linear, AudioResamplerQuality::Sinc32, AudioResamplerQuality::Sinc64);
            const double pitch = GENERATE(0.125, 0.91875, 1.0, 2.0, 8.0);
            auto processor = Create(Plan(quality, pitch));
            const auto reference = Render(processor, source, 128, 128);
            processor.Reset();
            REQUIRE(Render(processor, source, 1, 1) == reference);
            processor.Reset();
            REQUIRE(Render(processor, source, 37, 7) == reference);
            REQUIRE(reference[0].size() == reference[1].size());
            REQUIRE(reference[0].size() <= (source[0].size() + 512) * 8);
        }

        TEST_CASE("SIMD streaming matches scalar output and keeps partition invariant state", "[unit][audio][resampler]") {
            if (!AudioResampler::SupportsSimd()) {
                REQUIRE(AudioResampler::Create(Plan(), 32ULL << 20, AudioResamplerExecution::Simd).HasError());
                return;
            }
            const auto source = Source();
            const auto quality = GENERATE(AudioResamplerQuality::Linear, AudioResamplerQuality::Sinc32, AudioResamplerQuality::Sinc64);
            const double pitch = GENERATE(0.125, 0.91875, 1.0, 2.0, 8.0);
            auto scalar = Create(Plan(quality, pitch));
            auto native = Create(Plan(quality, pitch), AudioResamplerExecution::Simd);
            const auto reference = Render(scalar, source, 128, 128);
            const auto actual = Render(native, source, 37, 7);
            native.Reset();
            REQUIRE(Render(native, source, 1, 1) == actual);
            REQUIRE(actual[0].size() == reference[0].size());
            REQUIRE(actual[1].size() == reference[1].size());
            const auto close = [](const float value, const float expected) {
                return value == Catch::Approx(expected).margin(3e-6);
            };
            REQUIRE(std::ranges::equal(actual[0], reference[0], close));
            REQUIRE(std::ranges::equal(actual[1], reference[1], close));
        }

        TEST_CASE("Empty stream finishes without samples and reset and move preserve ownership", "[unit][audio][resampler]") {
            auto original = Create(Plan());
            REQUIRE(Render(original, {}, 1, 1)[0].empty());
            auto moved = std::move(original);
            original.Reset();
            REQUIRE(original.Process({}, {}).status == AudioResamplerStatus::InvalidState);
            moved.Reset();
            auto destination = Create(Plan());
            destination = std::move(moved);
            REQUIRE(Render(destination, Source(), 13, 5) == Source());
            REQUIRE(AudioResampler::Create(Plan(), 0).HasError());
        }

        TEST_CASE("Malformed spans overlap alignment and limits leave state and output unchanged", "[unit][audio][resampler]") {
            auto processor = Create(Plan());
            std::array<Block, 2> input;
            std::array<Block, 2> output;
            output[0].values.fill(99.0F);
            std::array<std::span<const float>, 2> sources{input[0].values, input[1].values};
            std::array<std::span<float>, 2> destinations{output[0].values, output[1].values};
            REQUIRE(processor.Process({{}, 1}, {destinations, 1}).status == AudioResamplerStatus::InvalidBuffer);
            REQUIRE(processor.Process({sources, 1}, {{}, 1}).status == AudioResamplerStatus::InvalidBuffer);
            REQUIRE(processor.Process({sources, 100'000}, {destinations, 1}).status == AudioResamplerStatus::InvalidBuffer);
            REQUIRE(processor.Process({sources, 1}, {destinations, 129}).status == AudioResamplerStatus::InvalidBuffer);
            sources[0] = std::span{input[0].values}.subspan(1);
            REQUIRE(processor.Process({sources, 1}, {destinations, 1}).status == AudioResamplerStatus::InvalidBuffer);
            sources[0] = {};
            REQUIRE(processor.Process({sources, 1}, {destinations, 1}).status == AudioResamplerStatus::InvalidBuffer);
            sources[0] = output[0].values;
            REQUIRE(processor.Process({sources, 1}, {destinations, 1}).status == AudioResamplerStatus::InvalidBuffer);
            sources[0] = input[0].values;
            destinations[1] = destinations[0];
            REQUIRE(processor.Process({sources, 1}, {destinations, 1}).status == AudioResamplerStatus::InvalidBuffer);
            REQUIRE(output[0].values[0] == 99.0F);
            REQUIRE(Render(processor, Source(), 13, 5) == Source());
        }

        TEST_CASE("Callback process and reset allocate nothing including starvation and fault sanitization", "[unit][audio][resampler]") {
            auto processor = Create(Plan());
            std::array<Block, 2> input;
            std::array<Block, 2> output;
            const std::array<std::span<const float>, 2> sources{input[0].values, input[1].values};
            const std::array<std::span<float>, 2> destinations{output[0].values, output[1].values};
            input[0].values[0] = std::numeric_limits<float>::quiet_NaN();
            input[0].values[1] = std::numeric_limits<float>::infinity();
            input[1].values[0] = std::numeric_limits<float>::denorm_min();
            input[1].values[1] = -0.0F;
            const auto before = allocations.load(std::memory_order_relaxed);
            const auto emptyOutput = processor.Process({sources, 2, true}, {destinations, 0});
            const auto starved = processor.Process({sources, 0, false}, {destinations, 128});
            const auto result = processor.Process({sources, 2, true}, {destinations, 128});
            const auto terminal = processor.Process({sources, 0, false}, {destinations, 0});
            const auto rejected = processor.Process({sources, 1, false}, {destinations, 128});
            processor.Reset();
            const auto after = allocations.load(std::memory_order_relaxed);
            REQUIRE(before == after);
            REQUIRE(emptyOutput.consumed == 0);
            REQUIRE(starved.status == AudioResamplerStatus::InputNeeded);
            REQUIRE(result.status == AudioResamplerStatus::Complete);
            REQUIRE(terminal.status == AudioResamplerStatus::Complete);
            REQUIRE(result.produced == 2);
            REQUIRE(result.sanitizedSamples == 2);
            REQUIRE(rejected.status == AudioResamplerStatus::InvalidState);
            REQUIRE(rejected.consumed == 0);
            REQUIRE(rejected.produced == 0);
            REQUIRE(rejected.sanitizedSamples == 0);
            REQUIRE(output[0].values[0] == 0.0F);
            REQUIRE_FALSE(std::signbit(output[1].values[0]));
            REQUIRE_FALSE(std::signbit(output[1].values[1]));
        }
    }  // namespace
}  // namespace Horo::Audio
