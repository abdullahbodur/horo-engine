#pragma once

/**
 * @file PacketQueue.h
 * @brief Synchronization-neutral preallocated packet queue and explicit overload policy.
 */

#include "Horo/Network/NetworkHandles.h"
#include "Horo/Network/PacketBuffer.h"
#include "Horo/Network/TransportCapabilities.h"

#include <cstddef>
#include <cstdint>
#include <optional>

namespace Horo::Network {
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
        ConnectionHandle connection;
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
        static constexpr std::size_t InvalidIndex = static_cast<std::size_t>(-1);

        enum class AccountingState : std::uint8_t {
            Empty,
            Occupied,
            Tombstone
        };

        struct ConnectionAccounting final {
            ConnectionHandle connection;
            std::size_t packets{};
            std::size_t bytes{};
            std::size_t head{InvalidIndex};
            std::size_t tail{InvalidIndex};
            AccountingState state{AccountingState::Empty};
        };

        explicit PacketQueue(const PacketQueueDescriptor &descriptor, std::unique_ptr<std::optional<QueuedPacket>[]> records) noexcept;
        [[nodiscard]] bool Fits(const QueuedPacket &packet) const noexcept;
        [[nodiscard]] bool FitsAfterRemoving(const QueuedPacket &packet, std::size_t removed) const noexcept;
        [[nodiscard]] std::optional<std::size_t> OldestFor(ConnectionHandle connection) const noexcept;
        [[nodiscard]] std::size_t FindAccounting(ConnectionHandle connection) const noexcept;
        [[nodiscard]] std::size_t FindOrCreateAccounting(ConnectionHandle connection) const noexcept;
        [[nodiscard]] Result<PacketQueueAdmission> Admit(QueuedPacket packet, PacketQueueAdmission outcome) noexcept;
        [[nodiscard]] Result<PacketQueueAdmission> HandleOverflow(QueuedPacket packet) noexcept;
        [[nodiscard]] std::optional<QueuedPacket> Take(std::size_t index) noexcept;
        void Erase(std::size_t index) noexcept;

        PacketQueueDescriptor descriptor_;
        std::unique_ptr<std::optional<QueuedPacket>[]> records_;
        std::unique_ptr<std::size_t[]> next_;
        std::unique_ptr<std::size_t[]> previous_;
        std::unique_ptr<std::size_t[]> nextForConnection_;
        std::unique_ptr<std::size_t[]> previousForConnection_;
        std::unique_ptr<std::size_t[]> free_;
        std::unique_ptr<ConnectionAccounting[]> accounting_;
        std::size_t head_{InvalidIndex};
        std::size_t tail_{InvalidIndex};
        std::size_t freeCount_{};
        std::size_t size_{};
        std::size_t bytes_{};
    };
}  // namespace Horo::Network
