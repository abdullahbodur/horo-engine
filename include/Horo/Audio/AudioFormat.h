#pragma once

/**
 * @file AudioFormat.h
 * @brief Horo-owned semantic channel layouts and native PCM representation metadata.
 */

#include <compare>
#include <cstdint>
#include <optional>
#include <span>
#include <variant>
#include <vector>

namespace Horo::Audio {
    /** @brief Canonical processing sample; native PCM representations stay at adapter boundaries. */
    using AudioSample = float;

    /** @brief Lower sample-rate bound of the current processing profile, in samples per second. */
    inline constexpr std::uint32_t MinimumAudioSampleRate = 8'000;
    /** @brief Upper sample-rate bound of the current processing profile, in samples per second. */
    inline constexpr std::uint32_t MaximumAudioSampleRate = 384'000;
    /** @brief Maximum semantic channels admitted by the current processing profile. */
    inline constexpr std::uint32_t MaximumAudioChannels = 64;

    /** @brief Semantic layout families, never inferred from channel count. */
    enum class AudioLayoutKind : std::uint8_t {
        Speaker,
        Ambisonic,
        Discrete
    };

    /** @brief Horo speaker roles; numeric values are not native channel masks or positions. */
    enum class AudioSpeakerRole : std::uint8_t {
        FrontLeft,
        FrontRight,
        FrontCenter,
        LowFrequency,
        SideLeft,
        SideRight,
        BackLeft,
        BackRight,
        TopFrontLeft,
        TopFrontRight,
        TopBackLeft,
        TopBackRight
    };

    /** @brief Position in a discrete layout, with no implied speaker meaning. */
    struct AudioDiscreteChannel final {
        std::uint8_t index{};
        auto operator<=>(const AudioDiscreteChannel &) const noexcept = default;
    };

    /** @brief Real-valued ACN component; only canonical SN3D normalization is represented here. */
    struct AudioAmbisonicChannel final {
        std::uint8_t acn{};
        auto operator<=>(const AudioAmbisonicChannel &) const noexcept = default;
    };

    /** @brief Typed channel meaning; speaker, discrete and ACN roles cannot alias by integer value. */
    using AudioChannelRole = std::variant<AudioSpeakerRole, AudioDiscreteChannel, AudioAmbisonicChannel>;

    /** @brief Canonical AmbiX metadata; orders 0 through 3 use ACN order and SN3D normalization. */
    struct AmbisonicDescriptor final {
        std::uint8_t order{};
        auto operator<=>(const AmbisonicDescriptor &) const noexcept = default;
    };

    /**
     * @brief Owning layout constructed on control, then retained immutable for its processing epoch.
     * Empty layouts are invalid. Native channel enums never enter orderedChannels. Noncanonical
     * Ambisonic representations require explicit adapter conversion before constructing this value.
     */
    struct AudioChannelLayout final {
        AudioLayoutKind kind{AudioLayoutKind::Speaker};
        std::vector<AudioChannelRole> orderedChannels;
        std::optional<AmbisonicDescriptor> ambisonic;
        bool operator==(const AudioChannelLayout &) const = default;
    };

    /** @brief Borrowed layout valid only while the immutable owning layout remains alive and unchanged. */
    struct AudioChannelLayoutView final {
        AudioLayoutKind kind{AudioLayoutKind::Speaker};
        std::span<const AudioChannelRole> orderedChannels;
        std::optional<AmbisonicDescriptor> ambisonic;
    };

    /** @brief Named speaker presets in the exact ADR-063 processing order. */
    enum class AudioSpeakerPreset : std::uint8_t {
        Mono,
        Stereo,
        TwoPointOne,
        Quad,
        FivePointOne,
        SevenPointOne,
        SevenPointOneFour
    };

