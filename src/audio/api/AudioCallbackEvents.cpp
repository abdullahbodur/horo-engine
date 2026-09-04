#include "Horo/Audio/AudioCallbackEvents.h"

#include <type_traits>

namespace Horo::Audio {
    static_assert(std::is_trivially_copyable_v<AudioCallbackEvent>);
    static_assert(sizeof(AudioCallbackEvent) <= 128);

    /** @copydoc ValidateAudioCallbackEvent */
    bool ValidateAudioCallbackEvent(const AudioCallbackEvent &event, const AudioDeviceEpoch &expected,
                                    const std::uint64_t clockDomain) noexcept {
        if (!MatchesAudioDeviceEpoch(expected, event.epoch) || clockDomain == 0 || event.timestamp.clockDomain != clockDomain)
            return false;
        if (const auto *fault = std::get_if<AudioCallbackFault>(&event.fact))
            return fault->code > AudioCallbackFaultCode::None && fault->code <= AudioCallbackFaultCode::BackendFailure;
        if (const auto *underrun = std::get_if<AudioCallbackUnderrun>(&event.fact))
            return !underrun->missingFrames.has_value() || *underrun->missingFrames != 0;
        return true;
    }

    /** @copydoc IsCriticalAudioCallbackFact */
    bool IsCriticalAudioCallbackFact(const AudioCallbackFact &fact) noexcept {
        return !std::holds_alternative<AudioCallbackUnderrun>(fact);
    }
}  // namespace Horo::Audio
