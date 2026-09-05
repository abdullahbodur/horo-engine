#include "Horo/Audio/AudioFormat.h"

#include <algorithm>
#include <array>
#include <limits>

namespace Horo::Audio {
    static_assert(sizeof(AudioSample) == 4 && std::numeric_limits<AudioSample>::is_iec559);

    namespace {  // NOSONAR(cpp:S1000) - File-local validation helpers intentionally have internal linkage.
        /** @brief All admitted speaker roles in Horo's extended 7.1.4 order. */
        constexpr std::array SpeakerOrder{AudioSpeakerRole::FrontLeft,     AudioSpeakerRole::FrontRight,  AudioSpeakerRole::FrontCenter,
                                          AudioSpeakerRole::LowFrequency,  AudioSpeakerRole::SideLeft,    AudioSpeakerRole::SideRight,
                                          AudioSpeakerRole::BackLeft,      AudioSpeakerRole::BackRight,   AudioSpeakerRole::TopFrontLeft,
                                          AudioSpeakerRole::TopFrontRight, AudioSpeakerRole::TopBackLeft, AudioSpeakerRole::TopBackRight};

        /** @brief Speaker layouts carry unique known roles and no Ambisonic metadata. */
        bool ValidSpeakers(const AudioChannelLayoutView &layout) noexcept {
            if (layout.ambisonic)
                return false;
            std::array<bool, SpeakerOrder.size()> seen{};
            for (const auto &channel : layout.orderedChannels) {
                const auto *speaker = std::get_if<AudioSpeakerRole>(&channel);
                if (!speaker)
                    return false;
                const auto found = std::ranges::find(SpeakerOrder, *speaker);
                if (found == SpeakerOrder.end())
                    return false;
                const auto index = static_cast<std::size_t>(found - SpeakerOrder.begin());
                if (seen[index])
                    return false;
                seen[index] = true;
            }
            return true;
        }

        /** @brief Discrete channels use explicit sequential indices, not implied speaker positions. */
        bool ValidDiscrete(const AudioChannelLayoutView &layout) noexcept {
            if (layout.ambisonic)
                return false;
            for (std::size_t index = 0; index < layout.orderedChannels.size(); ++index) {
                const auto *channel = std::get_if<AudioDiscreteChannel>(&layout.orderedChannels[index]);
                if (!channel || channel->index != index)
                    return false;
            }
            return true;
        }

        /** @brief Bounds order before squaring and enforces canonical ACN/SN3D component order. */
        bool ValidAmbisonic(const AudioChannelLayoutView &layout) noexcept {
            if (!layout.ambisonic || layout.ambisonic->order > 3)
                return false;
            if (const auto side = static_cast<std::size_t>(layout.ambisonic->order) + 1; layout.orderedChannels.size() != side * side)
                return false;
            for (std::size_t index = 0; index < layout.orderedChannels.size(); ++index) {
                const auto *channel = std::get_if<AudioAmbisonicChannel>(&layout.orderedChannels[index]);
                if (!channel || channel->acn != index)
                    return false;
            }
            return true;
        }

        /** @brief Float containers carry exactly their IEEE payload with no integer alignment bits. */
        bool ValidFloat(const AudioPcmFormat &format) noexcept {
            return (format.bytesPerSample == 4 || format.bytesPerSample == 8) && format.significantBits == format.bytesPerSample * 8 &&
                   format.lsbPaddingBits == 0;
        }

        /** @brief Signed integer payload and low padding fit a bounded PCM container. */
        bool ValidSignedInteger(const AudioPcmFormat &format) noexcept {
            return format.bytesPerSample >= 1 && format.bytesPerSample <= 4 && format.significantBits >= 1 &&
                   format.significantBits + format.lsbPaddingBits <= format.bytesPerSample * 8;
        }

        /** @brief The admitted unsigned format is unpadded 8-bit offset binary. */
        bool ValidUnsignedInteger(const AudioPcmFormat &format) noexcept {
            return format.bytesPerSample == 1 && format.significantBits == 8 && format.lsbPaddingBits == 0;
        }
    }  // namespace

    /** @copydoc ViewAudioChannelLayout */
    AudioChannelLayoutView ViewAudioChannelLayout(const AudioChannelLayout &layout) noexcept {
        return {.kind = layout.kind, .orderedChannels = layout.orderedChannels, .ambisonic = layout.ambisonic};
    }

    /** @copydoc ValidateAudioChannelLayout */
    bool ValidateAudioChannelLayout(const AudioChannelLayoutView &layout, const std::uint32_t maximumChannels) noexcept {
        using enum AudioLayoutKind;
        if (maximumChannels == 0 || maximumChannels > MaximumAudioChannels || layout.orderedChannels.empty() ||
            layout.orderedChannels.size() > maximumChannels)
            return false;
        switch (layout.kind) {
            case Speaker:
                return ValidSpeakers(layout);
            case Discrete:
                return ValidDiscrete(layout);
            case Ambisonic:
                return ValidAmbisonic(layout);
        }
        return false;
    }

    /** @copydoc MakeAudioSpeakerLayout */
    AudioChannelLayout MakeAudioSpeakerLayout(const AudioSpeakerPreset preset) {
        using enum AudioSpeakerPreset;
        using enum AudioSpeakerRole;
        switch (preset) {
            case Mono:
                return {.orderedChannels = {FrontCenter}};
            case Stereo:
                return {.orderedChannels = {FrontLeft, FrontRight}};
            case TwoPointOne:
                return {.orderedChannels = {FrontLeft, FrontRight, LowFrequency}};
            case Quad:
                return {.orderedChannels = {FrontLeft, FrontRight, BackLeft, BackRight}};
            case FivePointOne:
                return {.orderedChannels = {SpeakerOrder.begin(), SpeakerOrder.begin() + 6}};
            case SevenPointOne:
                return {.orderedChannels = {SpeakerOrder.begin(), SpeakerOrder.begin() + 8}};
            case SevenPointOneFour:
                return {.orderedChannels = {SpeakerOrder.begin(), SpeakerOrder.end()}};
        }
        return {};
    }

    /** @copydoc ValidateAudioPcmFormat */
    bool ValidateAudioPcmFormat(const AudioPcmFormat &format) noexcept {
        using enum AudioPcmEncoding;
        if (format.packing != AudioPcmPacking::Planar && format.packing != AudioPcmPacking::Interleaved)
            return false;
        if (format.byteOrder != AudioByteOrder::LittleEndian && format.byteOrder != AudioByteOrder::BigEndian)
            return false;
        switch (format.encoding) {
            case IeeeFloat:
                return ValidFloat(format);
            case UnsignedInteger:
                return ValidUnsignedInteger(format);
            case SignedInteger:
                return ValidSignedInteger(format);
        }
        return false;
    }

    /** @copydoc ValidateAudioProcessingFormat */
    bool ValidateAudioProcessingFormat(const AudioProcessingFormat &format, const std::uint32_t maximumChannels) noexcept {
        return format.sampleRate >= MinimumAudioSampleRate && format.sampleRate <= MaximumAudioSampleRate &&
               ValidateAudioChannelLayout(ViewAudioChannelLayout(format.layout), maximumChannels);
    }
}  // namespace Horo::Audio
