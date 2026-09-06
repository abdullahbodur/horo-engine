#include "Horo/Network/PacketQueue.h"

#include "Horo/Network/NetworkErrors.h"

#include <limits>
#include <new>

namespace Horo::Network {
    namespace {
        template <typename T> Result<T> Fail(const ErrorCodeDescriptor &code) {
            return Result<T>::Failure(MakeError(code));
        }

        bool AddFits(const std::size_t left, const std::size_t right, const std::size_t maximum) noexcept {
            return right <= maximum && left <= maximum - right;
        }
    }  // namespace

    PacketQueue::PacketQueue(PacketQueueDescriptor descriptor, std::unique_ptr<std::optional<QueuedPacket>[]> records) noexcept
        : descriptor_(descriptor), records_(std::move(records)) {}

    Result<PacketQueue> PacketQueue::Create(const PacketQueueDescriptor &descriptor) {
        if (descriptor.maximumPackets == 0 || descriptor.maximumBytes == 0 || descriptor.maximumPacketsPerConnection == 0 ||
            descriptor.maximumBytesPerConnection == 0 || descriptor.overflow >= PacketQueueOverflowPolicy::Count)
            return Fail<PacketQueue>(NetworkErrors::PacketQueueInvalid);
        try {
            auto records = std::make_unique_for_overwrite<std::optional<QueuedPacket>[]>(descriptor.maximumPackets);
            PacketQueue queue{descriptor, std::move(records)};
            queue.next_ = std::make_unique_for_overwrite<std::size_t[]>(descriptor.maximumPackets);
            queue.previous_ = std::make_unique_for_overwrite<std::size_t[]>(descriptor.maximumPackets);
            queue.nextForConnection_ = std::make_unique_for_overwrite<std::size_t[]>(descriptor.maximumPackets);
            queue.previousForConnection_ = std::make_unique_for_overwrite<std::size_t[]>(descriptor.maximumPackets);
            queue.free_ = std::make_unique_for_overwrite<std::size_t[]>(descriptor.maximumPackets);
            queue.accounting_ = std::make_unique<ConnectionAccounting[]>(descriptor.maximumPackets);
            queue.freeCount_ = descriptor.maximumPackets;
            for (std::size_t index = 0; index < descriptor.maximumPackets; ++index)
                queue.free_[index] = descriptor.maximumPackets - index - 1;
            return Result<PacketQueue>::Success(std::move(queue));
        } catch (const std::bad_alloc &) {
            return Fail<PacketQueue>(NetworkErrors::PacketQueueCapacityExceeded);
        }
    }

    bool PacketQueue::Fits(const QueuedPacket &packet) const noexcept {
        if (!packet.connection.IsValid() || !packet.payload.IsValid() || packet.delivery >= DeliveryPolicy::Count)
            return false;
        const auto accounting = FindAccounting(packet.connection);
        const auto connectionPackets = accounting == InvalidIndex ? 0 : accounting_[accounting].packets;
        const auto connectionBytes = accounting == InvalidIndex ? 0 : accounting_[accounting].bytes;
        const auto bytes = packet.payload.Bytes().size();
        return size_ < descriptor_.maximumPackets && AddFits(bytes_, bytes, descriptor_.maximumBytes) &&
               connectionPackets < descriptor_.maximumPacketsPerConnection &&
               AddFits(connectionBytes, bytes, descriptor_.maximumBytesPerConnection);
    }

    bool PacketQueue::FitsAfterRemoving(const QueuedPacket &packet, const std::size_t removed) const noexcept {
        const auto accounting = FindAccounting(packet.connection);
        auto connectionPackets = accounting == InvalidIndex ? 0 : accounting_[accounting].packets;
        auto connectionBytes = accounting == InvalidIndex ? 0 : accounting_[accounting].bytes;
        const auto removedBytes = records_[removed]->payload.Bytes().size();
        if (records_[removed]->connection == packet.connection) {
            --connectionPackets;
            connectionBytes -= removedBytes;
        }
        const auto packetBytes = packet.payload.Bytes().size();
        const auto remainingBytes = bytes_ - removedBytes;
        return size_ - 1 < descriptor_.maximumPackets && AddFits(remainingBytes, packetBytes, descriptor_.maximumBytes) &&
               connectionPackets < descriptor_.maximumPacketsPerConnection &&
               AddFits(connectionBytes, packetBytes, descriptor_.maximumBytesPerConnection);
    }

    std::optional<std::size_t> PacketQueue::OldestFor(const PacketConnectionId connection) const noexcept {
        const auto accounting = FindAccounting(connection);
        return accounting == InvalidIndex ? std::nullopt : std::optional{accounting_[accounting].head};
    }

    std::size_t PacketQueue::FindAccounting(const PacketConnectionId connection) const noexcept {
        const auto start = static_cast<std::size_t>(connection.Value()) % descriptor_.maximumPackets;
        for (std::size_t offset = 0; offset < descriptor_.maximumPackets; ++offset) {
            const auto index = (start + offset) % descriptor_.maximumPackets;
            if (accounting_[index].state == AccountingState::Empty)
                return InvalidIndex;
            if (accounting_[index].state == AccountingState::Occupied && accounting_[index].connection == connection)
                return index;
        }
        return InvalidIndex;
    }

