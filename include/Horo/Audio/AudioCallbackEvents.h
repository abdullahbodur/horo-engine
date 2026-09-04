#pragma once

/**
 * @file AudioCallbackEvents.h
 * @brief Bounded callback facts that never transfer lifecycle authority to the audio thread.
 */

#include "Horo/Audio/AudioDeviceTiming.h"

#include <variant>

namespace Horo::Audio {
    /** @brief The callback adopted the matching primed epoch at a buffer boundary. */
    struct AudioCallbackReady final {};

    /** @brief The callback stopped using render state; native callback detachment must still be proven separately. */
    struct AudioCallbackQuiesced final {};

    /** @brief Underrun telemetry; absence of a native lost-frame count is distinct from zero. */
    struct AudioCallbackUnderrun final {
        std::optional<std::uint32_t> missingFrames;
    };
    /** @brief Allocation-free fault identity translated to ordinary Audio errors only by control. */
    enum class AudioCallbackFaultCode : std::uint8_t {
        None,
        NonFiniteOutput,
        InvalidEpoch,
        DeadlineExceeded,
        CompletionOverflow,
        BackendFailure
    };

    /** @brief First fatal callback fault, latched for the epoch while the callback enters admitted silence/quiescence. */
    struct AudioCallbackFault final {
        AudioCallbackFaultCode code{AudioCallbackFaultCode::None};
    };

    /** @brief Closed, owned, fixed-size callback fact payload; no strings, native pointers, logs or user callbacks. */
    using AudioCallbackFact = std::variant<AudioCallbackReady, AudioCallbackQuiesced, AudioCallbackUnderrun, AudioCallbackFault>;

    /**
     * @brief Evidence submitted by one current callback, not a committed runtime/device transition.
     *
     * The backend/control transport retains Ready, Quiesced and the first Fault for this
     * epoch until control accepts them. Saturation cannot discard those lifecycle facts;
     * repeated identical acknowledgements may coalesce only within the same epoch. Underrun
     * telemetry may coalesce with an explicit counter, not pretend every frame count is known.
     * Control validates identity before using a fact and still requires the native backend's
     * callback-entry-impossible guarantee before reclaiming callback-visible state.
     */
    struct AudioCallbackEvent final {
        AudioDeviceEpoch epoch;
        std::uint64_t sampleFrame{}; /**< Generation-scoped Horo sample cursor; zero is a valid first boundary. */
        AudioMonotonicTimestamp timestamp;
        AudioCallbackFact fact;
    };

    /**
     * @brief Checks complete callback identity, expected monotonic domain and bounded payload semantics.
     * @param event Owned callback fact, unchanged by validation.
     * @param expected Current control-owned epoch.
     * @param clockDomain Current non-zero Horo monotonic domain; native timestamps must already be mapped.
     * @return True for a structurally current event; does not commit lifecycle state or prove native detachment.
     */
    [[nodiscard]] bool ValidateAudioCallbackEvent(const AudioCallbackEvent &event, const AudioDeviceEpoch &expected,
                                                  std::uint64_t clockDomain) noexcept;

    /**
     * @brief Classifies lifecycle-critical facts independently of best-effort underrun telemetry.
     * @param fact Fixed-size callback payload; validation remains the receiver's separate responsibility.
     * @return True for Ready, Quiesced or Fault, which require retained delivery under saturation.
     */
    [[nodiscard]] bool IsCriticalAudioCallbackFact(const AudioCallbackFact &fact) noexcept;
}  // namespace Horo::Audio
