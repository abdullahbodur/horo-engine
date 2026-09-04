#pragma once

/**
 * @file AudioBackendCapabilities.h
 * @brief Backend probe facts separated from selection, stream negotiation and activation.
 */

#include "Horo/Audio/AudioDeviceDiscovery.h"

#include <array>

namespace Horo::Audio {
    /** @brief Availability of the backend service/API, not an active stream. */
    enum class AudioBackendAvailability : std::uint8_t {
        NotProbed,
        Unavailable,
        Available
    };
    /** @brief Optional-feature evidence; Unknown is not false support and Unavailable is not Unsupported. */
    enum class AudioCapabilitySupport : std::uint8_t {
        Unknown,
        Unsupported,
        Unavailable,
        Available
    };

    /** @brief Typed optional features; array position is fixed by this versioned Horo contract. */
    enum class AudioBackendCapability : std::uint8_t {
        PhysicalEnumeration,
        NativeHotplug,
        HardwareLatency,
        ExclusiveMode,
        AggregateDevices,
        SessionControls,
        HardwareSpatialOutput,
        NativeDiagnostics,
        Count
    };

    /**
     * @brief Owned bounded probe facts produced outside the callback without opening the production stream.
     *
     * Compiled, hostSupported and availability are independent dimensions. Available requires
     * both build presence and host support but proves neither selection nor activation. A completed
     * probe has a non-zero monotonically increasing revision; NotProbed has revision zero.
     * backendVersion is privacy-safe API/runtime version text, at most 128 bytes, not a device ID.
     * Features describe backend-level support, not admission of a particular device/mode request.
     * No feature may report Available while backend availability is NotProbed or Unavailable.
     * Null reports every listed native/physical feature Unsupported; it cannot qualify hardware.
     */
    struct AudioBackendProbe final {
        std::uint32_t contractVersion{1};
        AudioBackendKind backend{AudioBackendKind::NullAudio};
        bool compiled{};
        bool hostSupported{};
        AudioBackendAvailability availability{AudioBackendAvailability::NotProbed};
        std::uint64_t revision{};
        std::string backendVersion;
        std::array<AudioCapabilitySupport, static_cast<std::size_t>(AudioBackendCapability::Count)> features{};
    };

    /**
     * @brief Validates probe shape, distinct availability facts and Null's lack of native capabilities.
     * @param probe Immutable completed or unperformed probe facts; no native operations are performed.
     * @return True for a structurally coherent report; not evidence of hardware qualification or feature correctness.
     */
    [[nodiscard]] bool ValidateAudioBackendProbe(const AudioBackendProbe &probe) noexcept;

    /**
     * @brief Reads one feature without converting missing or invalid evidence to support.
     * @param probe Backend probe that remains alive for the call.
     * @param capability Typed optional feature.
     * @return Reported support, or Unknown for an invalid report or feature identity.
     */
    [[nodiscard]] AudioCapabilitySupport QueryAudioBackendCapability(const AudioBackendProbe &probe,
                                                                     AudioBackendCapability capability) noexcept;
}  // namespace Horo::Audio
