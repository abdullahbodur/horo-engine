#include "Horo/Audio/AudioDeviceNegotiation.h"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>

namespace Horo::Audio {
    namespace {
        struct Negotiation final {
            AudioDeviceSnapshot snapshot;
            AudioDeviceFormatRequest request;
            AudioNegotiatedDeviceFormat candidate;
        };

        Negotiation Fixture(const AudioBackendKind backend = AudioBackendKind::CoreAudio) {
            const auto owner = AudioRuntimeId::Create(11).Value();
            const AudioDeviceId device{owner, 1, 2};
            const AudioProcessingFormat format{48'000, MakeAudioSpeakerLayout(AudioSpeakerPreset::Stereo)};
            return {.snapshot = {.owner = owner,
                                 .backend = backend,
                                 .revision = 3,
                                 .devices = {{device, "Output",
                                              backend == AudioBackendKind::NullAudio ? AudioDeviceClass::Headless
                                                                                     : AudioDeviceClass::Physical}},
                                 .defaults = {.multimedia = device}},
                    .request = {.device = AudioDefaultDeviceRole::Multimedia,
                                .preferred = format,
                                .period = {.minimumFrames = 64, .preferredFrames = 128, .maximumFrames = 512}},
                    .candidate = {.device = device,
                                  .discoveryRevision = 3,
                                  .formatRevision = 1,
                                  .effective = format,
                                  .nativeSignal = format,
                                  .nativeChannelForHoro = {0, 1},
                                  .callbackFrames = 128}};
        }

        AudioDeviceNegotiationResult Validate(const Negotiation &fixture) {
            return ValidateAudioDeviceNegotiation(fixture.request, fixture.snapshot, fixture.candidate);
        }

        void Expect(const Negotiation &fixture, const AudioDeviceNegotiationStatus status) {
            REQUIRE(Validate(fixture).status == status);
        }

        TEST_CASE("Audio device negotiation preserves intent and native facts for every backend peer", "[audio][device]") {
            for (const auto backend : {AudioBackendKind::WASAPI, AudioBackendKind::CoreAudio, AudioBackendKind::PipeWire,
                                       AudioBackendKind::SDL3Audio, AudioBackendKind::NullAudio}) {
                auto fixture = Fixture(backend);
                const auto requested = fixture.request.preferred;
                const auto native = fixture.candidate.nativeSignal;
                const auto result = Validate(fixture);
                REQUIRE(result.status == AudioDeviceNegotiationStatus::Accepted);
                REQUIRE(result.selection.status == AudioDeviceResolutionStatus::Resolved);
                REQUIRE(result.selection.device == fixture.candidate.device);
                REQUIRE(result.selection.revision == 3);
                REQUIRE(fixture.request.preferred == requested);
                REQUIRE(fixture.candidate.nativeSignal == native);
                REQUIRE_FALSE(fixture.candidate.nativePeriodFrames);
            }
        }

        TEST_CASE("Audio device request validates callback interval preferences and bounds", "[audio][device]") {
            auto fixture = Fixture();
            for (const auto period : {AudioDevicePeriodRequest{0, 128, 512}, {64, 63, 512}, {64, 513, 512}, {64, 128, 16'385}}) {
                fixture.request.period = period;
                REQUIRE_FALSE(ValidateAudioDeviceFormatRequest(fixture.request));
                Expect(fixture, AudioDeviceNegotiationStatus::InvalidRequest);
            }
            fixture.request.period = {1, 1, 16'384};
            REQUIRE(ValidateAudioDeviceFormatRequest(fixture.request));
            fixture.request.period.preferredFrames = 16'384;
            REQUIRE(ValidateAudioDeviceFormatRequest(fixture.request));
        }

        TEST_CASE("Audio device request validates all bounded alternatives even when unused", "[audio][device]") {
            auto fixture = Fixture();
            fixture.request.preferred.sampleRate = 0;
            Expect(fixture, AudioDeviceNegotiationStatus::InvalidRequest);
            fixture.request.preferred.sampleRate = 48'000;
            fixture.request.allowedAlternatives.resize(8, fixture.request.preferred);
            Expect(fixture, AudioDeviceNegotiationStatus::Accepted);
            fixture.request.allowedAlternatives.push_back(fixture.request.preferred);
            Expect(fixture, AudioDeviceNegotiationStatus::InvalidRequest);
            fixture.request.allowedAlternatives.pop_back();
            fixture.request.allowedAlternatives.back().layout = {};
            Expect(fixture, AudioDeviceNegotiationStatus::InvalidRequest);
            fixture.request.allowedAlternatives.clear();
            fixture.request.nativeRatePolicy = static_cast<AudioDeviceRatePolicy>(255);
            Expect(fixture, AudioDeviceNegotiationStatus::InvalidRequest);
        }

        TEST_CASE("Audio device negotiation retains selection failures and rejects stale candidates", "[audio][device]") {
            auto fixture = Fixture();
            fixture.snapshot.defaults.multimedia.reset();
            auto result = Validate(fixture);
            REQUIRE(result.status == AudioDeviceNegotiationStatus::SelectionFailed);
            REQUIRE(result.selection.status == AudioDeviceResolutionStatus::Unavailable);
            fixture.snapshot.revision = 0;
            REQUIRE(Validate(fixture).selection.status == AudioDeviceResolutionStatus::InvalidSnapshot);
            fixture = Fixture();
            fixture.request.device = AudioDeviceId{};
            REQUIRE(Validate(fixture).selection.status == AudioDeviceResolutionStatus::InvalidSelection);
            fixture = Fixture();
            fixture.candidate.device.generation = 1;
            Expect(fixture, AudioDeviceNegotiationStatus::StaleDevice);
            fixture = Fixture();
            fixture.snapshot.revision = 4;
            Expect(fixture, AudioDeviceNegotiationStatus::StaleDevice);
            fixture = Fixture();
            fixture.candidate.formatRevision = 0;
            Expect(fixture, AudioDeviceNegotiationStatus::StaleDevice);
        }

        TEST_CASE("Audio device format alternatives cannot be mixed into an unlisted tuple", "[audio][device]") {
            auto fixture = Fixture();
            const AudioProcessingFormat mono44{44'100, MakeAudioSpeakerLayout(AudioSpeakerPreset::Mono)};
            fixture.request.allowedAlternatives = {mono44};
            fixture.candidate.effective.sampleRate = 44'100;
            Expect(fixture, AudioDeviceNegotiationStatus::UnadmittedFormat);
            fixture.candidate.effective = mono44;
            fixture.candidate.nativeSignal = mono44;
            fixture.candidate.nativeChannelForHoro = {0};
            fixture.candidate.deviations = {.sampleRate = AudioFormatDeviationReason::DeviceConstraint,
                                            .layout = AudioFormatDeviationReason::HostPolicy};
            Expect(fixture, AudioDeviceNegotiationStatus::Accepted);
            REQUIRE(fixture.request.preferred.sampleRate == 48'000);
            REQUIRE(fixture.request.preferred.layout.orderedChannels.size() == 2);
        }

        TEST_CASE("Audio device negotiation rejects malformed effective native and PCM formats", "[audio][device]") {
            auto fixture = Fixture();
            fixture.candidate.effective.sampleRate = 0;
            Expect(fixture, AudioDeviceNegotiationStatus::InvalidFormat);
            fixture = Fixture();
            fixture.candidate.nativeSignal.sampleRate = 0;
            Expect(fixture, AudioDeviceNegotiationStatus::InvalidFormat);
            fixture = Fixture();
            fixture.candidate.nativePcm.significantBits = 0;
            Expect(fixture, AudioDeviceNegotiationStatus::InvalidFormat);
        }

        TEST_CASE("Audio native channel mapping is a complete semantic permutation", "[audio][device]") {
            auto fixture = Fixture();
            std::ranges::reverse(fixture.candidate.nativeSignal.layout.orderedChannels);
            fixture.candidate.nativeChannelForHoro = {1, 0};
            Expect(fixture, AudioDeviceNegotiationStatus::Accepted);
            fixture.candidate.nativeChannelForHoro = {0, 1};
            Expect(fixture, AudioDeviceNegotiationStatus::InvalidChannelMap);
            fixture.candidate.nativeChannelForHoro = {1, 1};
            Expect(fixture, AudioDeviceNegotiationStatus::InvalidChannelMap);
            fixture.candidate.nativeChannelForHoro = {2, 0};
            Expect(fixture, AudioDeviceNegotiationStatus::InvalidChannelMap);
            fixture.candidate.nativeChannelForHoro = {1};
            Expect(fixture, AudioDeviceNegotiationStatus::InvalidChannelMap);
            fixture = Fixture();
            fixture.candidate.nativeSignal.layout = MakeAudioSpeakerLayout(AudioSpeakerPreset::Mono);
            Expect(fixture, AudioDeviceNegotiationStatus::InvalidChannelMap);
        }

        TEST_CASE("Audio native mappings cannot reinterpret layout family order or side roles", "[audio][device]") {
            auto fixture = Fixture();
            fixture.candidate.nativeSignal.layout = {.kind = AudioLayoutKind::Discrete,
                                                     .orderedChannels = {AudioDiscreteChannel{0}, AudioDiscreteChannel{1}}};
            Expect(fixture, AudioDeviceNegotiationStatus::InvalidChannelMap);
            const AudioChannelLayout ambi0{.kind = AudioLayoutKind::Ambisonic,
                                           .orderedChannels = {AudioAmbisonicChannel{0}},
                                           .ambisonic = AmbisonicDescriptor{0}};
            fixture.request.preferred.layout = ambi0;
            fixture.candidate.effective.layout = ambi0;
            fixture.candidate.nativeSignal.layout = {.kind = AudioLayoutKind::Ambisonic,
                                                     .orderedChannels = {AudioAmbisonicChannel{0}, AudioAmbisonicChannel{1},
                                                                         AudioAmbisonicChannel{2}, AudioAmbisonicChannel{3}},
                                                     .ambisonic = AmbisonicDescriptor{1}};
            Expect(fixture, AudioDeviceNegotiationStatus::InvalidChannelMap);
            fixture = Fixture();
            fixture.request.preferred.layout = MakeAudioSpeakerLayout(AudioSpeakerPreset::FivePointOne);
            fixture.candidate.effective = fixture.request.preferred;
            fixture.candidate.nativeSignal = fixture.request.preferred;
            fixture.candidate.nativeChannelForHoro = {0, 1, 2, 3, 4, 5};
            fixture.candidate.nativeSignal.layout.orderedChannels[4] = AudioSpeakerRole::BackLeft;
            Expect(fixture, AudioDeviceNegotiationStatus::InvalidChannelMap);
        }

        TEST_CASE("Audio reported callback periods stay inside requested limits", "[audio][device]") {
            auto fixture = Fixture();
            for (const auto frames : {0U, 63U, 513U}) {
                fixture.candidate.callbackFrames = frames;
                Expect(fixture, AudioDeviceNegotiationStatus::InvalidPeriod);
            }
            for (const auto frames : {64U, 512U}) {
                fixture.candidate.callbackFrames = frames;
                fixture.candidate.deviations.callbackPeriod = AudioFormatDeviationReason::DeviceConstraint;
                Expect(fixture, AudioDeviceNegotiationStatus::Accepted);
            }
            fixture.candidate.nativePeriodFrames = 0;
            Expect(fixture, AudioDeviceNegotiationStatus::InvalidPeriod);
            fixture.candidate.nativePeriodFrames = 256;
            Expect(fixture, AudioDeviceNegotiationStatus::Accepted);
        }

        TEST_CASE("Audio adapter rate conversion requires policy and a declared prepared resampler", "[audio][device]") {
            auto fixture = Fixture();
            fixture.candidate.nativeSignal.sampleRate = 96'000;
            Expect(fixture, AudioDeviceNegotiationStatus::UnsupportedRateConversion);
            fixture.request.nativeRatePolicy = AudioDeviceRatePolicy::AllowPreparedResampler;
            Expect(fixture, AudioDeviceNegotiationStatus::UnsupportedRateConversion);
            fixture.candidate.rateConversion = AudioDeviceRateConversion::PreparedResampler;
            Expect(fixture, AudioDeviceNegotiationStatus::Accepted);
            REQUIRE(fixture.request.preferred.sampleRate == 48'000);
            REQUIRE(fixture.candidate.nativeSignal.sampleRate == 96'000);
            fixture.candidate.rateConversion = static_cast<AudioDeviceRateConversion>(255);
            Expect(fixture, AudioDeviceNegotiationStatus::UnsupportedRateConversion);
            fixture.candidate.nativeSignal.sampleRate = 48'000;
            fixture.candidate.rateConversion = AudioDeviceRateConversion::PreparedResampler;
            Expect(fixture, AudioDeviceNegotiationStatus::UnsupportedRateConversion);
        }

        TEST_CASE("Audio device deviations must explain every changed preference and no unchanged one", "[audio][device]") {
            auto fixture = Fixture();
            fixture.candidate.deviations.sampleRate = AudioFormatDeviationReason::HostPolicy;
            Expect(fixture, AudioDeviceNegotiationStatus::UndeclaredDeviation);
            fixture = Fixture();
            fixture.candidate.deviations.layout = AudioFormatDeviationReason::DeviceConstraint;
            Expect(fixture, AudioDeviceNegotiationStatus::UndeclaredDeviation);
            fixture = Fixture();
            fixture.candidate.callbackFrames = 256;
            Expect(fixture, AudioDeviceNegotiationStatus::UndeclaredDeviation);
            fixture.candidate.deviations.callbackPeriod = static_cast<AudioFormatDeviationReason>(255);
            Expect(fixture, AudioDeviceNegotiationStatus::UndeclaredDeviation);
            fixture.candidate.deviations.callbackPeriod = AudioFormatDeviationReason::HostPolicy;
            Expect(fixture, AudioDeviceNegotiationStatus::Accepted);
        }
    }  // namespace
}  // namespace Horo::Audio
