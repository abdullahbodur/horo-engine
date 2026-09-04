#include "Horo/Audio/AudioCommands.h"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <limits>

namespace Horo::Audio {
    namespace {
        AudioRuntimeId Owner(const std::uint64_t value = 7) {
            return AudioRuntimeId::Create(value).Value();
        }

        AudioVoiceHandle Voice() {
            return {.owner = Owner(), .slot = 1, .generation = 2};
        }

        AudioCommandScope Scope() {
            return {.owner = Owner(), .epoch = 9, .scene = {.owner = Owner(), .slot = 3, .generation = 4}};
        }

        AudioMemoryHandle Storage() {
            return {.owner = Owner(), .pool = AudioMemoryPoolId::Create(8).Value(), .slot = 3, .generation = 4};
        }

        AudioCommand Parameter(const float value = 0.75F) {
            return {.scope = Scope(),
                    .payload =
                        AudioSetParameterCommand{.voice = Voice(), .parameter = AudioParameterId::Create(11).Value(), .value = value}};
        }

        TEST_CASE("Audio commands classify all typed alternatives without priority reordering", "[unit][audio][commands]") {
            const std::array<AudioCommandPayload, 8>
                payloads{AudioCreateVoiceCommand{.voice = Voice(), .clip = {.owner = Owner(), .slot = 5, .generation = 6}},
                         AudioStartVoiceCommand{.voice = Voice()},
                         AudioStopVoiceCommand{.voice = Voice()},
                         Parameter().payload,
                         AudioSwapGraphCommand{.storage = Storage()},
                         AudioReleaseResourceCommand{.storage = Storage()},
                         AudioSceneUnloadCommand{},
                         AudioResetCommand{}};
            const std::array classes{AudioCommandClass::Ordinary, AudioCommandClass::Ordinary, AudioCommandClass::Critical,
                                     AudioCommandClass::Ordinary, AudioCommandClass::Ordinary, AudioCommandClass::Critical,
                                     AudioCommandClass::Critical, AudioCommandClass::Critical};
            for (std::size_t index = 0; index < payloads.size(); ++index) {
                AudioCommand command{.scope = Scope(), .payload = payloads[index]};
                if (index == payloads.size() - 1) {
                    command.scope.scene = {};
                }
                AudioCommand output;
                REQUIRE(NormalizeAudioCommand(command, output) == AudioCommandStatus::Ok);
                REQUIRE(output.scope == command.scope);
                REQUIRE(output.payload.index() == command.payload.index());
                REQUIRE(ClassifyAudioCommand(output) == classes[index]);
            }
        }

        TEST_CASE("Audio command normalization rejects malformed scope without changing output", "[unit][audio][commands]") {
            auto command = Parameter();
            SECTION("owner") {
                command.scope.owner = {};
            }
            SECTION("epoch") {
                command.scope.epoch = 0;
            }
            SECTION("scene") {
                command.scope.scene = {};
            }
            SECTION("scene owner") {
                command.scope.scene.owner = Owner(8);
            }
            SECTION("scene slot") {
                command.scope.scene.slot = 0;
            }
            SECTION("scene generation") {
                command.scope.scene.generation = 0;
            }
            SECTION("reset cannot carry a scene") {
                command.payload = AudioResetCommand{};
            }
            auto output = Parameter(1.5F);
            REQUIRE(NormalizeAudioCommand(command, output) == AudioCommandStatus::InvalidScope);
            REQUIRE(output.scope == Scope());
            REQUIRE(std::get<AudioSetParameterCommand>(output.payload).value == 1.5F);
        }

