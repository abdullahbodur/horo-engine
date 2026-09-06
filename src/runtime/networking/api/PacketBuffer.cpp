#include "Horo/Network/PacketBuffer.h"

#include "Horo/Network/NetworkErrors.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <new>
#include <vector>

namespace Horo::Network {
    struct PacketBufferPoolState final {
        struct Slot final {
            std::array<std::byte, 256> inlineStorage{};
            std::unique_ptr<std::byte[]> overflowStorage;
            std::size_t size{};
            std::size_t generation{};
            bool leased{};
        };

        PacketBufferPoolDescriptor descriptor;
        std::vector<Slot> slots;
        std::vector<std::size_t> freeSlots;
        std::atomic_flag gate = ATOMIC_FLAG_INIT;
        std::size_t freeCount{};
        std::size_t outstanding{};
    };

    namespace {
        template <typename T> Result<T> Fail(const ErrorCodeDescriptor &code) {
            return Result<T>::Failure(MakeError(code));
        }

        class StateLock final {
        public:
            explicit StateLock(PacketBufferPoolState &state) noexcept : state_(state) {
                while (state_.gate.test_and_set(std::memory_order_acquire)) {
                    state_.gate.wait(true, std::memory_order_relaxed);
                }
            }

            ~StateLock() {
                state_.gate.clear(std::memory_order_release);
                state_.gate.notify_one();
            }

        private:
            PacketBufferPoolState &state_;
        };

        bool IsValidLocked(const PacketBufferPoolState &state, const std::size_t slot, const std::size_t generation) noexcept {
            return slot < state.slots.size() && state.slots[slot].leased && state.slots[slot].generation == generation;
        }

        std::byte *Storage(PacketBufferPoolState::Slot &slot, const PacketBufferPoolDescriptor &descriptor) noexcept {
            return slot.size <= descriptor.inlineBytes ? slot.inlineStorage.data() : slot.overflowStorage.get();
        }

        const std::byte *Storage(const PacketBufferPoolState::Slot &slot, const PacketBufferPoolDescriptor &descriptor) noexcept {
            return slot.size <= descriptor.inlineBytes ? slot.inlineStorage.data() : slot.overflowStorage.get();
        }
    }  // namespace

    PacketBuffer::PacketBuffer(std::shared_ptr<PacketBufferPoolState> state, const std::size_t slot, const std::size_t generation) noexcept
        : state_(std::move(state)), slot_(slot), generation_(generation) {}

    PacketBuffer::~PacketBuffer() {
        Reset();
    }

    PacketBuffer::PacketBuffer(PacketBuffer &&other) noexcept {
        *this = std::move(other);
    }

    PacketBuffer &PacketBuffer::operator=(PacketBuffer &&other) noexcept {
        if (this != &other) {
            Reset();
            state_ = std::move(other.state_);
            slot_ = other.slot_;
            generation_ = other.generation_;
            other.slot_ = 0;
            other.generation_ = 0;
        }
        return *this;
    }

    bool PacketBuffer::IsValid() const noexcept {
        if (!state_)
            return false;
        const StateLock lock{*state_};
        return IsValidLocked(*state_, slot_, generation_);
    }

    std::span<const std::byte> PacketBuffer::Bytes() const noexcept {
        if (!state_)
            return {};
        const StateLock lock{*state_};
        if (!IsValidLocked(*state_, slot_, generation_))
            return {};
        const auto &slot = state_->slots[slot_];
        return {Storage(slot, state_->descriptor), slot.size};
    }

    bool PacketBuffer::UsesInlineStorage() const noexcept {
        if (!state_)
            return false;
        const StateLock lock{*state_};
        return IsValidLocked(*state_, slot_, generation_) && state_->slots[slot_].size <= state_->descriptor.inlineBytes;
    }

    void PacketBuffer::Reset() noexcept {
        if (state_) {
            const StateLock lock{*state_};
            if (IsValidLocked(*state_, slot_, generation_)) {
                auto &slot = state_->slots[slot_];
                slot.leased = false;
                slot.size = 0;
                --state_->outstanding;
                state_->freeSlots[state_->freeCount++] = slot_;
            }
        }
        state_.reset();
        slot_ = 0;
        generation_ = 0;
    }

    Result<PacketBufferPool> PacketBufferPool::Create(const PacketBufferPoolDescriptor &descriptor) {
        if (descriptor.maximumBuffers == 0 || descriptor.maximumBytesPerBuffer == 0 || descriptor.inlineBytes > 256)
            return Fail<PacketBufferPool>(NetworkErrors::PacketBufferInvalid);
        try {
            auto state = std::make_shared<PacketBufferPoolState>();
            state->descriptor = descriptor;
            state->slots.resize(descriptor.maximumBuffers);
            state->freeSlots.resize(descriptor.maximumBuffers);
            state->freeCount = descriptor.maximumBuffers;
            for (std::size_t index = 0; index < descriptor.maximumBuffers; ++index)
                state->freeSlots[index] = descriptor.maximumBuffers - index - 1;
            if (descriptor.maximumBytesPerBuffer > descriptor.inlineBytes) {
                for (auto &slot : state->slots)
                    slot.overflowStorage = std::make_unique_for_overwrite<std::byte[]>(descriptor.maximumBytesPerBuffer);
            }
            return Result<PacketBufferPool>::Success(PacketBufferPool{std::move(state)});
        } catch (const std::bad_alloc &) {
            return Fail<PacketBufferPool>(NetworkErrors::PacketBufferCapacityExceeded);
        }
    }

    Result<PacketBuffer> PacketBufferPool::Acquire(const std::span<const std::byte> bytes) {
        if (!state_ || bytes.size() > state_->descriptor.maximumBytesPerBuffer)
            return Fail<PacketBuffer>(NetworkErrors::PacketBufferCapacityExceeded);
        const StateLock lock{*state_};
        if (state_->freeCount == 0)
            return Fail<PacketBuffer>(NetworkErrors::PacketBufferPoolExhausted);
        const auto slotIndex = state_->freeSlots[--state_->freeCount];
        auto &slot = state_->slots[slotIndex];
        slot.size = bytes.size();
        if (!bytes.empty())
            std::ranges::copy(bytes, Storage(slot, state_->descriptor));
        ++slot.generation;
        if (slot.generation == 0)
            ++slot.generation;
        slot.leased = true;
        ++state_->outstanding;
        return Result<PacketBuffer>::Success(PacketBuffer{state_, slotIndex, slot.generation});
    }

    std::size_t PacketBufferPool::Outstanding() const noexcept {
        if (!state_)
            return 0;
        const StateLock lock{*state_};
        return state_->outstanding;
    }
}  // namespace Horo::Network
