#include "Horo/Audio/AudioFormat.h"

#include <array>
#include <bit>
#include <catch2/catch_test_macros.hpp>
#include <limits>

namespace Horo::Audio {
    namespace {
        bool Valid(const AudioChannelLayout &layout, const std::uint32_t limit = 64) {
            return ValidateAudioChannelLayout(ViewAudioChannelLayout(layout), limit);
        }

        AudioChannelLayout Discrete(const std::uint8_t count) {
            AudioChannelLayout layout{.kind = AudioLayoutKind::Discrete};
            for (std::uint8_t index = 0; index < count; ++index)
                layout.orderedChannels.emplace_back(AudioDiscreteChannel{index});
            return layout;
        }

        AudioChannelLayout Ambisonic(const std::uint8_t order) {
            AudioChannelLayout layout{.kind = AudioLayoutKind::Ambisonic, .ambisonic = AmbisonicDescriptor{order}};
            const auto count = (order + 1) * (order + 1);
            for (std::uint8_t index = 0; index < count; ++index)
                layout.orderedChannels.emplace_back(AudioAmbisonicChannel{index});
            return layout;
        }

        TEST_CASE("Audio speaker presets preserve exact semantic orders", "[audio][format]") {
            using enum AudioSpeakerRole;
            const std::array<std::vector<AudioChannelRole>, 7> expected{std::vector<AudioChannelRole>{FrontCenter},
                                                                        {FrontLeft, FrontRight},
                                                                        {FrontLeft, FrontRight, LowFrequency},
                                                                        {FrontLeft, FrontRight, BackLeft, BackRight},
                                                                        {FrontLeft, FrontRight, FrontCenter, LowFrequency, SideLeft,
                                                                         SideRight},
                                                                        {FrontLeft, FrontRight, FrontCenter, LowFrequency, SideLeft,
                                                                         SideRight, BackLeft, BackRight},
                                                                        {FrontLeft, FrontRight, FrontCenter, LowFrequency, SideLeft,
                                                                         SideRight, BackLeft, BackRight, TopFrontLeft, TopFrontRight,
                                                                         TopBackLeft, TopBackRight}};
            const std::array presets{AudioSpeakerPreset::Mono,
                                     AudioSpeakerPreset::Stereo,
                                     AudioSpeakerPreset::TwoPointOne,
                                     AudioSpeakerPreset::Quad,
                                     AudioSpeakerPreset::FivePointOne,
                                     AudioSpeakerPreset::SevenPointOne,
                                     AudioSpeakerPreset::SevenPointOneFour};
            for (std::size_t index = 0; index < presets.size(); ++index) {
                const auto layout = MakeAudioSpeakerLayout(presets[index]);
                REQUIRE(layout.kind == AudioLayoutKind::Speaker);
                REQUIRE(layout.orderedChannels == expected[index]);
                REQUIRE_FALSE(layout.ambisonic);
                REQUIRE(Valid(layout));
            }
            REQUIRE_FALSE(Valid(MakeAudioSpeakerLayout(static_cast<AudioSpeakerPreset>(255))));
        }

        TEST_CASE("Audio speaker validation rejects ambiguous roles and profile violations", "[audio][format]") {
            auto stereo = MakeAudioSpeakerLayout(AudioSpeakerPreset::Stereo);
            REQUIRE_FALSE(Valid(stereo, 0));
            REQUIRE_FALSE(Valid(stereo, 65));
            REQUIRE_FALSE(Valid(stereo, 1));
            REQUIRE(Valid(stereo, 2));
            stereo.kind = static_cast<AudioLayoutKind>(255);
            REQUIRE_FALSE(Valid(stereo));
            stereo.kind = AudioLayoutKind::Speaker;
            stereo.ambisonic = AmbisonicDescriptor{0};
            REQUIRE_FALSE(Valid(stereo));
            stereo.ambisonic.reset();
            stereo.orderedChannels[1] = AudioSpeakerRole::FrontLeft;
            REQUIRE_FALSE(Valid(stereo));
            stereo.orderedChannels[1] = static_cast<AudioSpeakerRole>(255);
            REQUIRE_FALSE(Valid(stereo));
            stereo.orderedChannels[1] = AudioDiscreteChannel{1};
            REQUIRE_FALSE(Valid(stereo));
            stereo.orderedChannels.clear();
            REQUIRE_FALSE(Valid(stereo));
        }

