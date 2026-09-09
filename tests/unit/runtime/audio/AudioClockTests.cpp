#include "Horo/Audio/AudioClock.h"

#include <catch2/catch_test_macros.hpp>
#include <limits>

namespace Horo::Audio {
    namespace {
        AudioRuntimeId Owner() {
            return AudioRuntimeId::Create(17).Value();
        }

        AudioClockCorrelationSnapshot Correlation() {
            return {.clock = {.owner = Owner(),
                              .epoch = 5,
                              .generation = 3,
                              .discontinuityRevision = 7,
                              .sampleFrame = 96'000,
                              .sampleRate = 48'000,
                              .observedAt = {.clockDomain = 11, .nanoseconds = 4'000'000'000},
                              .state = AudioSampleClockState::Running},
                    .producerClockDomain = 13,
                    .producerGeneration = 2,
                    .producerNanoseconds = 2'000'000'000,
                    .validFromNanoseconds = 1'000'000'000,
                    .validThroughNanoseconds = 4'000'000'000};
        }

        AudioClockExpectation Expectation() {
            return {.owner = Owner(),
                    .epoch = 5,
                    .clockGeneration = 3,
                    .discontinuityRevision = 7,
                    .producerClockDomain = 13,
                    .producerGeneration = 2};
        }

        TEST_CASE("Audio clock maps producer time with exact checked frame arithmetic", "[unit][audio][clock]") {
            const auto snapshot = Correlation();
            const auto expectation = Expectation();
            const auto anchor = MapAudioProducerTimeToSampleFrame(snapshot, expectation, 2'000'000'000);
            const auto later = MapAudioProducerTimeToSampleFrame(snapshot, expectation, 3'500'000'000);
            const auto earlier = MapAudioProducerTimeToSampleFrame(snapshot, expectation, 1'500'000'000);
            const auto fractional = MapAudioProducerTimeToSampleFrame(snapshot, expectation, 2'000'010'416);
            REQUIRE(anchor.status == AudioClockMappingStatus::Mapped);
            REQUIRE(anchor.sampleFrame == 96'000);
            REQUIRE(later.sampleFrame == 168'000);
            REQUIRE(earlier.sampleFrame == 72'000);
            REQUIRE(fractional.sampleFrame == 96'000);
        }

        TEST_CASE("Audio clock rejects stale paused and out-of-window mappings", "[unit][audio][clock]") {
            auto snapshot = Correlation();
            auto expectation = Expectation();
            REQUIRE(MapAudioProducerTimeToSampleFrame(snapshot, expectation, 999'999'999).status ==
                    AudioClockMappingStatus::OutsideCorrelation);
            ++expectation.discontinuityRevision;
            REQUIRE(MapAudioProducerTimeToSampleFrame(snapshot, expectation, snapshot.producerNanoseconds).status ==
                    AudioClockMappingStatus::StaleClock);
            expectation = Expectation();
            snapshot.clock.state = AudioSampleClockState::Paused;
            REQUIRE(MapAudioProducerTimeToSampleFrame(snapshot, expectation, snapshot.producerNanoseconds).status ==
                    AudioClockMappingStatus::Paused);
        }

        TEST_CASE("Audio clock rejects malformed correlations and arithmetic overflow", "[unit][audio][clock]") {
            auto snapshot = Correlation();
            const auto expectation = Expectation();
            SECTION("zero rate") {
                snapshot.clock.sampleRate = 0;
            }
            SECTION("rate bound") {
                snapshot.clock.sampleRate = MaximumAudioSampleRate + 1;
            }
            SECTION("reversed interval") {
                snapshot.validFromNanoseconds = snapshot.producerNanoseconds + 1;
            }
            REQUIRE(MapAudioProducerTimeToSampleFrame(snapshot, expectation, snapshot.producerNanoseconds).status ==
                    AudioClockMappingStatus::InvalidSnapshot);

            snapshot = Correlation();
            snapshot.clock.sampleFrame = 1;
            REQUIRE(MapAudioProducerTimeToSampleFrame(snapshot, expectation, snapshot.validFromNanoseconds).status ==
                    AudioClockMappingStatus::Overflow);

            snapshot = Correlation();
            snapshot.clock.sampleFrame = std::numeric_limits<std::uint64_t>::max();
            REQUIRE(MapAudioProducerTimeToSampleFrame(snapshot, expectation, snapshot.validThroughNanoseconds).status ==
                    AudioClockMappingStatus::Overflow);
        }
    }  // namespace
}  // namespace Horo::Audio
