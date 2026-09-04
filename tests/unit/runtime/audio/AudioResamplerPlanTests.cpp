#include "Horo/Audio/AudioErrors.h"
#include "Horo/Audio/AudioResamplerPlan.h"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <limits>
#include <type_traits>

namespace Horo::Audio {
    namespace {
        constexpr AudioResamplerBudget LargeBudget{1ULL << 40, 1ULL << 30, 4096};

        AudioResamplerDescriptor Request() {
            return {.inputRate = 48'000, .outputRate = 48'000, .channels = 2, .maximumOutputFrames = 256};
        }

        void CheckError(const AudioResamplerDescriptor &request, const ErrorCodeDescriptor &error) {
            const auto result = AudioResamplerPlan::Prepare(request, LargeBudget);
            REQUIRE(result.HasError());
            REQUIRE(result.ErrorValue().code.Value() == error.code.Value());
        }

        TEST_CASE("Resampler plans own requests and separate rate conversion from pitch", "[unit][audio][resampler]") {
            auto request = Request();
            request.inputRate = 24'000;
            request.pitch = 2.0;
            const auto plan = AudioResamplerPlan::Prepare(request, LargeBudget);
            REQUIRE(plan.HasValue());
            REQUIRE(plan.Value().InputStep() == 1.0);
            request.pitch = 1.0;
            REQUIRE(plan.Value().Descriptor().pitch == 2.0);
            REQUIRE(plan.Value().Descriptor().inputRate == 24'000);
            REQUIRE(plan.Value().Taps() == 32);
            REQUIRE(plan.Value().Requirements().maximumSampleProducts == 2 * 256 * 32);
            REQUIRE(plan.Value().Requirements().maximumHistoryBytes == 2 * 32 * sizeof(float));
            REQUIRE(plan.Value().Requirements().maximumLookAheadFrames == 16);
            static_assert(!std::is_default_constructible_v<AudioResamplerPlan>);
        }

        TEST_CASE("Resampler quality selects explicit widths and stretches anti-alias support", "[unit][audio][resampler]") {
            constexpr std::array widths{2U, 32U, 64U};
            for (std::size_t index = 0; index < widths.size(); ++index) {
                auto request = Request();
                request.quality = static_cast<AudioResamplerQuality>(index);
                REQUIRE(AudioResamplerPlan::Prepare(request, LargeBudget).Value().Taps() == widths[index]);
                request.inputRate = 96'000;
                const auto plan = AudioResamplerPlan::Prepare(request, LargeBudget);
                REQUIRE(plan.HasValue());
                REQUIRE(plan.Value().InputStep() == 2.0);
                REQUIRE(plan.Value().Taps() == (index == 0 ? 2 : widths[index] * 2));
            }
            auto request = Request();
            request.inputRate = 44'100;
            const auto plan = AudioResamplerPlan::Prepare(request, LargeBudget);
            REQUIRE(plan.Value().InputStep() == 44'100.0 / 48'000.0);
        }

        TEST_CASE("Resampler rejects malformed dimensions and enum values", "[unit][audio][resampler]") {
            for (const auto rate : {0U, 7'999U, 384'001U, std::numeric_limits<std::uint32_t>::max()}) {
                auto request = Request();
                request.inputRate = rate;
                CheckError(request, AudioErrors::ResamplerInvalid);
                request = Request();
                request.outputRate = rate;
                CheckError(request, AudioErrors::ResamplerInvalid);
            }
            for (const auto channels : {0U, 65U, std::numeric_limits<std::uint32_t>::max()}) {
                auto request = Request();
                request.channels = channels;
                CheckError(request, AudioErrors::ResamplerInvalid);
            }
            for (const auto frames : {0U, 16'385U, std::numeric_limits<std::uint32_t>::max()}) {
                auto request = Request();
                request.maximumOutputFrames = frames;
                CheckError(request, AudioErrors::ResamplerInvalid);
            }
            auto request = Request();
            request.quality = static_cast<AudioResamplerQuality>(255);
            CheckError(request, AudioErrors::ResamplerInvalid);
            request = Request();
            request.stage = static_cast<AudioResamplerStage>(255);
            CheckError(request, AudioErrors::ResamplerInvalid);
        }

        TEST_CASE("Resampler rejects invalid pitch and unsupported time stretch", "[unit][audio][resampler]") {
            for (const double pitch :
                 {0.0, -1.0, 0.124, 8.001, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
                auto request = Request();
                request.pitch = pitch;
                CheckError(request, AudioErrors::ResamplerInvalid);
            }
            for (const double speed : {0.0, 0.5, 2.0, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
                auto request = Request();
                request.playbackSpeed = speed;
                CheckError(request, AudioErrors::OperationUnsupported);
            }
            auto request = Request();
            request.stage = AudioResamplerStage::MixToDevice;
            REQUIRE(AudioResamplerPlan::Prepare(request, LargeBudget).HasValue());
            request.pitch = 2.0;
            CheckError(request, AudioErrors::OperationUnsupported);
        }

        TEST_CASE("Resampler bounds effective ratios independently of individual rates", "[unit][audio][resampler]") {
            auto request = Request();
            request.inputRate = 384'000;
            request.outputRate = 8'000;
            request.pitch = 8.0;
            CheckError(request, AudioErrors::ResamplerInvalid);
            request.inputRate = 8'000;
            request.outputRate = 384'000;
            request.pitch = 0.125;
            CheckError(request, AudioErrors::ResamplerInvalid);
            request.outputRate = 64'000;
            REQUIRE(AudioResamplerPlan::Prepare(request, LargeBudget).Value().InputStep() == 1.0 / 64.0);
            request.inputRate = 64'000;
            request.outputRate = 8'000;
            request.pitch = 8.0;
            request.quality = AudioResamplerQuality::Sinc64;
            request.channels = 64;
            request.maximumOutputFrames = 16'384;
            const auto plan = AudioResamplerPlan::Prepare(request, LargeBudget);
            REQUIRE(plan.HasValue());
            REQUIRE(plan.Value().InputStep() == 64.0);
            REQUIRE(plan.Value().Taps() == 4096);
            REQUIRE(plan.Value().Requirements().maximumSampleProducts == 4'294'967'296ULL);
        }

        TEST_CASE("Resampler requires all budgets and accepts exact reservations", "[unit][audio][resampler]") {
            const auto request = Request();
            const auto plan = AudioResamplerPlan::Prepare(request, LargeBudget);
            REQUIRE(plan.HasValue());
            const auto exact = plan.Value().Requirements();
            REQUIRE(AudioResamplerPlan::Prepare(request, exact).HasValue());
            for (unsigned index = 0; index < 3; ++index) {
                auto budget = exact;
                if (index == 0) {
                    --budget.maximumSampleProducts;
                }
                if (index == 1) {
                    --budget.maximumHistoryBytes;
                }
                if (index == 2) {
                    --budget.maximumLookAheadFrames;
                }
                const auto result = AudioResamplerPlan::Prepare(request, budget);
                REQUIRE(result.HasError());
                REQUIRE(result.ErrorValue().code.Value() == AudioErrors::ResamplerBudgetExceeded.code.Value());
            }
        }
    }  // namespace
}  // namespace Horo::Audio
