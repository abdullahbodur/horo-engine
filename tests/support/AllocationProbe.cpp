#include "AllocationProbe.h"

#include <atomic>
#include <cstdlib>
#include <new>

namespace {
    class AllocationMeter final {
    public:
        [[nodiscard]] static void *Acquire(const std::size_t byteCount) {
            count_.fetch_add(1, std::memory_order_relaxed);
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

    private:
        static inline std::atomic<std::size_t> count_{};
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
    std::size_t Count() noexcept {
        return AllocationMeter::Count();
    }
}  // namespace Horo::Tests::AllocationProbe
