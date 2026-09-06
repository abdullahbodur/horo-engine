#pragma once

/**
 * @file PacketQueue.h
 * @brief Synchronization-neutral preallocated packet queue and explicit overload policy.
 */

#include "Horo/Network/PacketBuffer.h"
#include "Horo/Network/ProtocolIdentity.h"
#include "Horo/Network/TransportCapabilities.h"

#include <cstddef>
#include <cstdint>
#include <optional>

namespace Horo::Network {
    /** @brief Stable non-zero connection identity for queue accounting; not a native handle. */
    using PacketConnectionId = ProtocolIdentity<struct PacketConnectionIdentityTag>;

    /** @brief Explicit action selected before a queue becomes full. */
    enum class PacketQueueOverflowPolicy : std::uint8_t {
        RejectIncoming,             /**< Preserve queued work and reject the incoming packet. */
        DropOldest,                 /**< Drop oldest packets until the incoming packet fits. */
        ReplaceOldestForConnection, /**< Replace exactly one oldest packet for the same connection. */
        CloseConnection,            /**< Reject the packet and instruct the caller to close its connection. */
        Count                       /**< Invalid sentinel. */
    };
    /** @brief Observable successful overload outcome. */
    enum class PacketQueueAdmission : std::uint8_t {
        Enqueued,            /**< Admitted without eviction. */
        DroppedOldest,       /**< Admitted after oldest-first eviction. */
        ReplacedOldest,      /**< Admitted after same-connection replacement. */
        ConnectionMustClose, /**< Not admitted; caller must close the connection. */
        Count                /**< Invalid sentinel. */
    };

    /** @brief Fixed global and per-connection queue bounds. */
    struct PacketQueueDescriptor final {
        std::size_t maximumPackets{};
        std::size_t maximumBytes{};
        std::size_t maximumPacketsPerConnection{};
        std::size_t maximumBytesPerConnection{};
        PacketQueueOverflowPolicy overflow{PacketQueueOverflowPolicy::RejectIncoming};
    };

    /** @brief One owned packet record transferred into or out of the queue. */
    struct QueuedPacket final {
        PacketConnectionId connection;
        ChannelId channel;
        DeliveryPolicy delivery{DeliveryPolicy::ReliableOrdered};
        PacketBuffer payload;
    };

    /**
     * @brief Preallocated FIFO storage primitive; callers own all synchronization around every method.
     * @note This type is not a cross-thread queue by itself. Concrete transports choose their private synchronization primitive.
     */
    class PacketQueue final {
    public:
        [[nodiscard]] static Result<PacketQueue> Create(const PacketQueueDescriptor &descriptor);
        [[nodiscard]] Result<PacketQueueAdmission> Enqueue(QueuedPacket packet,
                                                           TransportAdmissionState state = TransportAdmissionState::Accepting) noexcept;
        [[nodiscard]] std::optional<QueuedPacket> Pop() noexcept;
        void Drain() noexcept;

        [[nodiscard]] std::size_t Size() const noexcept {
            return size_;
        }

        [[nodiscard]] std::size_t Bytes() const noexcept {
            return bytes_;
        }

        PacketQueue(PacketQueue &&) noexcept = default;
        PacketQueue &operator=(PacketQueue &&) noexcept = default;
        PacketQueue(const PacketQueue &) = delete;
        PacketQueue &operator=(const PacketQueue &) = delete;

    private:
        explicit PacketQueue(PacketQueueDescriptor descriptor, std::unique_ptr<std::optional<QueuedPacket>[]> records) noexcept;
        [[nodiscard]] bool Fits(const QueuedPacket &packet) const noexcept;
        [[nodiscard]] bool FitsAfterRemoving(const QueuedPacket &packet, std::size_t removed) const noexcept;
        [[nodiscard]] std::optional<std::size_t> OldestFor(PacketConnectionId connection) const noexcept;
        void Erase(std::size_t index) noexcept;

        PacketQueueDescriptor descriptor_;
        std::unique_ptr<std::optional<QueuedPacket>[]> records_;
        std::size_t size_{};
        std::size_t bytes_{};
    };
}  // namespace Horo::Network