        TEST_CASE("Audio payload handles reject unknown owner and incomplete identity", "[unit][audio][commands]") {
            const AudioVoiceHandle foreign{.owner = Owner(8), .slot = 1, .generation = 2};
            auto command = Parameter();
            SECTION("create voice") {
                command.payload = AudioCreateVoiceCommand{};
            }
            SECTION("create clip") {
                command.payload = AudioCreateVoiceCommand{.voice = Voice()};
            }
            SECTION("start") {
                command.payload = AudioStartVoiceCommand{.voice = foreign};
            }
            SECTION("stop") {
                command.payload = AudioStopVoiceCommand{};
            }
            SECTION("parameter voice") {
                std::get<AudioSetParameterCommand>(command.payload).voice = foreign;
            }
            SECTION("parameter id") {
                std::get<AudioSetParameterCommand>(command.payload).parameter = {};
            }
            SECTION("swap owner") {
                auto storage = Storage();
                storage.owner = Owner(8);
                command.payload = AudioSwapGraphCommand{.storage = storage};
            }
            SECTION("swap pool") {
                auto storage = Storage();
                storage.pool = {};
                command.payload = AudioSwapGraphCommand{.storage = storage};
            }
            SECTION("release slot") {
                auto storage = Storage();
                storage.slot = 0;
                command.payload = AudioReleaseResourceCommand{.storage = storage};
            }
            SECTION("release generation") {
                auto storage = Storage();
                storage.generation = 0;
                command.payload = AudioReleaseResourceCommand{.storage = storage};
            }
            auto output = Parameter(2.0F);
            REQUIRE(NormalizeAudioCommand(command, output) == AudioCommandStatus::InvalidPayload);
            REQUIRE(std::get<AudioSetParameterCommand>(output.payload).value == 2.0F);
        }

        TEST_CASE("Audio parameters canonicalize signed zero and denormals but preserve finite units", "[unit][audio][commands]") {
            for (const float value : {0.0F, -0.0F, std::numeric_limits<float>::denorm_min(), -std::numeric_limits<float>::denorm_min()}) {
                auto command = Parameter(value);
                REQUIRE(NormalizeAudioCommand(command, command) == AudioCommandStatus::Ok);
                const auto normalized = std::get<AudioSetParameterCommand>(command.payload).value;
                REQUIRE(normalized == 0.0F);
                REQUIRE_FALSE(std::signbit(normalized));
            }
            for (const float value : {-2.5F, 8.0F, std::numeric_limits<float>::max(), std::numeric_limits<float>::min()}) {
                auto command = Parameter(value);
                REQUIRE(NormalizeAudioCommand(command, command) == AudioCommandStatus::Ok);
                REQUIRE(std::get<AudioSetParameterCommand>(command.payload).value == value);
            }
            for (const float value : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(),
                                      -std::numeric_limits<float>::infinity()}) {
                auto command = Parameter(value);
                auto output = Parameter();
                REQUIRE(NormalizeAudioCommand(command, output) == AudioCommandStatus::InvalidPayload);
                REQUIRE(std::get<AudioSetParameterCommand>(output.payload).value == 0.75F);
            }
        }

        TEST_CASE("Audio coalescing requires identical scope voice and parameter", "[unit][audio][commands]") {
            auto earlier = Parameter();
            auto later = Parameter(3.0F);
            REQUIRE(CanCoalesceAudioCommands(earlier, later));
            SECTION("different runtime") {
                later.scope.owner = Owner(9);
            }
            SECTION("different epoch") {
                ++later.scope.epoch;
            }
            SECTION("different scene") {
                ++later.scope.scene.generation;
            }
            SECTION("different voice") {
                ++std::get<AudioSetParameterCommand>(later.payload).voice.generation;
            }
            SECTION("different parameter") {
                std::get<AudioSetParameterCommand>(later.payload).parameter = AudioParameterId::Create(12).Value();
            }
            SECTION("earlier barrier") {
                earlier.payload = AudioSceneUnloadCommand{};
            }
            SECTION("later barrier") {
                later.payload = AudioResetCommand{};
            }
            SECTION("later stop") {
                later.payload = AudioStopVoiceCommand{.voice = Voice()};
            }
            REQUIRE_FALSE(CanCoalesceAudioCommands(earlier, later));
        }
    }  // namespace
}  // namespace Horo::Audio
