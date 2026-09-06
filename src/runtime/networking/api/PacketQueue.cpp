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
            return Result<PacketQueue>::Success(PacketQueue{descriptor, std::move(records)});
        } catch (const std::bad_alloc &) {
            return Fail<PacketQueue>(NetworkErrors::PacketQueueCapacityExceeded);
        }
    }

    bool PacketQueue::Fits(const QueuedPacket &packet) const noexcept {
        if (!packet.connection.IsValid() || !packet.payload.IsValid() || packet.delivery >= DeliveryPolicy::Count)
            return false;
        std::size_t connectionPackets{};
        std::size_t connectionBytes{};
        for (std::size_t index = 0; index < size_; ++index) {
            if (records_[index]->connection == packet.connection) {
                ++connectionPackets;
                connectionBytes += records_[index]->payload.Bytes().size();
            }
        }
        const auto bytes = packet.payload.Bytes().size();
        return size_ < descriptor_.maximumPackets && AddFits(bytes_, bytes, descriptor_.maximumBytes) &&
               connectionPackets < descriptor_.maximumPacketsPerConnection &&
               AddFits(connectionBytes, bytes, descriptor_.maximumBytesPerConnection);
    }

    bool PacketQueue::FitsAfterRemoving(const QueuedPacket &packet, const std::size_t removed) const noexcept {
        std::size_t connectionPackets{};
        std::size_t connectionBytes{};
        for (std::size_t index = 0; index < size_; ++index) {
            if (index != removed && records_[index]->connection == packet.connection) {
                ++connectionPackets;
                connectionBytes += records_[index]->payload.Bytes().size();
            }
        }
        const auto packetBytes = packet.payload.Bytes().size();
        const auto remainingBytes = bytes_ - records_[removed]->payload.Bytes().size();
        return size_ - 1 < descriptor_.maximumPackets && AddFits(remainingBytes, packetBytes, descriptor_.maximumBytes) &&
               connectionPackets < descriptor_.maximumPacketsPerConnection &&
               AddFits(connectionBytes, packetBytes, descriptor_.maximumBytesPerConnection);
    }

    std::optional<std::size_t> PacketQueue::OldestFor(const PacketConnectionId connection) const noexcept {
        for (std::size_t index = 0; index < size_; ++index)
            if (records_[index]->connection == connection)
                return index;
        return std::nullopt;
    }

    void PacketQueue::Erase(const std::size_t index) noexcept {
        bytes_ -= records_[index]->payload.Bytes().size();
        for (std::size_t current = index; current + 1 < size_; ++current)
            records_[current] = std::move(records_[current + 1]);
        records_[size_ - 1].reset();
        --size_;
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
        if (Fits(packet)) {
            bytes_ += packetBytes;
            records_[size_++] = std::move(packet);
            return Result<PacketQueueAdmission>::Success(PacketQueueAdmission::Enqueued);
        }
        if (descriptor_.overflow == PacketQueueOverflowPolicy::RejectIncoming)
            return Fail<PacketQueueAdmission>(NetworkErrors::PacketQueueFull);
        if (descriptor_.overflow == PacketQueueOverflowPolicy::CloseConnection)
            return Result<PacketQueueAdmission>::Success(PacketQueueAdmission::ConnectionMustClose);
        const auto sameConnection = OldestFor(packet.connection);
        if (descriptor_.overflow == PacketQueueOverflowPolicy::ReplaceOldestForConnection) {
            if (!sameConnection.has_value() || !FitsAfterRemoving(packet, *sameConnection))
                return Fail<PacketQueueAdmission>(NetworkErrors::PacketQueueFull);
            Erase(*sameConnection);
        } else {
            while (!Fits(packet) && size_ != 0)
                Erase(0);
            if (!Fits(packet))
                return Fail<PacketQueueAdmission>(NetworkErrors::PacketQueueFull);
        }
        bytes_ += packetBytes;
        records_[size_++] = std::move(packet);
        return Result<PacketQueueAdmission>::Success(descriptor_.overflow == PacketQueueOverflowPolicy::DropOldest
                                                         ? PacketQueueAdmission::DroppedOldest
                                                         : PacketQueueAdmission::ReplacedOldest);
    }

    std::optional<QueuedPacket> PacketQueue::Pop() noexcept {
        if (size_ == 0)
            return std::nullopt;
        const std::size_t removedBytes = records_[0]->payload.Bytes().size();
        std::optional<QueuedPacket> result{std::move(records_[0])};
        for (std::size_t current = 0; current + 1 < size_; ++current)
            records_[current] = std::move(records_[current + 1]);
        records_[size_ - 1].reset();
        --size_;
        bytes_ -= removedBytes;
        return result;
    }

    void PacketQueue::Drain() noexcept {
        while (size_ != 0)
            Erase(size_ - 1);
    }
}  // namespace Horo::Network
