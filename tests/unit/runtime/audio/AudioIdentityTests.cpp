#include "AudioHandleRegistry.h"
#include "Horo/Audio/AudioErrors.h"
#include "Horo/Audio/AudioIdentity.h"

#include <catch2/catch_test_macros.hpp>
#include <string>
#include <type_traits>

namespace Horo::Audio {
    namespace {
        template <typename Id> Id StableId(const std::uint64_t value) {
            Result<Id> parsed = Id::Create(value);
            REQUIRE(parsed.HasValue());
            return std::move(parsed).Value();
        }

        TEST_CASE("Audio identities are strongly typed and reject zero values", "[unit][audio][identity]") {
            REQUIRE(AudioBusId::Create(0).HasError());
            REQUIRE(AudioParameterId::Create(0).HasError());
            REQUIRE(AudioEventId::Create(0).HasError());

            const AudioBusId bus = StableId<AudioBusId>(11);
            const AudioParameterId parameter = StableId<AudioParameterId>(11);
            const AudioEventId event = StableId<AudioEventId>(11);
            REQUIRE(bus.IsValid());
            REQUIRE(parameter.IsValid());
            REQUIRE(event.IsValid());
            static_assert(!std::is_same_v<AudioBusId, AudioParameterId>);
            static_assert(!std::is_same_v<AudioParameterId, AudioEventId>);
        }

        TEST_CASE("Audio asset identities preserve canonical asset identity", "[unit][audio][identity]") {
            REQUIRE(AudioClipId::Create({}).HasError());
            REQUIRE(AudioSoundId::Create({}).HasError());

            const auto asset = Assets::AssetId::Parse("032bc03d-4b5f-48dc-9ae7-493cbce36e12");
            REQUIRE(asset.HasValue());
            const auto clip = AudioClipId::Create(asset.Value());
            const auto sound = AudioSoundId::Create(asset.Value());
            REQUIRE(clip.HasValue());
            REQUIRE(sound.HasValue());
            REQUIRE((clip.Value().Asset() == asset.Value()));
            REQUIRE((sound.Value().Asset() == asset.Value()));
            static_assert(!std::is_same_v<AudioClipId, AudioSoundId>);
        }

        TEST_CASE("Audio handles require owner slot and generation", "[unit][audio][identity]") {
            const AudioRuntimeId owner = StableId<AudioRuntimeId>(7);
            REQUIRE_FALSE(AudioVoiceHandle{}.IsValid());
            REQUIRE_FALSE(AudioVoiceHandle{owner, 1, 0}.IsValid());
            REQUIRE(AudioVoiceHandle{owner, 1, 1}.IsValid());
            static_assert(!std::is_same_v<AudioVoiceHandle, AudioClipHandle>);
            static_assert(!std::is_same_v<AudioDeviceId, AudioEventInstanceId>);
        }

        TEST_CASE("Audio registry rejects malformed and foreign handles", "[unit][audio][identity]") {
            const AudioRuntimeId owner = StableId<AudioRuntimeId>(7);
            auto created = Detail::AudioHandleRegistry<AudioVoiceHandle>::Create(owner, {.maximumSlots = 2});
            REQUIRE(created.HasValue());
            auto registry = std::move(created).Value();

            const auto malformed = registry.Resolve({});
            REQUIRE(malformed.HasError());
            REQUIRE((malformed.ErrorValue().code.Value() == AudioErrors::HandleMalformed.code.Value()));

            const AudioVoiceHandle foreign{StableId<AudioRuntimeId>(8), 1, 1};
            const auto mismatched = registry.Resolve(foreign);
            REQUIRE(mismatched.HasError());
            REQUIRE((mismatched.ErrorValue().code.Value() == AudioErrors::HandleOwnerMismatch.code.Value()));
            REQUIRE((mismatched.ErrorValue().message.find("owner 8, slot 1, generation 1") != std::string::npos));
        }

