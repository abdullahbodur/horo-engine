#include "AudioCommandTestProbe.h"
#include "Horo/Audio/AudioCommandBuffer.h"

#include <algorithm>
#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <new>
#include <thread>
#ifdef _WIN32
#include <malloc.h>
#endif

namespace Horo::Audio::Test {
    thread_local std::size_t allocationCount{};
    thread_local std::size_t deallocationCount{};
    thread_local std::size_t failCountdown{};
}  // namespace Horo::Audio::Test

namespace {
    using Horo::Audio::Test::allocationCount;
    using Horo::Audio::Test::deallocationCount;
    using Horo::Audio::Test::failCountdown;

    struct JoinOnExit {
        std::thread &thread;

        ~JoinOnExit() {
            if (thread.joinable()) {
                thread.join();
            }
        }
    };

    void CountAllocation() {
        ++allocationCount;
        if (failCountdown != 0 && --failCountdown == 0) {
            throw std::bad_alloc{};
        }
    }

    void *Allocate(const std::size_t size) {
        CountAllocation();
        if (void *memory = std::malloc(std::max(size, std::size_t{1}))) {
            return memory;
        }
        throw std::bad_alloc{};
    }

    void *AllocateAligned(const std::size_t size, const std::size_t alignment) {
        CountAllocation();
#ifdef _WIN32
        if (void *memory = _aligned_malloc(std::max(size, std::size_t{1}), alignment)) {
            return memory;
        }
#else
        void *memory = nullptr;
        if (posix_memalign(&memory, alignment, std::max(size, std::size_t{1})) == 0) {
            return memory;
        }
#endif
        throw std::bad_alloc{};
    }

    void Free(void *memory) noexcept {
        ++deallocationCount;
        std::free(memory);
    }

    void FreeAligned(void *memory) noexcept {
        ++deallocationCount;
#ifdef _WIN32
        _aligned_free(memory);
#else
        std::free(memory);
#endif
    }
}  // namespace

void *operator new(const std::size_t size) {
    return Allocate(size);
}

void *operator new[](const std::size_t size) {
    return Allocate(size);
}

void *operator new(const std::size_t size, const std::align_val_t alignment) {
    return AllocateAligned(size, static_cast<std::size_t>(alignment));
}

void *operator new[](const std::size_t size, const std::align_val_t alignment) {
    return AllocateAligned(size, static_cast<std::size_t>(alignment));
}

void operator delete(void *memory) noexcept {
    Free(memory);
}

void operator delete[](void *memory) noexcept {
    Free(memory);
}

void operator delete(void *memory, std::size_t) noexcept {
    Free(memory);
}

void operator delete[](void *memory, std::size_t) noexcept {
    Free(memory);
}

void operator delete(void *memory, std::align_val_t) noexcept {
    FreeAligned(memory);
}

void operator delete[](void *memory, std::align_val_t) noexcept {
    FreeAligned(memory);
}

void operator delete(void *memory, std::size_t, std::align_val_t) noexcept {
    FreeAligned(memory);
}

void operator delete[](void *memory, std::size_t, std::align_val_t) noexcept {
    FreeAligned(memory);
}

namespace Horo::Audio {
    namespace {
        AudioRuntimeId Owner() {
            return AudioRuntimeId::Create(17).Value();
        }

        AudioCommandBufferDescriptor Descriptor() {
            return {.owner = Owner(),
                    .storageIdentity = AudioMemoryPoolId::Create(23).Value(),
                    .epoch = 5,
                    .slots = 4,
                    .criticalSlots = 2,
                    .budgetBytes = 4096};
        }

        AudioCommandBuffer Buffer() {
            auto result = AudioCommandBuffer::Create(Descriptor());
            REQUIRE(result.HasValue());
            return std::move(result).Value();
        }

        AudioCommandRecord Record(const std::uint64_t sequence) {
            return {.sequence = sequence,
                    .command = {.scope = {.owner = Owner(), .epoch = 5, .scene = {.owner = Owner(), .slot = 1, .generation = 1}},
                                .payload = AudioStartVoiceCommand{.voice = {.owner = Owner(), .slot = 2, .generation = 3}}}};
        }

        AudioCommandRecord Stop(const std::uint64_t sequence) {
            auto record = Record(sequence);
            record.command.payload = AudioStopVoiceCommand{.voice = {.owner = Owner(), .slot = 2, .generation = 3}};
            return record;
        }

