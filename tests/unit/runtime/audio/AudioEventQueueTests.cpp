#include "AudioCommandTestProbe.h"
#include "Horo/Audio/AudioEventQueue.h"

#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstdint>
#include <limits>
#include <thread>
#include <type_traits>
#include <utility>

namespace Horo::Audio {
    namespace {
        AudioRuntimeId Owner(const std::uint64_t value = 17) {
            return AudioRuntimeId::Create(value).Value();
        }

        AudioCommandScope Scope(const std::uint64_t epoch = 6) {
            return {.owner = Owner(), .epoch = epoch, .scene = {.owner = Owner(), .slot = 1, .generation = 2}};
        }

        AudioDeviceEpoch CallbackEpoch() {
            return {.device = {.owner = Owner(), .slot = 3, .generation = 4}, .formatRevision = 5, .callbackEpoch = 7};
        }

        AudioEventQueueDescriptor Descriptor(const std::uint32_t slots = 4) {
            return {.owner = Owner(),
                    .storageIdentity = AudioMemoryPoolId::Create(23).Value(),
                    .commandEpoch = 6,
                    .callbackEpoch = CallbackEpoch(),
                    .clockDomain = 9,
                    .slots = slots,
                    .criticalSlots = slots / 2,
                    .budgetBytes = 64 * 1024};
        }

        AudioEventQueue Queue(const std::uint32_t slots = 4) {
            auto result = AudioEventQueue::Create(Descriptor(slots));
            REQUIRE(result.HasValue());
            return std::move(result).Value();
        }

        AudioCompletionToken Token(const std::uint64_t sequence) {
            auto result = AudioCompletionToken::Create(Scope(), sequence);
            REQUIRE(result.HasValue());
            return std::move(result).Value();
        }

        AudioTerminalEvent Voice(const std::uint64_t sequence, const AudioVoiceTerminalReason reason = AudioVoiceTerminalReason::Finished) {
            return AudioVoiceTerminalEvent{.scope = Scope(),
                                           .acceptedSequence = sequence,
                                           .voice = {.owner = Owner(), .slot = 5, .generation = 6},
                                           .reason = reason};
        }

        AudioTerminalEvent Release(const std::uint64_t sequence, const std::uint64_t completedEpoch = 8) {
            return AudioResourceReleaseEvent{.scope = Scope(),
                                             .acceptedSequence = sequence,
                                             .storage = {.owner = Owner(),
                                                         .pool = AudioMemoryPoolId::Create(77).Value(),
                                                         .slot = 2,
                                                         .generation = 3},
                                             .completedEpoch = completedEpoch};
        }

        AudioCallbackEvent Callback(AudioCallbackFact fact = AudioCallbackUnderrun{64}) {
            return {.epoch = CallbackEpoch(), .sampleFrame = 0, .timestamp = {.clockDomain = 9, .nanoseconds = 0}, .fact = fact};
        }

        TEST_CASE("Audio event queue preparation validates exact bounded producer identity", "[unit][audio][events]") {
            auto descriptor = Descriptor();
            SECTION("owner") {
                descriptor.owner = {};
            }
            SECTION("pool") {
                descriptor.storageIdentity = {};
            }
            SECTION("command epoch") {
                descriptor.commandEpoch = 0;
            }
            SECTION("device epoch") {
                descriptor.callbackEpoch.callbackEpoch = 0;
            }
            SECTION("foreign device owner") {
                descriptor.callbackEpoch.device.owner = Owner(18);
            }
            SECTION("clock") {
                descriptor.clockDomain = 0;
            }
            SECTION("single slot") {
                descriptor.slots = 1;
            }
            SECTION("non power of two") {
                descriptor.slots = 3;
            }
            SECTION("over capacity") {
                descriptor.slots = MaximumAudioEventSlots * 2;
            }
            SECTION("no critical reserve") {
                descriptor.criticalSlots = 0;
            }
            SECTION("all critical") {
                descriptor.criticalSlots = descriptor.slots;
            }
            SECTION("budget") {
                descriptor.budgetBytes = 1;
            }
            REQUIRE(AudioEventQueue::Create(descriptor).HasError());
        }

        TEST_CASE("Audio event queue preparation rolls back every backing allocation failure", "[unit][audio][events]") {
            for (std::size_t failure = 1; failure <= 4; ++failure) {
                Test::failCountdown = failure;
                const auto result = AudioEventQueue::Create(Descriptor());
                Test::failCountdown = 0;
                REQUIRE(result.HasError());
                REQUIRE(result.ErrorValue().code.Value() == AudioErrors::MemoryAllocationFailed.code.Value());
            }
        }

