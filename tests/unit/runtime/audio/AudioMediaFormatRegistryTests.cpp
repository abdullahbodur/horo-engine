#include "Horo/Audio/AudioErrors.h"
#include "Horo/Audio/AudioMediaFormatRegistry.h"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <string>

namespace Horo::Audio {
    namespace {
        const AudioPcmFormat SignedPcm16{.encoding = AudioPcmEncoding::SignedInteger,
                                         .byteOrder = AudioByteOrder::LittleEndian,
                                         .bytesPerSample = 2,
                                         .significantBits = 16};

        struct Fixture final {
            std::array<AudioContainerDescriptor, 2> containers{{{AudioContainerIds::Wave, "WAVE"}, {AudioContainerIds::Ogg, "Ogg"}}};
            std::array<AudioCodecDescriptor, 3> codecs{{{AudioCodecIds::Pcm, "PCM", AudioCodecPayloadKind::Pcm},
                                                        {AudioCodecIds::Vorbis, "Vorbis", AudioCodecPayloadKind::Compressed},
                                                        {AudioCodecIds::Opus, "Opus", AudioCodecPayloadKind::Compressed}}};
            std::array<AudioMediaFormatBinding, 3> bindings{{{AudioContainerIds::Wave, AudioCodecIds::Pcm, SignedPcm16},
                                                             {AudioContainerIds::Ogg, AudioCodecIds::Vorbis, std::nullopt},
                                                             {AudioContainerIds::Ogg, AudioCodecIds::Opus, std::nullopt}}};

            [[nodiscard]] AudioMediaFormatContributions Contributions() const noexcept {
                return {.containers = containers, .codecs = codecs, .bindings = bindings};
            }
        };

        template <typename T> void RequireError(const Result<T> &result, const ErrorCodeDescriptor &expected) {
            REQUIRE(result.HasError());
            REQUIRE(result.ErrorValue().code.Value() == expected.code.Value());
        }
    }  // namespace

    TEST_CASE("Audio format registry resolves only exact container codec and PCM tuples", "[unit][audio][format-registry]") {
        const Fixture fixture;
        auto result = AudioMediaFormatRegistry::Create(fixture.Contributions());
        REQUIRE(result.HasValue());
        AudioMediaFormatRegistry registry = std::move(result).Value();

        REQUIRE(registry.Containers().size() == 2);
        REQUIRE(registry.Codecs().size() == 3);
        REQUIRE(registry.Bindings().size() == 3);
        REQUIRE(registry.Resolve({AudioContainerIds::Wave, AudioCodecIds::Pcm, SignedPcm16}).HasValue());
        REQUIRE(registry.Resolve({AudioContainerIds::Ogg, AudioCodecIds::Vorbis, std::nullopt}).HasValue());
        REQUIRE(registry.Resolve({AudioContainerIds::Ogg, AudioCodecIds::Opus, std::nullopt}).HasValue());
    }

    TEST_CASE("Container discovery cannot substitute for codec or exact combination validation", "[unit][audio][format-registry]") {
        const Fixture fixture;
        auto created = AudioMediaFormatRegistry::Create(fixture.Contributions());
        REQUIRE(created.HasValue());
        const AudioMediaFormatRegistry registry = std::move(created).Value();

        RequireError(registry.Resolve({AudioContainerId{99}, AudioCodecIds::Vorbis, std::nullopt}), AudioErrors::FormatContainerUnknown);
        RequireError(registry.Resolve({AudioContainerIds::Ogg, AudioCodecId{99}, std::nullopt}), AudioErrors::FormatCodecUnknown);
        RequireError(registry.Resolve({AudioContainerIds::Wave, AudioCodecIds::Vorbis, std::nullopt}),
                     AudioErrors::FormatCombinationUnsupported);
        RequireError(registry.Resolve({AudioContainerIds::Ogg, AudioCodecIds::Pcm, SignedPcm16}),
                     AudioErrors::FormatCombinationUnsupported);
    }

    TEST_CASE("Audio format registry rejects malformed representation metadata", "[unit][audio][format-registry]") {
        Fixture fixture;
        fixture.bindings[0].pcm.reset();
        RequireError(AudioMediaFormatRegistry::Create(fixture.Contributions()), AudioErrors::FormatRegistryInvalid);

        fixture = Fixture{};
        fixture.bindings[1].pcm = SignedPcm16;
        RequireError(AudioMediaFormatRegistry::Create(fixture.Contributions()), AudioErrors::FormatRegistryInvalid);

        fixture = Fixture{};
        fixture.bindings[0].pcm->significantBits = 17;
        RequireError(AudioMediaFormatRegistry::Create(fixture.Contributions()), AudioErrors::FormatRegistryInvalid);

        const Fixture valid;
        auto created = AudioMediaFormatRegistry::Create(valid.Contributions());
        REQUIRE(created.HasValue());
        RequireError(created.Value().Resolve({AudioContainerIds::Wave, AudioCodecIds::Pcm, std::nullopt}),
                     AudioErrors::FormatRegistryInvalid);
        RequireError(created.Value().Resolve({AudioContainerIds::Ogg, AudioCodecIds::Vorbis, SignedPcm16}),
                     AudioErrors::FormatRegistryInvalid);
    }

