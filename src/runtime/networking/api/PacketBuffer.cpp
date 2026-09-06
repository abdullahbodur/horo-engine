#include "Horo/Network/PacketBuffer.h"

#include "Horo/Network/NetworkErrors.h"

#include <algorithm>
#include <array>
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
        std::size_t outstanding{};
    };

    namespace {
        template <typename T> Result<T> Fail(const ErrorCodeDescriptor &code) {
            return Result<T>::Failure(MakeError(code));
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
        return state_ && slot_ < state_->slots.size() && state_->slots[slot_].leased && state_->slots[slot_].generation == generation_;
    }

    std::span<const std::byte> PacketBuffer::Bytes() const noexcept {
        if (!IsValid())
            return {};
        const auto &slot = state_->slots[slot_];
        return {Storage(slot, state_->descriptor), slot.size};
    }

    bool PacketBuffer::UsesInlineStorage() const noexcept {
        return IsValid() && state_->slots[slot_].size <= state_->descriptor.inlineBytes;
    }

    void PacketBuffer::Reset() noexcept {
        if (IsValid()) {
            auto &slot = state_->slots[slot_];
            slot.leased = false;
            slot.size = 0;
            --state_->outstanding;
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
        const auto found = std::ranges::find_if(state_->slots, [](const auto &slot) {
            return !slot.leased;
        });
        if (found == state_->slots.end())
            return Fail<PacketBuffer>(NetworkErrors::PacketBufferPoolExhausted);
        found->size = bytes.size();
        if (!bytes.empty())
            std::ranges::copy(bytes, Storage(*found, state_->descriptor));
        ++found->generation;
        if (found->generation == 0)
            ++found->generation;
        found->leased = true;
        ++state_->outstanding;
        return Result<PacketBuffer>::Success(
            PacketBuffer{state_, static_cast<std::size_t>(found - state_->slots.begin()), found->generation});
    }

    std::size_t PacketBufferPool::Outstanding() const noexcept {
        return state_ ? state_->outstanding : 0;
    }
}  // namespace Horo::Network