        TEST_CASE("Audio completion tokens publish at most one exact terminal result", "[unit][audio][events][completion]") {
            auto queue = Queue();
            auto token = Token(1);
            const auto event = Voice(1, AudioVoiceTerminalReason::Stopped);
            REQUIRE(queue.TryPublishTerminal(token, event) == AudioEventPublishStatus::Published);
            REQUIRE(token.State() == AudioCompletionTokenState::Published);
            REQUIRE(queue.TryPublishTerminal(token, event) == AudioEventPublishStatus::DuplicateTerminal);
            REQUIRE(queue.Stats().terminalEvents == 1);
            REQUIRE(queue.Stats().duplicateTerminals == 1);

            AudioControlEventRecord output;
            REQUIRE(queue.TryConsume(output));
            const auto &voice = std::get<AudioVoiceTerminalEvent>(output.event);
            REQUIRE(voice.acceptedSequence == 1);
            REQUIRE(voice.reason == AudioVoiceTerminalReason::Stopped);
        }

        TEST_CASE("Audio completion retry retains its token and preserves FIFO critical outcomes", "[unit][audio][events][saturation]") {
            auto queue = Queue();
            REQUIRE(queue.TryPublishDevice(Callback()) == AudioEventPublishStatus::Published);
            REQUIRE(queue.TryPublishDevice(Callback()) == AudioEventPublishStatus::Published);
            REQUIRE(queue.TryPublishDevice(Callback()) == AudioEventPublishStatus::TelemetryDropped);
            REQUIRE(queue.TryPublishDevice(Callback(AudioCallbackReady{})) == AudioEventPublishStatus::Published);
            REQUIRE(queue.TryPublishDevice(Callback(AudioCallbackQuiesced{})) == AudioEventPublishStatus::Published);
            REQUIRE(queue.TryPublishDevice(Callback(AudioCallbackFault{AudioCallbackFaultCode::BackendFailure})) ==
                    AudioEventPublishStatus::CriticalRetry);

            auto terminal = Token(3);
            REQUIRE(queue.TryPublishTerminal(terminal, Voice(3)) == AudioEventPublishStatus::CriticalRetry);
            REQUIRE(terminal.State() == AudioCompletionTokenState::Pending);
            AudioControlEventRecord output;
            REQUIRE(queue.TryConsume(output));
            REQUIRE(queue.TryPublishTerminal(terminal, Voice(3)) == AudioEventPublishStatus::Published);
            REQUIRE(queue.Stats().droppedTelemetry == 1);
            REQUIRE(queue.Stats().criticalRetries == 2);
            REQUIRE(queue.Stats().terminalEvents == 1);
            REQUIRE(queue.Stats().deviceEvents == 4);
        }

        TEST_CASE("Audio event queue rejects stale malformed and mismatched outcomes without consuming tokens",
                  "[unit][audio][events][validation]") {
            REQUIRE(AudioCompletionToken::Create({}, 1).HasError());
            REQUIRE(AudioCompletionToken::Create(Scope(), 0).HasError());
            auto queue = Queue();
            auto token = Token(4);
            REQUIRE(queue.TryPublishTerminal(token, Voice(5)) == AudioEventPublishStatus::InvalidEvent);
            REQUIRE(token.State() == AudioCompletionTokenState::Pending);

            auto invalidVoice = Voice(4);
            std::get<AudioVoiceTerminalEvent>(invalidVoice).voice.owner = Owner(18);
            REQUIRE(queue.TryPublishTerminal(token, invalidVoice) == AudioEventPublishStatus::InvalidEvent);
            auto invalidReason = Voice(4, static_cast<AudioVoiceTerminalReason>(255));
            REQUIRE(queue.TryPublishTerminal(token, invalidReason) == AudioEventPublishStatus::InvalidEvent);

            auto release = Token(4);
            REQUIRE(queue.TryPublishTerminal(release, Release(4, 0)) == AudioEventPublishStatus::InvalidEvent);
            auto staleDevice = Callback(AudioCallbackReady{});
            ++staleDevice.epoch.callbackEpoch;
            REQUIRE(queue.TryPublishDevice(staleDevice) == AudioEventPublishStatus::InvalidEvent);
            staleDevice = Callback(AudioCallbackReady{});
            staleDevice.timestamp.clockDomain = 10;
            REQUIRE(queue.TryPublishDevice(staleDevice) == AudioEventPublishStatus::InvalidEvent);
            REQUIRE(queue.Stats().terminalEvents == 0);
            REQUIRE(queue.Stats().deviceEvents == 0);
        }

        TEST_CASE("Audio resource release outcomes retain exact completion evidence without granting reclamation",
                  "[unit][audio][events][release]") {
            auto queue = Queue();
            auto token = Token(7);
            REQUIRE(queue.TryPublishTerminal(token, Release(7, 11)) == AudioEventPublishStatus::Published);
            AudioControlEventRecord output;
            REQUIRE(queue.TryConsume(output));
            const auto &release = std::get<AudioResourceReleaseEvent>(output.event);
            REQUIRE(release.acceptedSequence == 7);
            REQUIRE(release.completedEpoch == 11);
            REQUIRE(release.storage.pool == AudioMemoryPoolId::Create(77).Value());
        }