        TEST_CASE("Audio discrete channels retain ordered indices without speaker meaning", "[audio][format]") {
            auto layout = Discrete(64);
            REQUIRE(Valid(layout));
            REQUIRE_FALSE(Valid(layout, 63));
            layout.orderedChannels.push_back(AudioDiscreteChannel{64});
            REQUIRE_FALSE(Valid(layout));
            layout.orderedChannels.pop_back();
            layout.ambisonic = AmbisonicDescriptor{0};
            REQUIRE_FALSE(Valid(layout));
            layout.ambisonic.reset();
            layout.orderedChannels[0] = AudioDiscreteChannel{1};
            REQUIRE_FALSE(Valid(layout));
            layout.orderedChannels[0] = AudioSpeakerRole::FrontLeft;
            REQUIRE_FALSE(Valid(layout));
            const auto stereo = MakeAudioSpeakerLayout(AudioSpeakerPreset::Stereo);
            REQUIRE(Discrete(2) != stereo);
        }

        TEST_CASE("Audio Ambisonic layouts enforce bounded ACN and SN3D metadata", "[audio][format]") {
            for (std::uint8_t order = 0; order <= 3; ++order) {
                auto layout = Ambisonic(order);
                REQUIRE(Valid(layout));
                REQUIRE(layout.orderedChannels.size() == (order + 1) * (order + 1));
                layout.orderedChannels[0] = AudioAmbisonicChannel{1};
                REQUIRE_FALSE(Valid(layout));
                layout.orderedChannels[0] = AudioDiscreteChannel{0};
                REQUIRE_FALSE(Valid(layout));
            }
            auto layout = Ambisonic(1);
            layout.orderedChannels.pop_back();
            REQUIRE_FALSE(Valid(layout));
            layout = Ambisonic(0);
            layout.ambisonic->order = 4;
            REQUIRE_FALSE(Valid(layout));
            layout.ambisonic->order = 255;
            REQUIRE_FALSE(Valid(layout));
            layout.ambisonic.reset();
            REQUIRE_FALSE(Valid(layout));
        }

        TEST_CASE("Audio owning layouts copy independently and views borrow retained channels", "[audio][format]") {
            const auto original = MakeAudioSpeakerLayout(AudioSpeakerPreset::FivePointOne);
            auto copied = original;
            REQUIRE(copied == original);
            REQUIRE(copied.orderedChannels.data() != original.orderedChannels.data());
            copied.orderedChannels[4] = AudioSpeakerRole::BackLeft;
            REQUIRE(copied != original);
            REQUIRE(Valid(copied));
            const auto moved = std::move(copied);
            const auto view = ViewAudioChannelLayout(moved);
            REQUIRE(view.orderedChannels.data() == moved.orderedChannels.data());
            REQUIRE(view.orderedChannels.size() == moved.orderedChannels.size());
            REQUIRE(ValidateAudioChannelLayout(view));
            REQUIRE(std::get<AudioSpeakerRole>(original.orderedChannels[4]) == AudioSpeakerRole::SideLeft);
        }