        TEST_CASE("Audio SPSC preparation rejects unsupported dimensions and memory admission", "[unit][audio][commands]") {
            auto descriptor = Descriptor();
            SECTION("epoch") {
                descriptor.epoch = 0;
            }
            SECTION("empty") {
                descriptor.slots = 0;
            }
            SECTION("single slot") {
                descriptor.slots = 1;
            }
            SECTION("non power of two") {
                descriptor.slots = 3;
            }
            SECTION("over capacity") {
                descriptor.slots = MaximumAudioCommandSlots * 2;
            }
            SECTION("no critical reserve") {
                descriptor.criticalSlots = 0;
            }
            SECTION("all critical") {
                descriptor.criticalSlots = descriptor.slots;
            }
            SECTION("owner") {
                descriptor.owner = {};
            }
            SECTION("pool") {
                descriptor.storageIdentity = {};
            }
            SECTION("budget") {
                descriptor.budgetBytes = 1;
            }
            REQUIRE(AudioCommandBuffer::Create(descriptor).HasError());
        }

        TEST_CASE("Audio SPSC preparation rolls back every backing allocation failure", "[unit][audio][commands]") {
            const auto descriptor = Descriptor();
            for (std::size_t failure = 1; failure <= 4; ++failure) {
                failCountdown = failure;
                const auto result = AudioCommandBuffer::Create(descriptor);
                failCountdown = 0;
                REQUIRE(result.HasError());
                REQUIRE(result.ErrorValue().code.Value() == AudioErrors::MemoryAllocationFailed.code.Value());
            }
        }

        TEST_CASE("Audio SPSC reserves critical capacity without losing FIFO barriers", "[unit][audio][commands]") {
            auto buffer = Buffer();
            REQUIRE(buffer.TryPublish(Record(1)) == AudioCommandPublishStatus::Published);
            REQUIRE(buffer.TryPublish(Record(2)) == AudioCommandPublishStatus::Published);
            REQUIRE(buffer.TryPublish(Record(3)) == AudioCommandPublishStatus::OrdinaryFull);
            REQUIRE(buffer.TryPublish(Stop(3)) == AudioCommandPublishStatus::Published);
            auto release = Record(4);
            release.command.payload = AudioReleaseResourceCommand{
                .storage = {.owner = Owner(), .pool = AudioMemoryPoolId::Create(99).Value(), .slot = 1, .generation = 1}};
            REQUIRE(buffer.TryPublish(release) == AudioCommandPublishStatus::Published);
            auto unload = Record(5);
            unload.command.payload = AudioSceneUnloadCommand{};
            REQUIRE(buffer.TryPublish(unload) == AudioCommandPublishStatus::CriticalRetry);
            AudioCommandRecord output;
            REQUIRE(buffer.TryConsume(output));
            REQUIRE(output.sequence == 1);
            REQUIRE(buffer.TryPublish(unload) == AudioCommandPublishStatus::Published);
            auto reset = Record(6);
            reset.command.scope.scene = {};
            reset.command.payload = AudioResetCommand{};
            REQUIRE(buffer.TryPublish(reset) == AudioCommandPublishStatus::CriticalRetry);
            REQUIRE(buffer.TryConsume(output));
            REQUIRE(output.sequence == 2);
            REQUIRE(buffer.TryPublish(reset) == AudioCommandPublishStatus::Published);
            buffer.Close();
            REQUIRE_FALSE(buffer.IsDrained());
            for (std::uint64_t sequence = 3; sequence <= 6; ++sequence) {
                REQUIRE(buffer.TryConsume(output));
                REQUIRE(output.sequence == sequence);
            }
            REQUIRE(std::holds_alternative<AudioResetCommand>(output.command.payload));
            REQUIRE(buffer.IsDrained());
            REQUIRE_FALSE(buffer.TryConsume(output));
            REQUIRE(output.sequence == 6);
            REQUIRE(buffer.TryPublish(Stop(7)) == AudioCommandPublishStatus::Closed);
            buffer.Close();
            REQUIRE(buffer.IsDrained());
        }

        TEST_CASE("Audio SPSC validates commands and never consumes a rejected sequence", "[unit][audio][commands]") {
            auto buffer = Buffer();
            REQUIRE_FALSE(buffer.IsDrained());
            REQUIRE(buffer.TryPublish(Record(0)) == AudioCommandPublishStatus::InvalidSequence);
            auto record = Record(1);
            SECTION("owner") {
                record.command.scope.owner = AudioRuntimeId::Create(18).Value();
            }
            SECTION("epoch") {
                ++record.command.scope.epoch;
            }
            SECTION("payload") {
                record.command.payload = AudioStartVoiceCommand{};
            }
            REQUIRE(buffer.TryPublish(record) == AudioCommandPublishStatus::InvalidCommand);
            REQUIRE(buffer.TryPublish(Record(1)) == AudioCommandPublishStatus::Published);
            REQUIRE(buffer.TryPublish(Record(1)) == AudioCommandPublishStatus::InvalidSequence);
            REQUIRE(buffer.TryPublish(Record(std::numeric_limits<std::uint64_t>::max())) == AudioCommandPublishStatus::Published);
            REQUIRE(buffer.TryPublish(Record(2)) == AudioCommandPublishStatus::InvalidSequence);
        }