        TEST_CASE("Stale audio handle cannot resolve a reused slot", "[unit][audio][identity]") {
            const AudioRuntimeId owner = StableId<AudioRuntimeId>(21);
            auto created = Detail::AudioHandleRegistry<AudioVoiceHandle>::Create(owner, {.maximumSlots = 1});
            REQUIRE(created.HasValue());
            auto registry = std::move(created).Value();

            const auto first = registry.Acquire();
            REQUIRE(first.HasValue());
            REQUIRE((registry.ActiveCount() == 1));
            const auto full = registry.Acquire();
            REQUIRE(full.HasError());
            REQUIRE((full.ErrorValue().code.Value() == AudioErrors::HandleCapacityExhausted.code.Value()));
            REQUIRE(registry.Release(first.Value()).HasValue());
            REQUIRE(registry.Release(first.Value()).HasError());

            const auto replacement = registry.Acquire();
            REQUIRE(replacement.HasValue());
            REQUIRE((replacement.Value().slot == first.Value().slot));
            REQUIRE((replacement.Value().generation == first.Value().generation + 1));
            REQUIRE(registry.Resolve(replacement.Value()).HasValue());

            const auto stale = registry.Resolve(first.Value());
            REQUIRE(stale.HasError());
            REQUIRE((stale.ErrorValue().code.Value() == AudioErrors::HandleStale.code.Value()));
            REQUIRE((stale.ErrorValue().message.find("slot 1, generation 1") != std::string::npos));
            const auto absent = registry.Resolve(AudioVoiceHandle{owner, 2, 1});
            REQUIRE(absent.HasError());
            REQUIRE((absent.ErrorValue().code.Value() == AudioErrors::HandleStale.code.Value()));
        }

        TEST_CASE("Exhausted audio handle generations are never reused", "[unit][audio][identity]") {
            const AudioRuntimeId owner = StableId<AudioRuntimeId>(34);
            auto created = Detail::AudioHandleRegistry<AudioVoiceHandle>::Create(owner, {.maximumSlots = 1, .maximumGeneration = 2});
            REQUIRE(created.HasValue());
            auto registry = std::move(created).Value();

            const auto first = registry.Acquire();
            REQUIRE(first.HasValue());
            REQUIRE(registry.Release(first.Value()).HasValue());
            const auto second = registry.Acquire();
            REQUIRE(second.HasValue());
            REQUIRE((second.Value().generation == 2));
            REQUIRE(registry.Release(second.Value()).HasValue());

            const auto exhausted = registry.Acquire();
            REQUIRE(exhausted.HasError());
            REQUIRE((exhausted.ErrorValue().code.Value() == AudioErrors::HandleGenerationExhausted.code.Value()));
            REQUIRE((registry.ActiveCount() == 0));
        }

        TEST_CASE("Audio handle registry validates bounded construction", "[unit][audio][identity]") {
            REQUIRE(Detail::AudioHandleRegistry<AudioVoiceHandle>::Create({}, {}).HasError());
            const AudioRuntimeId owner = StableId<AudioRuntimeId>(55);
            REQUIRE(Detail::AudioHandleRegistry<AudioVoiceHandle>::Create(owner, {.maximumSlots = 0}).HasError());
            REQUIRE(Detail::AudioHandleRegistry<AudioVoiceHandle>::Create(owner, {.maximumGeneration = 0}).HasError());
            REQUIRE(Detail::AudioHandleRegistry<AudioVoiceHandle>::Create(owner, {.maximumSlots = MaximumAudioHandleSlots + 1}).HasError());
        }

        TEST_CASE("Audio errors expose stable definitive descriptors", "[unit][audio][errors]") {
            REQUIRE((AudioErrors::IdentityInvalid.domain.Value() == "horo.audio"));
            REQUIRE((AudioErrors::IdentityInvalid.code.Value() == "audio.identity.invalid"));
            REQUIRE((AudioErrors::HandleStale.code.Value() == "audio.handle.stale"));
            REQUIRE_FALSE(AudioErrors::HandleStale.retryable);
            REQUIRE(AudioErrors::HandleCapacityExhausted.retryable);
            REQUIRE((AudioErrors::DeviceUnavailable.code.Value() == "audio.device.unavailable"));
            REQUIRE((AudioErrors::BackendFailed.code.Value() == "audio.backend.failed"));
        }
    }  // namespace
}  // namespace Horo::Audio
