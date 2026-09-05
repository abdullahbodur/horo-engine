#include "Horo/Audio/AudioDeviceTiming.h"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <limits>
#include <type_traits>

namespace Horo::Audio {
    namespace {
        AudioDeviceEpoch Epoch() {
            return {.device = {AudioRuntimeId::Create(7).Value(), 1, 2}, .formatRevision = 3, .callbackEpoch = 4};
        }

        AudioDurationObservation Reported() {
            return {.quality = AudioObservationQuality::Reported,
                    .source = AudioObservationSource::BackendApi,
                    .nanoseconds = 0,
                    .observedAt = AudioMonotonicTimestamp{9, 0}};
        }

        AudioDurationObservation Measured(const AudioObservationSource source = AudioObservationSource::CallbackClock) {
            return {.quality = AudioObservationQuality::Measured,
                    .source = source,
                    .nanoseconds = 20,
                    .observedAt = AudioMonotonicTimestamp{9, 100},
                    .windowNanoseconds = 50,
                    .sampleCount = 4};
        }

        AudioDeviceTimingReport Report() {
            return {.epoch = Epoch(), .capturedAt = {9, 100}};
        }

        bool Valid(const AudioDeviceTimingReport &report, const AudioBackendKind backend = AudioBackendKind::CoreAudio) {
            return ValidateAudioDeviceTimingReport(report, Epoch(), backend);
        }

        TEST_CASE("Audio callback identity matches every dimension and never accepts empty epochs", "[audio][timing]") {
            const auto expected = Epoch();
            REQUIRE(MatchesAudioDeviceEpoch(expected, expected));
            REQUIRE_FALSE(MatchesAudioDeviceEpoch({}, {}));
            auto changed = expected;
            changed.device.owner = AudioRuntimeId::Create(8).Value();
            REQUIRE_FALSE(MatchesAudioDeviceEpoch(expected, changed));
            changed = expected;
            ++changed.device.slot;
            REQUIRE_FALSE(MatchesAudioDeviceEpoch(expected, changed));
            changed = expected;
            ++changed.device.generation;
            REQUIRE_FALSE(MatchesAudioDeviceEpoch(expected, changed));
            changed = expected;
            ++changed.formatRevision;
            REQUIRE_FALSE(MatchesAudioDeviceEpoch(expected, changed));
            changed = expected;
            ++changed.callbackEpoch;
            REQUIRE_FALSE(MatchesAudioDeviceEpoch(expected, changed));
            changed.formatRevision = 0;
            REQUIRE_FALSE(MatchesAudioDeviceEpoch(changed, changed));
            changed = expected;
            changed.callbackEpoch = 0;
            REQUIRE_FALSE(MatchesAudioDeviceEpoch(changed, changed));
            static_assert(std::is_trivially_copyable_v<AudioDeviceEpoch>);
            static_assert(std::is_trivially_copyable_v<AudioDeviceTimingReport>);
        }

        TEST_CASE("Audio missing timing knowledge does not fabricate a numeric zero", "[audio][timing]") {
            for (const auto quality :
                 {AudioObservationQuality::Unknown, AudioObservationQuality::Unsupported, AudioObservationQuality::Unavailable}) {
                AudioDurationObservation value{.quality = quality};
                REQUIRE(ValidateAudioDurationObservation(value));
                REQUIRE_FALSE(value.nanoseconds);
                value.nanoseconds = 0;
                REQUIRE_FALSE(ValidateAudioDurationObservation(value));
            }
            const auto zero = Reported();
            REQUIRE(ValidateAudioDurationObservation(zero));
            REQUIRE(zero.nanoseconds.has_value());
            REQUIRE(*zero.nanoseconds == 0);
            REQUIRE(zero.observedAt->nanoseconds == 0);
        }

        TEST_CASE("Audio missing duration evidence cannot contain stale metadata", "[audio][timing]") {
            using Mutation = void (*)(AudioDurationObservation &);
            const std::array<Mutation, 4> mutations{
                [](auto &value) {
                value.source = AudioObservationSource::BackendApi;
            },
                [](auto &value) {
                value.observedAt = AudioMonotonicTimestamp{9, 0};
            },
                [](auto &value) {
                value.windowNanoseconds = 1;
            },
                [](auto &value) {
                value.sampleCount = 1;
            },
            };
            for (const auto mutation : mutations) {
                AudioDurationObservation value;
                mutation(value);
                REQUIRE_FALSE(ValidateAudioDurationObservation(value));
            }
        }

        TEST_CASE("Audio API and estimated durations preserve distinct source semantics", "[audio][timing]") {
            auto value = Reported();
            value.source = AudioObservationSource::Adapter;
            REQUIRE_FALSE(ValidateAudioDurationObservation(value));
            value.quality = AudioObservationQuality::Estimated;
            REQUIRE(ValidateAudioDurationObservation(value));
            value.source = AudioObservationSource::BackendApi;
            REQUIRE(ValidateAudioDurationObservation(value));
            value.source = AudioObservationSource::Loopback;
            REQUIRE_FALSE(ValidateAudioDurationObservation(value));
            value.source = AudioObservationSource::Adapter;
            value.nanoseconds.reset();
            REQUIRE_FALSE(ValidateAudioDurationObservation(value));
            value = Reported();
            value.windowNanoseconds = 10;
            REQUIRE_FALSE(ValidateAudioDurationObservation(value));
            value.windowNanoseconds.reset();
            value.sampleCount = 1;
            REQUIRE_FALSE(ValidateAudioDurationObservation(value));
            value.quality = static_cast<AudioObservationQuality>(255);
            REQUIRE_FALSE(ValidateAudioDurationObservation(value));
        }