        TEST_CASE("Audio SPSC publishes owned canonicalized values", "[unit][audio][commands]") {
            auto buffer = Buffer();
            auto record = Record(1);
            record.command.payload = AudioSetParameterCommand{.voice = {.owner = Owner(), .slot = 2, .generation = 3},
                                                              .parameter = AudioParameterId::Create(9).Value(),
                                                              .value = -0.0F};
            REQUIRE(buffer.TryPublish(record) == AudioCommandPublishStatus::Published);
            std::get<AudioSetParameterCommand>(record.command.payload).value = 42.0F;
            record.command.scope.epoch = 6;
            AudioCommandRecord output;
            REQUIRE(buffer.TryConsume(output));
            REQUIRE(output.command.scope.epoch == 5);
            const auto value = std::get<AudioSetParameterCommand>(output.command.payload).value;
            REQUIRE(value == 0.0F);
            REQUIRE_FALSE(std::signbit(value));
        }

        TEST_CASE("Audio SPSC moves require quiescence and leave inert sources", "[unit][audio][commands]") {
            auto source = Buffer();
            REQUIRE(source.TryPublish(Record(1)) == AudioCommandPublishStatus::Published);
            auto moved = std::move(source);
            AudioCommandRecord output = Record(99);
            REQUIRE(source.IsDrained());
            REQUIRE_FALSE(source.TryConsume(output));
            REQUIRE(output.sequence == 99);
            REQUIRE(source.TryPublish(Record(2)) == AudioCommandPublishStatus::Inactive);
            source.Close();
            auto descriptor = Descriptor();
            descriptor.storageIdentity = AudioMemoryPoolId::Create(24).Value();
            auto destination = std::move(AudioCommandBuffer::Create(descriptor)).Value();
            destination = std::move(moved);
            REQUIRE(destination.TryConsume(output));
            REQUIRE(output.sequence == 1);
            REQUIRE(moved.IsDrained());
        }

        TEST_CASE("Audio SPSC hot methods allocate and free no general heap memory", "[unit][audio][commands]") {
            auto buffer = Buffer();
            const auto first = Record(1);
            const auto second = Stop(2);
            AudioCommandRecord output;
            const auto allocated = allocationCount;
            const auto freed = deallocationCount;
            const auto published = buffer.TryPublish(first);
            const auto critical = buffer.TryPublish(second);
            const auto consumed = buffer.TryConsume(output);
            buffer.Close();
            const auto beforeDrain = buffer.IsDrained();
            const auto last = buffer.TryConsume(output);
            const auto drained = buffer.IsDrained();
            const auto allocationDelta = allocationCount - allocated;
            const auto freeDelta = deallocationCount - freed;
            REQUIRE(allocationDelta == 0);
            REQUIRE(freeDelta == 0);
            REQUIRE(published == AudioCommandPublishStatus::Published);
            REQUIRE(critical == AudioCommandPublishStatus::Published);
            REQUIRE(consumed);
            REQUIRE(last);
            REQUIRE_FALSE(beforeDrain);
            REQUIRE(drained);
        }

        TEST_CASE("Audio SPSC transfers ordered records across threads through repeated ring reuse", "[unit][audio][commands]") {
            auto buffer = Buffer();
            constexpr std::uint64_t Count = 50'000;
            std::atomic<bool> incorrect{};
            std::uint64_t received{};
            std::size_t callbackAllocations{};
            std::size_t callbackFrees{};
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
            std::atomic<bool> stop{};
            std::thread consumer([&] {
                const auto allocated = allocationCount;
                const auto freed = deallocationCount;
                AudioCommandRecord output;
                while (!stop.load(std::memory_order_relaxed) && !buffer.IsDrained() && std::chrono::steady_clock::now() < deadline) {
                    if (buffer.TryConsume(output)) {
                        ++received;
                        if (output.sequence != received) {
                            incorrect.store(true);
                        }
                    } else {
                        std::this_thread::yield();
                    }
                }
                callbackAllocations = allocationCount - allocated;
                callbackFrees = deallocationCount - freed;
            });
            const JoinOnExit joinOnExit{consumer};
            std::uint64_t sent{};
            while (sent < Count && std::chrono::steady_clock::now() < deadline) {
                if (buffer.TryPublish(Record(sent + 1)) == AudioCommandPublishStatus::Published) {
                    ++sent;
                } else {
                    std::this_thread::yield();
                }
            }
            buffer.Close();
            if (sent != Count) {
                stop.store(true, std::memory_order_relaxed);
            }
            consumer.join();
            REQUIRE(sent == Count);
            REQUIRE(received == Count);
            REQUIRE_FALSE(incorrect.load());
            REQUIRE(callbackAllocations == 0);
            REQUIRE(callbackFrees == 0);
            REQUIRE(buffer.IsDrained());
        }
    }  // namespace
}  // namespace Horo::Audio
