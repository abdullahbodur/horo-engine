#include "Horo/Audio/AudioCallbackEvents.h"

#include <catch2/catch_test_macros.hpp>
#include <type_traits>

namespace Horo::Audio {
    namespace {
        AudioCallbackEvent Event() {
            return {.epoch = {.device = {AudioRuntimeId::Create(7).Value(), 1, 2}, .formatRevision = 3, .callbackEpoch = 4},
                    .sampleFrame = 0,
                    .timestamp = {9, 0},
                    .fact = AudioCallbackReady{}};
        }

        TEST_CASE("Audio callback facts preserve complete identity and zero-valued initial positions", "[audio][callback]") {
            auto event = Event();
            const auto expected = event.epoch;
            REQUIRE(ValidateAudioCallbackEvent(event, expected, 9));
            REQUIRE(event.sampleFrame == 0);
            REQUIRE(event.timestamp.nanoseconds == 0);
            REQUIRE_FALSE(ValidateAudioCallbackEvent(event, expected, 0));
            REQUIRE_FALSE(ValidateAudioCallbackEvent(event, expected, 10));
            ++event.epoch.callbackEpoch;
            REQUIRE_FALSE(ValidateAudioCallbackEvent(event, expected, 9));
            static_assert(std::is_trivially_copyable_v<AudioCallbackEvent>);
        }

        TEST_CASE("Audio callback lifecycle facts require retained delivery independently of telemetry", "[audio][callback]") {
            auto event = Event();
            REQUIRE(IsCriticalAudioCallbackFact(event.fact));
            event.fact = AudioCallbackQuiesced{};
            REQUIRE(IsCriticalAudioCallbackFact(event.fact));
            REQUIRE(ValidateAudioCallbackEvent(event, event.epoch, 9));
            event.fact = AudioCallbackFault{AudioCallbackFaultCode::BackendFailure};
            REQUIRE(IsCriticalAudioCallbackFact(event.fact));
            REQUIRE(ValidateAudioCallbackEvent(event, event.epoch, 9));
            event.fact = AudioCallbackUnderrun{};
            REQUIRE_FALSE(IsCriticalAudioCallbackFact(event.fact));
            REQUIRE(ValidateAudioCallbackEvent(event, event.epoch, 9));
            event.fact = AudioCallbackUnderrun{0};
            REQUIRE_FALSE(ValidateAudioCallbackEvent(event, event.epoch, 9));
            event.fact = AudioCallbackUnderrun{128};
            REQUIRE(ValidateAudioCallbackEvent(event, event.epoch, 9));
        }

        TEST_CASE("Audio callback fault records reject empty and unknown codes", "[audio][callback]") {
            auto event = Event();
            for (const auto code :
                 {AudioCallbackFaultCode::NonFiniteOutput, AudioCallbackFaultCode::InvalidEpoch, AudioCallbackFaultCode::DeadlineExceeded,
                  AudioCallbackFaultCode::CompletionOverflow, AudioCallbackFaultCode::BackendFailure}) {
                event.fact = AudioCallbackFault{code};
                REQUIRE(ValidateAudioCallbackEvent(event, event.epoch, 9));
            }
            event.fact = AudioCallbackFault{};
            REQUIRE_FALSE(ValidateAudioCallbackEvent(event, event.epoch, 9));
            event.fact = AudioCallbackFault{static_cast<AudioCallbackFaultCode>(255)};
            REQUIRE_FALSE(ValidateAudioCallbackEvent(event, event.epoch, 9));
        }
    }  // namespace
}  // namespace Horo::Audio