    TEST_CASE("Audio format registry rejects unknown references and duplicate metadata", "[unit][audio][format-registry]") {
        Fixture fixture;
        fixture.bindings[0].container = AudioContainerId{99};
        RequireError(AudioMediaFormatRegistry::Create(fixture.Contributions()), AudioErrors::FormatRegistryInvalid);

        fixture = Fixture{};
        fixture.bindings[0].codec = AudioCodecId{99};
        RequireError(AudioMediaFormatRegistry::Create(fixture.Contributions()), AudioErrors::FormatRegistryInvalid);

        fixture = Fixture{};
        fixture.containers[1].id = fixture.containers[0].id;
        RequireError(AudioMediaFormatRegistry::Create(fixture.Contributions()), AudioErrors::FormatRegistryConflict);

        fixture = Fixture{};
        fixture.codecs[2].id = fixture.codecs[1].id;
        RequireError(AudioMediaFormatRegistry::Create(fixture.Contributions()), AudioErrors::FormatRegistryConflict);

        fixture = Fixture{};
        fixture.bindings[2] = fixture.bindings[1];
        RequireError(AudioMediaFormatRegistry::Create(fixture.Contributions()), AudioErrors::FormatRegistryConflict);
    }

    TEST_CASE("Audio format registry rejects invalid identities names and payload kinds", "[unit][audio][format-registry]") {
        Fixture fixture;
        fixture.containers[0].id = {};
        RequireError(AudioMediaFormatRegistry::Create(fixture.Contributions()), AudioErrors::FormatRegistryInvalid);

        fixture = Fixture{};
        fixture.codecs[0].id = {};
        RequireError(AudioMediaFormatRegistry::Create(fixture.Contributions()), AudioErrors::FormatRegistryInvalid);

        fixture = Fixture{};
        fixture.containers[0].displayName.clear();
        RequireError(AudioMediaFormatRegistry::Create(fixture.Contributions()), AudioErrors::FormatRegistryInvalid);

        fixture = Fixture{};
        fixture.codecs[0].payloadKind = static_cast<AudioCodecPayloadKind>(255);
        RequireError(AudioMediaFormatRegistry::Create(fixture.Contributions()), AudioErrors::FormatRegistryInvalid);
    }

    TEST_CASE("Audio format registry enforces construction and metadata bounds", "[unit][audio][format-registry]") {
        Fixture fixture;
        AudioMediaFormatRegistryLimits limits;
        limits.maximumContainers = 1;
        RequireError(AudioMediaFormatRegistry::Create(fixture.Contributions(), limits), AudioErrors::FormatRegistryCapacityExceeded);

        limits = {};
        limits.maximumCodecs = 2;
        RequireError(AudioMediaFormatRegistry::Create(fixture.Contributions(), limits), AudioErrors::FormatRegistryCapacityExceeded);

        limits = {};
        limits.maximumBindings = 2;
        RequireError(AudioMediaFormatRegistry::Create(fixture.Contributions(), limits), AudioErrors::FormatRegistryCapacityExceeded);

        limits = {};
        limits.maximumDisplayNameBytes = 3;
        RequireError(AudioMediaFormatRegistry::Create(fixture.Contributions(), limits), AudioErrors::FormatRegistryInvalid);

        limits = {};
        limits.maximumContainers = 0;
        RequireError(AudioMediaFormatRegistry::Create(fixture.Contributions(), limits), AudioErrors::FormatRegistryInvalid);
    }

    TEST_CASE("Audio format registry owns copied contribution metadata", "[unit][audio][format-registry]") {
        Fixture fixture;
        auto created = AudioMediaFormatRegistry::Create(fixture.Contributions());
        REQUIRE(created.HasValue());
        fixture.containers[0].displayName = "changed";
        fixture.codecs[0].displayName = "changed";
        fixture.bindings[0].container = AudioContainerIds::Ogg;

        REQUIRE(created.Value().Containers()[0].displayName == "WAVE");
        REQUIRE(created.Value().Codecs()[0].displayName == "PCM");
        REQUIRE(created.Value().Bindings()[0].container == AudioContainerIds::Wave);
    }

    TEST_CASE("Audio format registry errors expose stable distinct identities", "[unit][audio][format-registry]") {
        const std::array descriptors{&AudioErrors::FormatRegistryInvalid,  &AudioErrors::FormatRegistryCapacityExceeded,
                                     &AudioErrors::FormatRegistryConflict, &AudioErrors::FormatContainerUnknown,
                                     &AudioErrors::FormatCodecUnknown,     &AudioErrors::FormatCombinationUnsupported};
        for (std::size_t left = 0; left < descriptors.size(); ++left) {
            REQUIRE(descriptors[left]->domain.Value() == "horo.audio");
            REQUIRE_FALSE(descriptors[left]->code.Value().empty());
            for (std::size_t right = 0; right < left; ++right)
                REQUIRE(descriptors[left]->code.Value() != descriptors[right]->code.Value());
        }
    }
}  // namespace Horo::Audio