        TEST_CASE("Audio numeric duration evidence requires a value and named monotonic clock", "[audio][timing]") {
            auto value = Reported();
            value.nanoseconds.reset();
            REQUIRE_FALSE(ValidateAudioDurationObservation(value));
            value = Reported();
            value.observedAt.reset();
            REQUIRE_FALSE(ValidateAudioDurationObservation(value));
            value = Reported();
            value.observedAt->clockDomain = 0;
            REQUIRE_FALSE(ValidateAudioDurationObservation(value));
            value = Reported();
            value.nanoseconds = std::numeric_limits<std::uint64_t>::max();
            REQUIRE(ValidateAudioDurationObservation(value));
        }

        TEST_CASE("Audio measured durations require positive bounded windows and sample counts", "[audio][timing]") {
            for (const auto source :
                 {AudioObservationSource::CallbackClock, AudioObservationSource::Loopback, AudioObservationSource::DeterministicClock})
                REQUIRE(ValidateAudioDurationObservation(Measured(source)));
            auto value = Measured();
            value.nanoseconds.reset();
            REQUIRE_FALSE(ValidateAudioDurationObservation(value));
            value = Measured();
            value.windowNanoseconds.reset();
            REQUIRE_FALSE(ValidateAudioDurationObservation(value));
            value.windowNanoseconds = 0;
            REQUIRE_FALSE(ValidateAudioDurationObservation(value));
            value = Measured();
            value.sampleCount = 0;
            REQUIRE_FALSE(ValidateAudioDurationObservation(value));
            value = Measured();
            value.windowNanoseconds = 101;
            REQUIRE_FALSE(ValidateAudioDurationObservation(value));
            value.windowNanoseconds = 100;
            REQUIRE(ValidateAudioDurationObservation(value));
            value.source = AudioObservationSource::BackendApi;
            REQUIRE_FALSE(ValidateAudioDurationObservation(value));
            value.source = static_cast<AudioObservationSource>(255);
            REQUIRE_FALSE(ValidateAudioDurationObservation(value));
        }

        TEST_CASE("Audio timing reports bind every observation to the current epoch and clock", "[audio][timing]") {
            auto report = Report();
            REQUIRE(Valid(report));
            ++report.epoch.callbackEpoch;
            REQUIRE_FALSE(Valid(report));
            report = Report();
            report.capturedAt.clockDomain = 0;
            REQUIRE_FALSE(Valid(report));
            report = Report();
            REQUIRE_FALSE(Valid(report, static_cast<AudioBackendKind>(255)));
            for (const auto member : {&AudioDeviceTimingReport::hardwareLatency, &AudioDeviceTimingReport::adapterLatency,
                                      &AudioDeviceTimingReport::queuedLatency, &AudioDeviceTimingReport::endToEndLatency,
                                      &AudioDeviceTimingReport::callbackExecution, &AudioDeviceTimingReport::callbackPeriod}) {
                report = Report();
                report.*member = Reported();
                REQUIRE(Valid(report));
                (report.*member).observedAt->clockDomain = 10;
                REQUIRE_FALSE(Valid(report));
                (report.*member).observedAt = AudioMonotonicTimestamp{9, 101};
                REQUIRE_FALSE(Valid(report));
                (report.*member).nanoseconds.reset();
                REQUIRE_FALSE(Valid(report));
            }
        }

        TEST_CASE("Audio physical measurements and callback observations cannot impersonate each other", "[audio][timing]") {
            for (const auto member : {&AudioDeviceTimingReport::hardwareLatency, &AudioDeviceTimingReport::endToEndLatency}) {
                auto report = Report();
                report.*member = Measured(AudioObservationSource::Loopback);
                REQUIRE(Valid(report));
                report.*member = Measured();
                REQUIRE_FALSE(Valid(report));
            }
            for (const auto member : {&AudioDeviceTimingReport::callbackExecution, &AudioDeviceTimingReport::callbackPeriod}) {
                auto report = Report();
                report.*member = Measured();
                REQUIRE(Valid(report));
                report.*member = Measured(AudioObservationSource::DeterministicClock);
                REQUIRE(Valid(report));
                report.*member = Measured(AudioObservationSource::Loopback);
                REQUIRE_FALSE(Valid(report));
            }
        }

        TEST_CASE("Audio Null reports unsupported physical latency instead of fabricated zero", "[audio][timing]") {
            auto report = Report();
            REQUIRE_FALSE(Valid(report, AudioBackendKind::NullAudio));
            report.hardwareLatency.quality = AudioObservationQuality::Unsupported;
            REQUIRE_FALSE(Valid(report, AudioBackendKind::NullAudio));
            report.endToEndLatency.quality = AudioObservationQuality::Unsupported;
            report.callbackExecution = Measured(AudioObservationSource::DeterministicClock);
            REQUIRE(Valid(report, AudioBackendKind::NullAudio));
            report.hardwareLatency = Reported();
            REQUIRE_FALSE(Valid(report, AudioBackendKind::NullAudio));
            for (const auto backend :
                 {AudioBackendKind::WASAPI, AudioBackendKind::CoreAudio, AudioBackendKind::PipeWire, AudioBackendKind::SDL3Audio})
                REQUIRE(Valid(report, backend));
        }
    }  // namespace
}  // namespace Horo::Audio