    std::size_t PacketQueue::FindOrCreateAccounting(const PacketConnectionId connection) noexcept {
        const auto start = static_cast<std::size_t>(connection.Value()) % descriptor_.maximumPackets;
        auto reusable = InvalidIndex;
        for (std::size_t offset = 0; offset < descriptor_.maximumPackets; ++offset) {
            const auto index = (start + offset) % descriptor_.maximumPackets;
            if (accounting_[index].state == AccountingState::Occupied && accounting_[index].connection == connection)
                return index;
            if (accounting_[index].state == AccountingState::Tombstone && reusable == InvalidIndex)
                reusable = index;
            if (accounting_[index].state == AccountingState::Empty) {
                reusable = reusable == InvalidIndex ? index : reusable;
                break;
            }
        }
        auto &entry = accounting_[reusable];
        entry = {connection, 0, 0, InvalidIndex, InvalidIndex, AccountingState::Occupied};
        return reusable;
    }

    Result<PacketQueueAdmission> PacketQueue::Admit(QueuedPacket packet, const PacketQueueAdmission outcome) noexcept {
        const auto accountingIndex = FindOrCreateAccounting(packet.connection);
        auto &accounting = accounting_[accountingIndex];
        const auto packetBytes = packet.payload.Bytes().size();
        const auto index = free_[--freeCount_];
        records_[index] = std::move(packet);
        previous_[index] = tail_;
        next_[index] = InvalidIndex;
        if (tail_ != InvalidIndex)
            next_[tail_] = index;
        else
            head_ = index;
        tail_ = index;
        previousForConnection_[index] = accounting.tail;
        nextForConnection_[index] = InvalidIndex;
        if (accounting.tail != InvalidIndex)
            nextForConnection_[accounting.tail] = index;
        else
            accounting.head = index;
        accounting.tail = index;
        ++accounting.packets;
        accounting.bytes += packetBytes;
        ++size_;
        bytes_ += packetBytes;
        return Result<PacketQueueAdmission>::Success(outcome);
    }

    std::optional<QueuedPacket> PacketQueue::Take(const std::size_t index) noexcept {
        const auto packetBytes = records_[index]->payload.Bytes().size();
        const auto accountingIndex = FindAccounting(records_[index]->connection);
        auto &accounting = accounting_[accountingIndex];
        if (previous_[index] != InvalidIndex)
            next_[previous_[index]] = next_[index];
        else
            head_ = next_[index];
        if (next_[index] != InvalidIndex)
            previous_[next_[index]] = previous_[index];
        else
            tail_ = previous_[index];
        if (previousForConnection_[index] != InvalidIndex)
            nextForConnection_[previousForConnection_[index]] = nextForConnection_[index];
        else
            accounting.head = nextForConnection_[index];
        if (nextForConnection_[index] != InvalidIndex)
            previousForConnection_[nextForConnection_[index]] = previousForConnection_[index];
        else
            accounting.tail = previousForConnection_[index];
        --accounting.packets;
        accounting.bytes -= packetBytes;
        if (accounting.packets == 0)
            accounting.state = AccountingState::Tombstone;
        auto result = std::move(records_[index]);
        records_[index].reset();
        free_[freeCount_++] = index;
        --size_;
        bytes_ -= packetBytes;
        return result;
    }

    void PacketQueue::Erase(const std::size_t index) noexcept {
        static_cast<void>(Take(index));
    }

    Result<PacketQueueAdmission> PacketQueue::HandleOverflow(QueuedPacket packet) noexcept {
        if (descriptor_.overflow == PacketQueueOverflowPolicy::RejectIncoming)
            return Fail<PacketQueueAdmission>(NetworkErrors::PacketQueueFull);
        if (descriptor_.overflow == PacketQueueOverflowPolicy::CloseConnection)
            return Result<PacketQueueAdmission>::Success(PacketQueueAdmission::ConnectionMustClose);
        const auto sameConnection = OldestFor(packet.connection);
        if (descriptor_.overflow == PacketQueueOverflowPolicy::ReplaceOldestForConnection) {
            if (!sameConnection.has_value() || !FitsAfterRemoving(packet, *sameConnection))
                return Fail<PacketQueueAdmission>(NetworkErrors::PacketQueueFull);
            Erase(*sameConnection);
            return Admit(std::move(packet), PacketQueueAdmission::ReplacedOldest);
        }
        while (!Fits(packet) && size_ != 0)
            Erase(head_);
        if (!Fits(packet))
            return Fail<PacketQueueAdmission>(NetworkErrors::PacketQueueFull);
        return Admit(std::move(packet), PacketQueueAdmission::DroppedOldest);
    }

    Result<PacketQueueAdmission> PacketQueue::Enqueue(QueuedPacket packet, const TransportAdmissionState state) noexcept {
        if (state == TransportAdmissionState::Cancelled)
            return Fail<PacketQueueAdmission>(NetworkErrors::TransportOperationCancelled);
        if (state == TransportAdmissionState::ShuttingDown)
            return Fail<PacketQueueAdmission>(NetworkErrors::TransportShuttingDown);
        if (state != TransportAdmissionState::Accepting || !packet.connection.IsValid() || !packet.payload.IsValid() ||
            packet.delivery >= DeliveryPolicy::Count)
            return Fail<PacketQueueAdmission>(NetworkErrors::PacketQueueInvalid);
        const auto packetBytes = packet.payload.Bytes().size();
        if (packetBytes > descriptor_.maximumBytes || packetBytes > descriptor_.maximumBytesPerConnection)
            return Fail<PacketQueueAdmission>(NetworkErrors::PacketQueueCapacityExceeded);
        return Fits(packet) ? Admit(std::move(packet), PacketQueueAdmission::Enqueued) : HandleOverflow(std::move(packet));
    }

    std::optional<QueuedPacket> PacketQueue::Pop() noexcept {
        if (size_ == 0)
            return std::nullopt;
        return Take(head_);
    }

    void PacketQueue::Drain() noexcept {
        while (size_ != 0)
            Erase(head_);
    }
}  // namespace Horo::Network
