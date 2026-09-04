#pragma once

/**
 * @file AudioDeviceNegotiation.h
 * @brief Control-thread admission of requested, effective and native output formats.
 */

#include "Horo/Audio/AudioDeviceDiscovery.h"
#include "Horo/Audio/AudioFormat.h"

namespace Horo::Audio {
    /** @brief Explicit policy for differing canonical and native sample rates. */
    enum class AudioDeviceRatePolicy : std::uint8_t {
        Exact,
        AllowPreparedResampler
    };
    /** @brief Adapter work required by the reported native rate; never silently inferred during callbacks. */
    enum class AudioDeviceRateConversion : std::uint8_t {
        None,
        PreparedResampler
    };
    /** @brief Reason for a deviation from a requested preference, not permission to exceed its limits. */
    enum class AudioFormatDeviationReason : std::uint8_t {
        None,
        DeviceConstraint,
        HostPolicy
    };

    /** @brief Requested callback frame bounds, in canonical sample-rate units, with a preference inside the bounds. */
    struct AudioDevicePeriodRequest final {
        std::uint32_t minimumFrames{};
        std::uint32_t preferredFrames{};
        std::uint32_t maximumFrames{}; /**< At most 16,384 frames per Horo processing call. */
    };

    /**
     * @brief Owned immutable request, never overwritten with negotiated results.
     *
     * At most eight explicit alternative rate/layout tuples are admitted. A backend cannot
     * combine the rate of one alternative with the layout of another. An empty list requires
     * the preferred tuple exactly. Native PCM representation has no preference here: it is
     * reported separately and privately converted to/from canonical planar binary32.
     */
    struct AudioDeviceFormatRequest final {
        AudioDeviceSelection device;
        AudioProcessingFormat preferred;
        std::vector<AudioProcessingFormat> allowedAlternatives;
        AudioDevicePeriodRequest period;
        AudioDeviceRatePolicy nativeRatePolicy{AudioDeviceRatePolicy::Exact};
    };

    /** @brief Every changed preference needs a typed reason; unchanged preferences must report None. */
    struct AudioDeviceFormatDeviations final {
        AudioFormatDeviationReason sampleRate{AudioFormatDeviationReason::None};
        AudioFormatDeviationReason layout{AudioFormatDeviationReason::None};
        AudioFormatDeviationReason callbackPeriod{AudioFormatDeviationReason::None};
    };

    /**
     * @brief Immutable candidate facts, not an active device or callback epoch.
     *
     * Control revalidates the current discovery snapshot before publication. formatRevision
     * is non-zero and advances on format changes without wrapping. effective is the Horo
     * planar processing contract; nativeSignal describes the adapter's actual rate and
     * semantic channel order, and nativePcm describes its private byte representation.
     * nativeChannelForHoro[i] names the native channel carrying effective channel i.
     * This mapping is a complete semantic permutation, not an implicit downmix or upmix.
     * Graph/layout conversion must produce the admitted effective output before this boundary.
     * Construction may allocate only on control; callback code borrows a retained validated plan.
     */
    struct AudioNegotiatedDeviceFormat final {
        AudioDeviceId device;
        std::uint64_t discoveryRevision{};
        std::uint64_t formatRevision{};
        AudioProcessingFormat effective;
        AudioProcessingFormat nativeSignal;
        AudioPcmFormat nativePcm;
        std::vector<std::uint8_t> nativeChannelForHoro;
        std::uint32_t callbackFrames{};
        std::optional<std::uint32_t> nativePeriodFrames; /**< Native-rate units; absent means unavailable, not zero or measured latency. */
        AudioDeviceRateConversion rateConversion{AudioDeviceRateConversion::None};
        AudioDeviceFormatDeviations deviations;
    };

    /** @brief Precise stage at which a candidate fails admission, without rewriting the request. */
    enum class AudioDeviceNegotiationStatus : std::uint8_t {
        InvalidRequest,
        SelectionFailed,
        StaleDevice,
        InvalidFormat,
        UnadmittedFormat,
        InvalidChannelMap,
        InvalidPeriod,
        UnsupportedRateConversion,
        UndeclaredDeviation,
        Accepted
    };

    /** @brief Admission outcome retaining the exact underlying discovery result for diagnostics. */
    struct AudioDeviceNegotiationResult final {
        AudioDeviceNegotiationStatus status{AudioDeviceNegotiationStatus::InvalidRequest};
        AudioDeviceResolution selection; /**< Evaluated only after format-policy validation; never alone proves candidate admission. */
    };

    /**
     * @brief Checks bounded format alternatives, callback limits and native-rate policy without allocation.
     * @param request Owned control policy; device selection is validated separately against discovery.
     * @return True for valid format policy; does not imply any backend can satisfy it.
     */
    [[nodiscard]] bool ValidateAudioDeviceFormatRequest(const AudioDeviceFormatRequest &request) noexcept;

    /**
     * @brief Admits a candidate against preserved intent and the current selected-backend discovery snapshot.
     * @param request Immutable requested preference and explicit limits/alternatives.
     * @param snapshot Current complete discovery facts of the runtime's fixed backend.
     * @param candidate Reported effective/native facts, never mutated by validation.
     * @return Stage-specific outcome and original discovery evidence; Accepted does not start a callback.
     * @pre Control owns all three immutable values during validation. Before publication it must also
     * prove format-revision freshness, optional-capability admission, prepared adapter resources and
     * the matching callback-ready handshake; this structural check cannot prove those lifecycle facts.
     */
    [[nodiscard]] AudioDeviceNegotiationResult ValidateAudioDeviceNegotiation(const AudioDeviceFormatRequest &request,
                                                                              const AudioDeviceSnapshot &snapshot,
                                                                              const AudioNegotiatedDeviceFormat &candidate) noexcept;
}  // namespace Horo::Audio
