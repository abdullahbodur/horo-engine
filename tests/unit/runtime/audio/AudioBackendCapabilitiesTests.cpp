#include "Horo/Audio/AudioBackendCapabilities.h"

#include <catch2/catch_test_macros.hpp>

namespace Horo::Audio {
    namespace {
        AudioBackendProbe Probe(const AudioBackendKind backend = AudioBackendKind::CoreAudio) {
            AudioBackendProbe probe{.backend = backend,
                                    .compiled = true,
                                    .hostSupported = true,
                                    .availability = AudioBackendAvailability::Available,
                                    .revision = 1,
                                    .backendVersion = "test API 1.0"};
            probe.features.fill(AudioCapabilitySupport::Unsupported);
            return probe;
        }

        TEST_CASE("Audio backend probes keep compilation host support and availability distinct", "[audio][device]") {
            auto probe = Probe();
            REQUIRE(ValidateAudioBackendProbe(probe));
            probe.compiled = false;
            REQUIRE_FALSE(ValidateAudioBackendProbe(probe));
            probe.availability = AudioBackendAvailability::Unavailable;
            REQUIRE(ValidateAudioBackendProbe(probe));
            probe.compiled = true;
            probe.hostSupported = false;
            REQUIRE(ValidateAudioBackendProbe(probe));
            probe.availability = AudioBackendAvailability::Available;
            REQUIRE_FALSE(ValidateAudioBackendProbe(probe));
            probe.hostSupported = true;
            probe.revision = 0;
            REQUIRE_FALSE(ValidateAudioBackendProbe(probe));
            probe.availability = AudioBackendAvailability::Unavailable;
            REQUIRE_FALSE(ValidateAudioBackendProbe(probe));
            probe.availability = AudioBackendAvailability::NotProbed;
            REQUIRE(ValidateAudioBackendProbe(probe));
            probe.revision = 1;
            REQUIRE_FALSE(ValidateAudioBackendProbe(probe));
        }

        TEST_CASE("Audio capability evidence preserves unknown unsupported and unavailable states", "[audio][device]") {
            auto probe = Probe();
            const auto feature = AudioBackendCapability::ExclusiveMode;
            const auto index = static_cast<std::size_t>(feature);
            for (const auto support : {AudioCapabilitySupport::Unknown, AudioCapabilitySupport::Unsupported,
                                       AudioCapabilitySupport::Unavailable, AudioCapabilitySupport::Available}) {
                probe.features[index] = support;
                REQUIRE(ValidateAudioBackendProbe(probe));
                REQUIRE(QueryAudioBackendCapability(probe, feature) == support);
            }
            REQUIRE(QueryAudioBackendCapability(probe, AudioBackendCapability::Count) == AudioCapabilitySupport::Unknown);
            REQUIRE(QueryAudioBackendCapability(probe, static_cast<AudioBackendCapability>(255)) == AudioCapabilitySupport::Unknown);
            probe.features[index] = static_cast<AudioCapabilitySupport>(255);
            REQUIRE_FALSE(ValidateAudioBackendProbe(probe));
            REQUIRE(QueryAudioBackendCapability(probe, feature) == AudioCapabilitySupport::Unknown);
            probe.features[index] = AudioCapabilitySupport::Available;
            probe.availability = AudioBackendAvailability::Unavailable;
            REQUIRE_FALSE(ValidateAudioBackendProbe(probe));
            probe.features[index] = AudioCapabilitySupport::Unavailable;
            REQUIRE(ValidateAudioBackendProbe(probe));
        }

        TEST_CASE("Audio Null capability probes cannot claim physical or native support", "[audio][device]") {
            for (const auto backend : {AudioBackendKind::WASAPI, AudioBackendKind::CoreAudio, AudioBackendKind::PipeWire,
                                       AudioBackendKind::SDL3Audio, AudioBackendKind::NullAudio}) {
                auto probe = Probe(backend);
                REQUIRE(ValidateAudioBackendProbe(probe));
                for (auto &feature : probe.features) {
                    feature = AudioCapabilitySupport::Available;
                    REQUIRE(ValidateAudioBackendProbe(probe) == (backend != AudioBackendKind::NullAudio));
                    feature = AudioCapabilitySupport::Unsupported;
                }
            }
        }

        TEST_CASE("Audio backend probes reject unknown versions identities and oversized metadata", "[audio][device]") {
            auto probe = Probe();
            probe.contractVersion = 2;
            REQUIRE_FALSE(ValidateAudioBackendProbe(probe));
            probe = Probe();
            probe.backend = static_cast<AudioBackendKind>(255);
            REQUIRE_FALSE(ValidateAudioBackendProbe(probe));
            probe = Probe();
            probe.backendVersion.assign(128, 'x');
            REQUIRE(ValidateAudioBackendProbe(probe));
            probe.backendVersion.push_back('x');
            REQUIRE_FALSE(ValidateAudioBackendProbe(probe));
            probe = Probe();
            probe.availability = static_cast<AudioBackendAvailability>(255);
            REQUIRE_FALSE(ValidateAudioBackendProbe(probe));
        }
    }  // namespace
}  // namespace Horo::Audio
