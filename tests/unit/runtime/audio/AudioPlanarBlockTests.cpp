#include "Horo/Audio/AudioPlanarBlock.h"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <limits>
#include <type_traits>

#if __has_include("Horo/Audio/Internal/AudioBackend.h")
#error "AudioApi must not publish the non-installed backend interface"
#endif

namespace Horo::Audio {
    namespace {
        /** @brief Builds the repeated borrowed-view shape while each test retains its own storage. */
        AudioPlanarBlockView BlockView(const AudioProcessingFormat &format, const std::span<AudioSample *const> planes,
                                       const std::uint32_t validFrames, const std::uint32_t capacityFrames) {
            return {.layout = ViewAudioChannelLayout(format.layout),
                    .sampleRate = format.sampleRate,
                    .planes = planes,
                    .validFrames = validFrames,
                    .capacityFrames = capacityFrames};
        }

        struct BufferFixture final {
            AudioProcessingFormat format{48'000, MakeAudioSpeakerLayout(AudioSpeakerPreset::Stereo)};
            alignas(64) std::array<AudioSample, 64> storage{};
            std::array<AudioSample *, 2> planes{storage.data(), storage.data() + 32};

            AudioPlanarBlockView View() const {
                return BlockView(format, planes, 24, 32);
            }
        };

        TEST_CASE("Audio planar validation preserves aligned adjacent buffers and permits empty work", "[audio][buffer]") {
            BufferFixture fixture;
            fixture.storage.fill(0.25F);
            auto block = fixture.View();
            REQUIRE(ValidateAudioPlanarBlock(block, fixture.format));
            block.validFrames = 0;
            REQUIRE(ValidateAudioPlanarBlock(block, fixture.format));
            std::swap(fixture.planes[0], fixture.planes[1]);
            REQUIRE(ValidateAudioPlanarBlock(block, fixture.format));
            for (const auto sample : fixture.storage)
                REQUIRE(sample == 0.25F);
            static_assert(std::is_trivially_copyable_v<AudioPlanarBlockView>);
        }

        TEST_CASE("Audio planar validation rejects malformed dimensions before inspecting planes", "[audio][buffer]") {
            const BufferFixture fixture;
            using Mutation = void (*)(AudioPlanarBlockView &);
            const std::array<Mutation, 6> mutations{
                [](auto &block) {
                block.sampleRate = 44'100;
            },
                [](auto &block) {
                block.validFrames = 33;
            },
                [](auto &block) {
                block.validFrames = 0;
                block.capacityFrames = 0;
            },
                [](auto &block) {
                block.capacityFrames = MaximumAudioCallbackFrames + 1;
            },
                [](auto &block) {
                block.planes = {};
            },
                [](auto &block) {
                block.planes = block.planes.first(1);
            },
            };
            for (const auto mutate : mutations) {
                auto block = fixture.View();
                mutate(block);
                REQUIRE_FALSE(ValidateAudioPlanarBlock(block, fixture.format));
            }
            REQUIRE_FALSE(ValidateAudioPlanarBlock(fixture.View(), {}));
        }

        TEST_CASE("Audio planar validation compares semantic channel roles and metadata", "[audio][buffer]") {
            const BufferFixture fixture;
            auto block = fixture.View();
            block.layout.kind = AudioLayoutKind::Discrete;
            REQUIRE_FALSE(ValidateAudioPlanarBlock(block, fixture.format));
            block = fixture.View();
            block.layout.ambisonic = AmbisonicDescriptor{0};
            REQUIRE_FALSE(ValidateAudioPlanarBlock(block, fixture.format));
            block = fixture.View();
            auto reversed = fixture.format.layout;
            std::swap(reversed.orderedChannels[0], reversed.orderedChannels[1]);
            block.layout = ViewAudioChannelLayout(reversed);
            REQUIRE_FALSE(ValidateAudioPlanarBlock(block, fixture.format));
            block.layout.orderedChannels = block.layout.orderedChannels.first(1);
            REQUIRE_FALSE(ValidateAudioPlanarBlock(block, fixture.format));
        }

        TEST_CASE("Audio planar validation rejects invalid and overlapping plane addresses", "[audio][buffer]") {
            BufferFixture fixture;
            for (auto *address :
                 {static_cast<AudioSample *>(nullptr), fixture.storage.data() + 1, fixture.storage.data(), fixture.storage.data() + 16}) {
                fixture.planes[1] = address;
                REQUIRE_FALSE(ValidateAudioPlanarBlock(fixture.View(), fixture.format));
            }
            fixture.planes = {fixture.storage.data() + 16, fixture.storage.data()};
            REQUIRE_FALSE(ValidateAudioPlanarBlock(fixture.View(), fixture.format));
            // Shape-only validation must reject wraparound without dereferencing this synthetic address.
            constexpr auto lastAligned = std::numeric_limits<std::uintptr_t>::max() & ~std::uintptr_t{63};
            fixture.planes[1] = reinterpret_cast<AudioSample *>(lastAligned);
            REQUIRE_FALSE(ValidateAudioPlanarBlock(fixture.View(), fixture.format));
        }

        TEST_CASE("Audio planar capacity limit includes its maximum and does not require a full block", "[audio][buffer]") {
            const AudioProcessingFormat format{MinimumAudioSampleRate, MakeAudioSpeakerLayout(AudioSpeakerPreset::Mono)};
            alignas(64) std::array<AudioSample, MaximumAudioCallbackFrames> storage{};
            const std::array<AudioSample *, 1> planes{storage.data()};
            auto block = BlockView(format, planes, MaximumAudioCallbackFrames, MaximumAudioCallbackFrames);
            REQUIRE(ValidateAudioPlanarBlock(block, format));
            block.capacityFrames = 1;
            block.validFrames = 1;
            REQUIRE(ValidateAudioPlanarBlock(block, format));
        }

        TEST_CASE("Audio planar buffers preserve canonical ambisonic metadata", "[audio][buffer]") {
            const AudioProcessingFormat format{48'000,
                                               {.kind = AudioLayoutKind::Ambisonic,
                                                .orderedChannels = {AudioAmbisonicChannel{0}},
                                                .ambisonic = AmbisonicDescriptor{0}}};
            alignas(64) std::array<AudioSample, 16> storage{};
            const std::array<AudioSample *, 1> planes{storage.data()};
            auto block = BlockView(format, planes, 16, 16);
            REQUIRE(ValidateAudioPlanarBlock(block, format));
            block.layout.ambisonic.reset();
            REQUIRE_FALSE(ValidateAudioPlanarBlock(block, format));
        }
    }  // namespace
}  // namespace Horo::Audio
