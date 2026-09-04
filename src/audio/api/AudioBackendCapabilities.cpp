#include "Horo/Audio/AudioBackendCapabilities.h"

#include <algorithm>

namespace Horo::Audio {
    namespace {  // NOSONAR(cpp:S1000) - File-local validation helpers intentionally have internal linkage.
        /** @brief Admitted first-party peers; no registration-order or host-platform selection. */
        constexpr std::array Backends{AudioBackendKind::WASAPI, AudioBackendKind::CoreAudio, AudioBackendKind::PipeWire,
                                      AudioBackendKind::SDL3Audio, AudioBackendKind::NullAudio};

        /** @brief Rejects impossible availability claims without conflating compilation and host support. */
        bool ValidAvailability(const AudioBackendProbe &probe) noexcept {
            using enum AudioBackendAvailability;
            switch (probe.availability) {
                case NotProbed:
                    return probe.revision == 0;
                case Unavailable:
                    return probe.revision != 0;
                case Available:
                    return probe.compiled && probe.hostSupported && probe.revision != 0;
            }
            return false;
        }
    }  // namespace

    /** @copydoc ValidateAudioBackendProbe */
    bool ValidateAudioBackendProbe(const AudioBackendProbe &probe) noexcept {
        if (probe.contractVersion != 1 || std::ranges::find(Backends, probe.backend) == Backends.end() ||
            probe.backendVersion.size() > 128 || !ValidAvailability(probe))
            return false;
        return std::ranges::all_of(probe.features, [&probe](const auto support) {
            if (probe.backend == AudioBackendKind::NullAudio)
                return support == AudioCapabilitySupport::Unsupported;
            if (support == AudioCapabilitySupport::Available && probe.availability != AudioBackendAvailability::Available)
                return false;
            return support <= AudioCapabilitySupport::Available;
        });
    }

    /** @copydoc QueryAudioBackendCapability */
    AudioCapabilitySupport QueryAudioBackendCapability(const AudioBackendProbe &probe, const AudioBackendCapability capability) noexcept {
        const auto index = static_cast<std::size_t>(capability);
        if (index >= probe.features.size() || !ValidateAudioBackendProbe(probe))
            return AudioCapabilitySupport::Unknown;
        return probe.features[index];
    }
}  // namespace Horo::Audio