    /** @brief Native sample coding, described without OS sample-format enums. */
    enum class AudioPcmEncoding : std::uint8_t {
        SignedInteger,
        UnsignedInteger,
        IeeeFloat
    };
    /** @brief Private native buffer organization, not permission to change the canonical planar mixer. */
    enum class AudioPcmPacking : std::uint8_t {
        Planar,
        Interleaved
    };
    /** @brief Explicit serialized/native sample byte order, independent of the compiling host. */
    enum class AudioByteOrder : std::uint8_t {
        LittleEndian,
        BigEndian
    };

    /**
     * @brief Native PCM sample metadata; contains no pointer or ownership of native buffers.
     *
     * Signed PCM admits 1–32 significant bits in 1–4 bytes; lsbPaddingBits names unused
     * low bits (for example 24-bit left-aligned PCM in 32 bits uses 8). Unsigned PCM
     * admits 8-bit offset binary only. Float admits IEEE binary32/binary64 without padding.
     * Native adapters own conversion, final clamp, rounding and dither policy.
     */
    struct AudioPcmFormat final {
        AudioPcmEncoding encoding{AudioPcmEncoding::IeeeFloat};
        AudioPcmPacking packing{AudioPcmPacking::Interleaved};
        AudioByteOrder byteOrder{AudioByteOrder::LittleEndian};
        std::uint8_t bytesPerSample{4};
        std::uint8_t significantBits{32};
        std::uint8_t lsbPaddingBits{};
        bool operator==(const AudioPcmFormat &) const = default;
    };

    /** @brief Explicit rate and semantic layout of canonical planar binary32 processing. */
    struct AudioProcessingFormat final {
        std::uint32_t sampleRate{}; /**< Samples/second; baseline admission is 8,000–384,000. */
        AudioChannelLayout layout;
        bool operator==(const AudioProcessingFormat &) const = default;
    };

    /**
     * @brief Borrows an owning layout without allocation or extending its lifetime.
     * @param layout Retained immutable layout; temporaries are deliberately rejected.
     * @return View whose lifetime cannot exceed layout or survive mutation of its channel vector.
     */
    [[nodiscard]] AudioChannelLayoutView ViewAudioChannelLayout(const AudioChannelLayout &layout) noexcept;
    AudioChannelLayoutView ViewAudioChannelLayout(AudioChannelLayout &&) = delete;
    AudioChannelLayoutView ViewAudioChannelLayout(const AudioChannelLayout &&) = delete;

    /**
     * @brief Validates bounded semantic roles, order and canonical Ambisonic metadata without allocation.
     * @param layout Borrowed layout whose owner remains alive for this call.
     * @param maximumChannels Active profile limit, in 1–64; speaker roles may appear in any explicit unique order.
     * @return True for a valid layout within the profile; does not imply an available spatial/layout converter.
     */
    [[nodiscard]] bool ValidateAudioChannelLayout(const AudioChannelLayoutView &layout,
                                                  std::uint32_t maximumChannels = MaximumAudioChannels) noexcept;

    /**
     * @brief Constructs an owning speaker preset on control, in exact Horo plane order.
     * @param preset Known ADR-063 preset.
     * @return Owning layout, or an empty invalid layout for an unknown preset.
     * @throws std::bad_alloc When control-thread layout allocation fails.
     */
    [[nodiscard]] AudioChannelLayout MakeAudioSpeakerLayout(AudioSpeakerPreset preset);

    /** @brief Validates the native PCM metadata. @param format Reported metadata. @return True for an admitted representation. */
    [[nodiscard]] bool ValidateAudioPcmFormat(const AudioPcmFormat &format) noexcept;

    /**
     * @brief Checks the baseline sample-rate range and semantic layout independently of device negotiation.
     * @param format Canonical processing descriptor.
     * @param maximumChannels Active profile channel limit in 1–64.
     * @return True for a structurally admitted format, not evidence of device support.
     */
    [[nodiscard]] bool ValidateAudioProcessingFormat(const AudioProcessingFormat &format,
                                                     std::uint32_t maximumChannels = MaximumAudioChannels) noexcept;
}  // namespace Horo::Audio