        TEST_CASE("Audio processing format admission preserves explicit rates and binary32 samples", "[audio][format]") {
            AudioProcessingFormat format{.sampleRate = 48'000, .layout = MakeAudioSpeakerLayout(AudioSpeakerPreset::Stereo)};
            for (const auto rate : {8'000U, 44'100U, 48'000U, 192'000U, 384'000U}) {
                format.sampleRate = rate;
                REQUIRE(ValidateAudioProcessingFormat(format));
                REQUIRE(format.sampleRate == rate);
            }
            for (const auto rate : {0U, 7'999U, 384'001U, std::numeric_limits<std::uint32_t>::max()}) {
                format.sampleRate = rate;
                REQUIRE_FALSE(ValidateAudioProcessingFormat(format));
            }
            format.sampleRate = 48'000;
            REQUIRE_FALSE(ValidateAudioProcessingFormat(format, 1));
            format.layout = {};
            REQUIRE_FALSE(ValidateAudioProcessingFormat(format));
            REQUIRE(std::bit_cast<std::uint32_t>(AudioSample{0.0F}) == 0);
            REQUIRE(std::numeric_limits<AudioSample>::digits == 24);
        }

        TEST_CASE("Audio native PCM metadata admits both packing and byte orders", "[audio][format]") {
            AudioPcmFormat format;
            for (const auto packing : {AudioPcmPacking::Planar, AudioPcmPacking::Interleaved}) {
                for (const auto order : {AudioByteOrder::LittleEndian, AudioByteOrder::BigEndian}) {
                    format.packing = packing;
                    format.byteOrder = order;
                    REQUIRE(ValidateAudioPcmFormat(format));
                }
            }
            format.packing = static_cast<AudioPcmPacking>(255);
            REQUIRE_FALSE(ValidateAudioPcmFormat(format));
            format.packing = AudioPcmPacking::Planar;
            format.byteOrder = static_cast<AudioByteOrder>(255);
            REQUIRE_FALSE(ValidateAudioPcmFormat(format));
            format.byteOrder = AudioByteOrder::LittleEndian;
            format.encoding = static_cast<AudioPcmEncoding>(255);
            REQUIRE_FALSE(ValidateAudioPcmFormat(format));
        }

        TEST_CASE("Audio native float metadata rejects integer padding and non IEEE containers", "[audio][format]") {
            AudioPcmFormat format;
            REQUIRE(ValidateAudioPcmFormat(format));
            format.bytesPerSample = 8;
            format.significantBits = 64;
            REQUIRE(ValidateAudioPcmFormat(format));
            format.lsbPaddingBits = 1;
            REQUIRE_FALSE(ValidateAudioPcmFormat(format));
            format.lsbPaddingBits = 0;
            format.significantBits = 63;
            REQUIRE_FALSE(ValidateAudioPcmFormat(format));
            format.bytesPerSample = 3;
            format.significantBits = 24;
            REQUIRE_FALSE(ValidateAudioPcmFormat(format));
        }

        TEST_CASE("Audio native signed PCM describes packed and padded payloads safely", "[audio][format]") {
            AudioPcmFormat format{.encoding = AudioPcmEncoding::SignedInteger};
            for (std::uint8_t bytes = 1; bytes <= 4; ++bytes) {
                format.bytesPerSample = bytes;
                format.significantBits = static_cast<std::uint8_t>(bytes * 8);
                REQUIRE(ValidateAudioPcmFormat(format));
            }
            format.significantBits = 24;
            format.lsbPaddingBits = 8;
            REQUIRE(ValidateAudioPcmFormat(format));
            format.lsbPaddingBits = 9;
            REQUIRE_FALSE(ValidateAudioPcmFormat(format));
            format.lsbPaddingBits = 255;
            format.significantBits = 255;
            REQUIRE_FALSE(ValidateAudioPcmFormat(format));
            format.lsbPaddingBits = 0;
            format.significantBits = 0;
            REQUIRE_FALSE(ValidateAudioPcmFormat(format));
            format.significantBits = 8;
            format.bytesPerSample = 0;
            REQUIRE_FALSE(ValidateAudioPcmFormat(format));
            format.bytesPerSample = 5;
            REQUIRE_FALSE(ValidateAudioPcmFormat(format));
        }

        TEST_CASE("Audio native unsigned PCM admits only unpadded offset binary eight bit", "[audio][format]") {
            AudioPcmFormat format{.encoding = AudioPcmEncoding::UnsignedInteger, .bytesPerSample = 1, .significantBits = 8};
            REQUIRE(ValidateAudioPcmFormat(format));
            format.lsbPaddingBits = 1;
            REQUIRE_FALSE(ValidateAudioPcmFormat(format));
            format.lsbPaddingBits = 0;
            format.significantBits = 7;
            REQUIRE_FALSE(ValidateAudioPcmFormat(format));
            format.significantBits = 8;
            format.bytesPerSample = 2;
            REQUIRE_FALSE(ValidateAudioPcmFormat(format));
        }
    }  // namespace
}  // namespace Horo::Audio
