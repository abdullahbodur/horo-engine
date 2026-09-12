#include "AllocationProbe.h"

#include <atomic>
#include <cstdlib>
#include <limits>
#include <new>

namespace {
    class AllocationMeter final {
    public:
        [[nodiscard]] static void *Acquire(const std::size_t byteCount) {
            count_.fetch_add(1, std::memory_order_relaxed);
            std::size_t remaining = failureCountdown_.load(std::memory_order_relaxed);
            while (remaining != DisabledFailureCountdown) {
                if (remaining == 0) {
                    if (failureCountdown_.compare_exchange_weak(remaining, DisabledFailureCountdown, std::memory_order_relaxed))
                        throw std::bad_alloc{};
                } else if (failureCountdown_.compare_exchange_weak(remaining, remaining - 1, std::memory_order_relaxed)) {
                    break;
                }
            }
            void *const storage = std::malloc(byteCount);
            if (storage == nullptr)
                throw std::bad_alloc{};
            return storage;
        }

        static void Release(void *const storage) noexcept {
            std::free(storage);
        }

        [[nodiscard]] static std::size_t Count() noexcept {
            return count_.load(std::memory_order_relaxed);
        }

        static void FailAfter(const std::size_t successfulAllocations) noexcept {
            failureCountdown_.store(successfulAllocations, std::memory_order_relaxed);
        }

        static void DisableFailures() noexcept {
            failureCountdown_.store(DisabledFailureCountdown, std::memory_order_relaxed);
        }

    private:
        static constexpr std::size_t DisabledFailureCountdown = std::numeric_limits<std::size_t>::max();
        static inline std::atomic<std::size_t> count_{};
        static inline std::atomic<std::size_t> failureCountdown_{DisabledFailureCountdown};
    };
}  // namespace

void *operator new(const std::size_t size) {
    return AllocationMeter::Acquire(size);
}

void *operator new[](const std::size_t size) {
    return AllocationMeter::Acquire(size);
}

void operator delete(void *memory) noexcept {
    AllocationMeter::Release(memory);
}

void operator delete[](void *memory) noexcept {
    AllocationMeter::Release(memory);
}

void operator delete(void *memory, std::size_t) noexcept {
    AllocationMeter::Release(memory);
}

void operator delete[](void *memory, std::size_t) noexcept {
    AllocationMeter::Release(memory);
}

namespace Horo::Tests::AllocationProbe {
    ScopedFailure::ScopedFailure(const std::size_t successfulAllocationsBeforeFailure) noexcept {
        AllocationMeter::FailAfter(successfulAllocationsBeforeFailure);
    }

    ScopedFailure::~ScopedFailure() {
        AllocationMeter::DisableFailures();
    }

    std::size_t Count() noexcept {
        return AllocationMeter::Count();
    }
}  // namespace Horo::Tests::AllocationProbe
