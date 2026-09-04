#include "Horo/Audio/AudioMemory.h"

#include <algorithm>
#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <limits>
#include <new>
#ifdef _WIN32
#include <malloc.h>
#endif

namespace {
    std::atomic<std::size_t> allocations{};
    thread_local bool failAlignedAllocation{};

    void *Allocate(const std::size_t size) {
        allocations.fetch_add(1, std::memory_order_relaxed);
        if (void *memory = std::malloc(std::max(size, std::size_t{1}))) {
            return memory;
        }
        throw std::bad_alloc{};
    }

    void *AllocateAligned(const std::size_t size, const std::size_t alignment) {
        allocations.fetch_add(1, std::memory_order_relaxed);
        if (failAlignedAllocation) {
            failAlignedAllocation = false;
            throw std::bad_alloc{};
        }
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

    void FreeAligned(void *memory) noexcept {
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
    std::free(memory);
}

void operator delete[](void *memory) noexcept {
    std::free(memory);
}

void operator delete(void *memory, std::size_t) noexcept {
    std::free(memory);
}

void operator delete[](void *memory, std::size_t) noexcept {
    std::free(memory);
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
        AudioRuntimeId Owner(const std::uint64_t value = 7) {
            return AudioRuntimeId::Create(value).Value();
        }

        AudioScratchArena Arena(const std::size_t capacity = 256) {
            auto created = AudioScratchArena::Create(Owner(), capacity);
            REQUIRE(created.HasValue());
            return std::move(created).Value();
        }

        AudioMemoryPoolDescriptor Descriptor() {
            return {.owner = Owner(),
                    .identity = AudioMemoryPoolId::Create(13).Value(),
                    .purpose = AudioMemoryPurpose::VoiceState,
                    .slots = 2,
                    .blockBytes = 65,
                    .budgetBytes = 1024};
        }

        AudioMemoryPool Pool(const AudioMemoryPoolDescriptor descriptor = Descriptor()) {
            auto created = AudioMemoryPool::Create(descriptor);
            REQUIRE(created.HasValue());
            return std::move(created).Value();
        }

        TEST_CASE("Pool preparation validates identities sizes budgets and failure rollback", "[unit][audio][memory]") {
            auto descriptor = Descriptor();
            SECTION("owner") {
                descriptor.owner = {};
            }
            SECTION("identity") {
                descriptor.identity = {};
            }
            SECTION("purpose") {
                descriptor.purpose = static_cast<AudioMemoryPurpose>(255);
            }
            SECTION("zero slots") {
                descriptor.slots = 0;
            }
            SECTION("too many slots") {
                descriptor.slots = MaximumAudioHandleSlots + 1;
            }
            SECTION("empty payload") {
                descriptor.blockBytes = 0;
            }
            SECTION("overflow payload") {
                descriptor.blockBytes = std::numeric_limits<std::size_t>::max();
            }
            SECTION("zero budget") {
                descriptor.budgetBytes = 0;
            }
            SECTION("unbounded budget") {
                descriptor.budgetBytes = MaximumAudioMemoryBytes + 1;
            }
            const auto result = AudioMemoryPool::Create(descriptor);
            REQUIRE(result.HasError());
            REQUIRE(result.ErrorValue().code.Value() == AudioErrors::MemoryInvalid.code.Value());
        }

        TEST_CASE("Pool budgets charge metadata and aligned strides for each purpose", "[unit][audio][memory]") {
            for (const auto purpose : {AudioMemoryPurpose::VoiceState, AudioMemoryPurpose::GraphState, AudioMemoryPurpose::CommandStorage,
                                       AudioMemoryPurpose::EventStorage}) {
                auto descriptor = Descriptor();
                descriptor.purpose = purpose;
                auto pool = Pool(descriptor);
                const auto reservation = pool.Stats().reservedBytes;
                REQUIRE(reservation > 256);
                descriptor.budgetBytes = reservation;
                REQUIRE(AudioMemoryPool::Create(descriptor).HasValue());
                --descriptor.budgetBytes;
                const auto tooSmall = AudioMemoryPool::Create(descriptor);
                REQUIRE(tooSmall.HasError());
                REQUIRE(tooSmall.ErrorValue().code.Value() == AudioErrors::MemoryBudgetExceeded.code.Value());
            }
            failAlignedAllocation = true;
            const auto failed = AudioMemoryPool::Create(Descriptor());
            REQUIRE(failed.HasError());
            REQUIRE(failed.ErrorValue().code.Value() == AudioErrors::MemoryAllocationFailed.code.Value());
            REQUIRE_FALSE(failAlignedAllocation);
        }

        TEST_CASE("Pool reserves aligned zeroed blocks and counts bounded exhaustion", "[unit][audio][memory]") {
            auto pool = Pool();
            const auto first = pool.Acquire();
            const auto second = pool.Acquire();
            REQUIRE(first.status == AudioMemoryStatus::Ok);
            REQUIRE(second.status == AudioMemoryStatus::Ok);
            REQUIRE(first.bytes.size() == 65);
            REQUIRE(reinterpret_cast<std::uintptr_t>(first.bytes.data()) % AudioMemoryAlignment == 0);
            REQUIRE(second.bytes.data() - first.bytes.data() == 128);
            REQUIRE(std::ranges::all_of(first.bytes, [](const auto byte) {
                return byte == std::byte{0};
            }));
            REQUIRE(pool.Resolve(first.handle).data() == first.bytes.data());
            REQUIRE(pool.Stats().usedBytes == 256);
            REQUIRE(pool.Stats().peakBytes == 256);
            REQUIRE(pool.Acquire().status == AudioMemoryStatus::Exhausted);
            REQUIRE(pool.Acquire().bytes.empty());
            REQUIRE(pool.Stats().failedAllocations == 2);
            REQUIRE(pool.Stats().usedBytes == 256);
        }

        TEST_CASE("Pool rejects foreign malformed stale and retired handles", "[unit][audio][memory]") {
            auto pool = Pool();
            auto alternate = Descriptor();
            alternate.identity = AudioMemoryPoolId::Create(14).Value();
            auto other = Pool(alternate);
            const auto allocation = pool.Acquire();
            REQUIRE(other.Resolve(allocation.handle).empty());
            REQUIRE(other.Retire(allocation.handle, 1) == AudioMemoryStatus::InvalidHandle);
            auto handle = allocation.handle;
            SECTION("owner") {
                handle.owner = Owner(9);
            }
            SECTION("pool") {
                handle.pool = alternate.identity;
            }
            SECTION("zero slot") {
                handle.slot = 0;
            }
            SECTION("outside slots") {
                handle.slot = 3;
            }
            SECTION("generation") {
                ++handle.generation;
            }
            REQUIRE(pool.Resolve(handle).empty());
            REQUIRE(pool.Retire(handle, 1) == AudioMemoryStatus::InvalidHandle);
            REQUIRE(pool.Retire(allocation.handle, 0) == AudioMemoryStatus::InvalidEpoch);
            REQUIRE(pool.Retire(allocation.handle, 3) == AudioMemoryStatus::Ok);
            REQUIRE(pool.Resolve(allocation.handle).empty());
            REQUIRE(pool.Retire(allocation.handle, 3) == AudioMemoryStatus::InvalidHandle);
        }

        TEST_CASE("Pool deferred reuse waits for completion and obeys round robin scan budget", "[unit][audio][memory]") {
            auto pool = Pool();
            auto first = pool.Acquire();
            auto second = pool.Acquire();
            std::ranges::fill(first.bytes, std::byte{42});
            REQUIRE(pool.Retire(first.handle, 5) == AudioMemoryStatus::Ok);
            REQUIRE(pool.Retire(second.handle, 8) == AudioMemoryStatus::Ok);
            REQUIRE(pool.Reclaim(Owner(9), 5, 2).status == AudioMemoryStatus::InvalidEpoch);
            REQUIRE(pool.Reclaim(Owner(), 0, 2).status == AudioMemoryStatus::InvalidEpoch);
            REQUIRE(pool.Reclaim(Owner(), 5, 0).status == AudioMemoryStatus::InvalidRequest);
            REQUIRE(pool.Reclaim(Owner(), 4, 2).reclaimed == 0);
            REQUIRE(pool.Stats().usedBytes == 256);
            REQUIRE(pool.Acquire().status == AudioMemoryStatus::Exhausted);
            REQUIRE(first.bytes.front() == std::byte{42});
            const auto partial = pool.Reclaim(Owner(), 5, 1);
            REQUIRE(partial.visited == 1);
            REQUIRE(partial.reclaimed == 1);
            REQUIRE(pool.Reclaim(Owner(), 4, 2).status == AudioMemoryStatus::InvalidEpoch);
            REQUIRE(pool.Stats().usedBytes == 128);
            auto reused = pool.Acquire();
            REQUIRE(reused.bytes.data() == first.bytes.data());
            REQUIRE(reused.handle.generation == first.handle.generation + 1);
            REQUIRE(std::ranges::all_of(reused.bytes, [](const auto byte) {
                return byte == std::byte{0};
            }));
            REQUIRE(pool.Resolve(first.handle).empty());
            REQUIRE(pool.Retire(reused.handle, 5) == AudioMemoryStatus::InvalidEpoch);
            REQUIRE(pool.Reclaim(Owner(), 5, 1).reclaimed == 0);
            const auto all = pool.Reclaim(Owner(), 8, std::numeric_limits<std::uint32_t>::max());
            REQUIRE(all.visited == 2);
            REQUIRE(all.reclaimed == 1);
            REQUIRE(pool.Stats().peakBytes == 256);
            REQUIRE(pool.Retire(reused.handle, 9) == AudioMemoryStatus::Ok);
            REQUIRE(pool.Reclaim(Owner(), 9, 2).reclaimed == 1);
            REQUIRE(pool.Stats().usedBytes == 0);
        }

        TEST_CASE("Pool callback operations perform no general heap allocations", "[unit][audio][memory]") {
            auto pool = Pool();
            const auto owner = Owner();
            const auto before = allocations.load(std::memory_order_relaxed);
            auto first = pool.Acquire();
            auto second = pool.Acquire();
            const auto exhausted = pool.Acquire();
            const auto live = pool.Resolve(first.handle);
            const auto retired = pool.Retire(first.handle, 1);
            const auto invalid = pool.Retire({}, 1);
            const auto reclaimed = pool.Reclaim(owner, 1, 2);
            const auto reused = pool.Acquire();
            const auto stats = pool.Stats();
            const auto after = allocations.load(std::memory_order_relaxed);
            REQUIRE(after == before);
            REQUIRE(first.status == AudioMemoryStatus::Ok);
            REQUIRE(second.status == AudioMemoryStatus::Ok);
            REQUIRE(exhausted.status == AudioMemoryStatus::Exhausted);
            REQUIRE(live.size() == 65);
            REQUIRE(retired == AudioMemoryStatus::Ok);
            REQUIRE(invalid == AudioMemoryStatus::InvalidHandle);
            REQUIRE(reclaimed.reclaimed == 1);
            REQUIRE(reused.status == AudioMemoryStatus::Ok);
            REQUIRE(stats.failedAllocations == 1);
        }

        TEST_CASE("Pool moves preserve storage identity and leave inert sources", "[unit][audio][memory]") {
            auto original = Pool();
            const auto block = original.Acquire();
            auto moved = std::move(original);
            REQUIRE(moved.Resolve(block.handle).data() == block.bytes.data());
            REQUIRE(original.Resolve(block.handle).empty());
            REQUIRE(original.Acquire().status == AudioMemoryStatus::Inactive);
            REQUIRE(original.Retire(block.handle, 1) == AudioMemoryStatus::Inactive);
            REQUIRE(original.Reclaim(Owner(), 1, 1).status == AudioMemoryStatus::Inactive);
            REQUIRE(original.Stats().reservedBytes == 0);
            auto destination = Pool();
            destination = std::move(moved);
            REQUIRE(destination.Resolve(block.handle).data() == block.bytes.data());
            REQUIRE(moved.Acquire().status == AudioMemoryStatus::Inactive);
            REQUIRE(destination.Reclaim(Owner(), std::numeric_limits<std::uint64_t>::max(), 2).status == AudioMemoryStatus::Ok);
            REQUIRE(destination.Retire(block.handle, 0) == AudioMemoryStatus::InvalidEpoch);
            REQUIRE(destination.Retire(block.handle, std::numeric_limits<std::uint64_t>::max()) == AudioMemoryStatus::InvalidEpoch);
        }

        TEST_CASE("Scratch admission rejects invalid owners dimensions and allocation failure", "[unit][audio][memory]") {
            REQUIRE(AudioScratchArena::Create({}, 64).HasError());
            for (const auto size :
                 {std::size_t{0}, std::size_t{1}, std::size_t{63}, std::size_t{65}, std::numeric_limits<std::size_t>::max()}) {
                const auto result = AudioScratchArena::Create(Owner(), size);
                REQUIRE(result.HasError());
                REQUIRE(result.ErrorValue().code.Value() == AudioErrors::MemoryInvalid.code.Value());
            }
            const auto large = AudioScratchArena::Create(Owner(), MaximumAudioMemoryBytes + 64);
            REQUIRE(large.HasError());
            REQUIRE(large.ErrorValue().code.Value() == AudioErrors::MemoryBudgetExceeded.code.Value());
            failAlignedAllocation = true;
            const auto unavailable = AudioScratchArena::Create(Owner(), 64);
            REQUIRE(unavailable.HasError());
            REQUIRE(unavailable.ErrorValue().code.Value() == AudioErrors::MemoryAllocationFailed.code.Value());
            REQUIRE_FALSE(failAlignedAllocation);
        }

        TEST_CASE("Scratch aligns and clears payload and padding with exact bounded accounting", "[unit][audio][memory]") {
            auto arena = Arena();
            REQUIRE(arena.BeginEpoch(Owner(), 1) == AudioMemoryStatus::Ok);
            auto first = arena.Allocate(1);
            auto second = arena.Allocate(65);
            REQUIRE(first.status == AudioMemoryStatus::Ok);
            REQUIRE(second.status == AudioMemoryStatus::Ok);
            REQUIRE(first.bytes.size() == 1);
            REQUIRE(second.bytes.size() == 65);
            REQUIRE(reinterpret_cast<std::uintptr_t>(first.bytes.data()) % 64 == 0);
            REQUIRE(second.bytes.data() - first.bytes.data() == 64);
            REQUIRE(std::ranges::all_of(second.bytes, [](const auto byte) {
                return byte == std::byte{0};
            }));
            REQUIRE(arena.Stats().reservedBytes == 256);
            REQUIRE(arena.Stats().usedBytes == 192);
            REQUIRE(arena.Stats().peakBytes == 192);
            REQUIRE(arena.Allocate(65).status == AudioMemoryStatus::Exhausted);
            REQUIRE(arena.Stats().usedBytes == 192);
            REQUIRE(arena.Allocate(64).status == AudioMemoryStatus::Ok);
            REQUIRE(arena.Allocate(1).status == AudioMemoryStatus::Exhausted);
            REQUIRE(arena.Stats().failedAllocations == 2);
        }

        TEST_CASE("Scratch epoch boundaries reject stale foreign and wrapping identities", "[unit][audio][memory]") {
            auto arena = Arena();
            REQUIRE(arena.Allocate(1).status == AudioMemoryStatus::Inactive);
            REQUIRE(arena.BeginEpoch(Owner(), 0) == AudioMemoryStatus::InvalidEpoch);
            REQUIRE(arena.BeginEpoch(Owner(8), 1) == AudioMemoryStatus::InvalidEpoch);
            REQUIRE(arena.BeginEpoch(Owner(), 1) == AudioMemoryStatus::Ok);
            auto block = arena.Allocate(64);
            std::ranges::fill(block.bytes, std::byte{0x7f});
            REQUIRE(arena.BeginEpoch(Owner(), 1) == AudioMemoryStatus::InvalidEpoch);
            REQUIRE(arena.Stats().usedBytes == 64);
            REQUIRE(block.bytes[0] == std::byte{0x7f});
            REQUIRE(arena.BeginEpoch(Owner(), 2) == AudioMemoryStatus::Ok);
            block = arena.Allocate(64);
            REQUIRE(block.epoch == 2);
            REQUIRE(std::ranges::all_of(block.bytes, [](const auto byte) {
                return byte == std::byte{0};
            }));
            REQUIRE(arena.Stats().peakBytes == 64);
            REQUIRE(arena.Stats().failedAllocations == 1);
            REQUIRE(arena.BeginEpoch(Owner(), std::numeric_limits<std::uint64_t>::max()) == AudioMemoryStatus::Ok);
            REQUIRE(arena.BeginEpoch(Owner(), 1) == AudioMemoryStatus::InvalidEpoch);
        }

        TEST_CASE("Scratch callback success exhaustion and reset perform no heap allocations", "[unit][audio][memory]") {
            auto arena = Arena(64);
            const auto before = allocations.load(std::memory_order_relaxed);
            const auto begin = arena.BeginEpoch(Owner(), 1);
            const auto invalid = arena.Allocate(0);
            const auto huge = arena.Allocate(std::numeric_limits<std::size_t>::max());
            const auto first = arena.Allocate(64);
            const auto exhausted = arena.Allocate(1);
            const auto reset = arena.BeginEpoch(Owner(), 2);
            const auto reused = arena.Allocate(64);
            const auto after = allocations.load(std::memory_order_relaxed);
            REQUIRE(after == before);
            REQUIRE(begin == AudioMemoryStatus::Ok);
            REQUIRE(reset == AudioMemoryStatus::Ok);
            REQUIRE(invalid.status == AudioMemoryStatus::InvalidRequest);
            REQUIRE(huge.status == AudioMemoryStatus::Exhausted);
            REQUIRE(first.status == AudioMemoryStatus::Ok);
            REQUIRE(exhausted.status == AudioMemoryStatus::Exhausted);
            REQUIRE(reused.status == AudioMemoryStatus::Ok);
            REQUIRE(reused.bytes.data() == first.bytes.data());
            REQUIRE(arena.Stats().failedAllocations == 3);
        }

        TEST_CASE("Scratch moves transfer storage and leave inert sources", "[unit][audio][memory]") {
            auto original = Arena();
            REQUIRE(original.BeginEpoch(Owner(), 1) == AudioMemoryStatus::Ok);
            const auto address = original.Allocate(64).bytes.data();
            auto moved = std::move(original);
            REQUIRE(original.BeginEpoch(Owner(), 2) == AudioMemoryStatus::Inactive);
            REQUIRE(original.Allocate(1).status == AudioMemoryStatus::Inactive);
            REQUIRE(original.Stats().reservedBytes == 0);
            auto assigned = Arena();
            assigned = std::move(moved);
            REQUIRE(assigned.Stats().usedBytes == 64);
            REQUIRE(assigned.Allocate(64).bytes.data() == address + 64);
        }
    }  // namespace
}  // namespace Horo::Audio