        TEST_CASE("Audio event queue move close and drain leave inert owners", "[unit][audio][events][shutdown]") {
            auto source = Queue();
            auto token = Token(1);
            REQUIRE(source.TryPublishTerminal(token, Voice(1, AudioVoiceTerminalReason::Cancelled)) == AudioEventPublishStatus::Published);
            auto moved = std::move(source);
            REQUIRE(source.IsDrained());
            REQUIRE(source.Stats().terminalEvents == 0);
            AudioControlEventRecord output{.event = Callback(AudioCallbackReady{})};
            REQUIRE_FALSE(source.TryConsume(output));
            REQUIRE(std::holds_alternative<AudioCallbackEvent>(output.event));
            source.Close();

            moved.Close();
            REQUIRE_FALSE(moved.IsDrained());
            auto late = Token(2);
            REQUIRE(moved.TryPublishTerminal(late, Voice(2)) == AudioEventPublishStatus::Closed);
            REQUIRE(late.State() == AudioCompletionTokenState::Pending);
            REQUIRE(moved.TryConsume(output));
            REQUIRE(moved.IsDrained());
            REQUIRE_FALSE(moved.TryConsume(output));
            REQUIRE(std::holds_alternative<AudioVoiceTerminalEvent>(output.event));
        }

        TEST_CASE("Audio event queue callback and control hot methods allocate and free no heap memory",
                  "[unit][audio][events][realtime]") {
            auto queue = Queue();
            auto token = Token(1);
            const auto terminal = Voice(1);
            const auto device = Callback(AudioCallbackReady{});
            AudioControlEventRecord output;
            const auto allocated = Test::allocationCount;
            const auto freed = Test::deallocationCount;
            const auto first = queue.TryPublishTerminal(token, terminal);
            const auto second = queue.TryPublishDevice(device);
            const auto consumed = queue.TryConsume(output);
            const auto stats = queue.Stats();
            queue.Close();
            const auto last = queue.TryConsume(output);
            const auto drained = queue.IsDrained();
            REQUIRE(Test::allocationCount - allocated == 0);
            REQUIRE(Test::deallocationCount - freed == 0);
            REQUIRE(first == AudioEventPublishStatus::Published);
            REQUIRE(second == AudioEventPublishStatus::Published);
            REQUIRE(consumed);
            REQUIRE(last);
            REQUIRE(stats.terminalEvents == 1);
            REQUIRE(drained);
        }

        TEST_CASE("Audio event queue transfers repeated terminal slots across two threads", "[unit][audio][events][realtime][threaded]") {
            auto queue = Queue(64);
            constexpr std::uint64_t Count = 20'000;
            std::atomic<bool> incorrect{};
            std::atomic<bool> stop{};
            std::uint64_t received{};
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);

            std::thread consumer([&] {
                AudioControlEventRecord output;
                while (!stop.load(std::memory_order_relaxed) && (!queue.IsDrained() || received != Count)) {
                    if (queue.TryConsume(output)) {
                        ++received;
                        const auto *voice = std::get_if<AudioVoiceTerminalEvent>(&output.event);
                        if (!voice || voice->acceptedSequence != received)
                            incorrect.store(true, std::memory_order_relaxed);
                    } else if (std::chrono::steady_clock::now() >= deadline) {
                        incorrect.store(true, std::memory_order_relaxed);
                        stop.store(true, std::memory_order_relaxed);
                    } else {
                        std::this_thread::yield();
                    }
                }
            });

            for (std::uint64_t sequence = 1; sequence <= Count && !stop.load(std::memory_order_relaxed); ++sequence) {
                auto token = Token(sequence);
                while (queue.TryPublishTerminal(token, Voice(sequence)) == AudioEventPublishStatus::CriticalRetry) {
                    if (std::chrono::steady_clock::now() >= deadline) {
                        incorrect.store(true, std::memory_order_relaxed);
                        stop.store(true, std::memory_order_relaxed);
                        break;
                    }
                    std::this_thread::yield();
                }
            }
            queue.Close();
            consumer.join();
            REQUIRE_FALSE(incorrect.load());
            REQUIRE(received == Count);
            REQUIRE(queue.IsDrained());
            REQUIRE(queue.Stats().terminalEvents == Count);
        }

        static_assert(!std::is_copy_constructible_v<AudioCompletionToken>);
        static_assert(!std::is_copy_constructible_v<AudioEventQueue>);
        static_assert(std::is_trivially_copyable_v<AudioControlEventRecord>);
    }  // namespace
}  // namespace Horo::Audio
