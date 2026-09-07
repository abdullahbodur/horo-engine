#include "Horo/Network/NetworkErrors.h"
#include "Horo/Network/PacketQueue.h"
#include "NetworkTestUtils.h"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <limits>
#include <thread>
#include <vector>

namespace Horo::Network {
    using TestSupport::RequireError;

    namespace {
        QueuedPacket MakePacket(PacketBufferPool &pool, const ConnectionHandle connection, const std::size_t bytes) {
            const std::vector payload(bytes, std::byte{0x5a});
            auto buffer = pool.Acquire(payload);
            REQUIRE(buffer.HasValue());
            return {connection, ChannelId{}, DeliveryPolicy::ReliableOrdered, std::move(buffer).Value()};
        }

        QueuedPacket MakePacket(PacketBufferPool &pool, const std::uint16_t connection, const std::size_t bytes) {
            return MakePacket(pool, ConnectionHandle::Create(connection, 1).Value(), bytes);
        }

        PacketQueueDescriptor QueueLimits(const PacketQueueOverflowPolicy policy) {
            return {2, 8, 2, 8, policy};
        }
    }  // namespace

    TEST_CASE("Packet buffer leases use prepared storage and release exact generations", "[unit][network][packet]") {
        auto createdPool = PacketBufferPool::Create({2, 8, 4});
        REQUIRE(createdPool.HasValue());
        auto pool = std::move(createdPool).Value();
        const std::array small{std::byte{1}, std::byte{2}};
        auto acquiredFirst = pool.Acquire(small);
        REQUIRE(acquiredFirst.HasValue());
        auto first = std::move(acquiredFirst).Value();
        REQUIRE(first.UsesInlineStorage());
        REQUIRE(first.Bytes().size() == 2);
        auto large = pool.Acquire(std::array<std::byte, 8>{});
        REQUIRE(large.HasValue());
        REQUIRE_FALSE(large.Value().UsesInlineStorage());
        RequireError(pool.Acquire({}), NetworkErrors::PacketBufferPoolExhausted);
        PacketBuffer moved = std::move(first);
        first.Reset();
        REQUIRE(pool.Outstanding() == 2);
        moved.Reset();
        moved.Reset();
        REQUIRE(pool.Outstanding() == 1);
        REQUIRE(pool.Acquire(small).HasValue());

        PacketBuffer retained;
        {
            auto shortLivedPool = PacketBufferPool::Create({1, 4, 4}).Value();
            retained = std::move(shortLivedPool.Acquire(small)).Value();
        }
        REQUIRE(retained.Bytes().size() == small.size());
        retained.Reset();
    }

    TEST_CASE("Packet buffer pool synchronizes distinct lease release and reuses its prepared free list", "[unit][network][packet]") {
        auto pool = PacketBufferPool::Create({2, 4, 4}).Value();
        auto first = std::move(pool.Acquire(std::array<std::byte, 1>{})).Value();
        auto second = std::move(pool.Acquire(std::array<std::byte, 1>{})).Value();
        std::thread firstRelease{[lease = std::move(first)]() mutable {
            lease.Reset();
        }};
        std::thread secondRelease{[lease = std::move(second)]() mutable {
            lease.Reset();
        }};
        firstRelease.join();
        secondRelease.join();
        REQUIRE(pool.Outstanding() == 0);
        REQUIRE(pool.Acquire(std::array<std::byte, 4>{}).HasValue());
    }

    TEST_CASE("Packet queue rejects capacity and returns ownership exactly once", "[unit][network][packet]") {
        auto pool = PacketBufferPool::Create({3, 8, 4}).Value();
        auto queue = PacketQueue::Create(QueueLimits(PacketQueueOverflowPolicy::RejectIncoming)).Value();
        REQUIRE(queue.Enqueue(MakePacket(pool, 1, 4)).HasValue());
        REQUIRE(queue.Enqueue(MakePacket(pool, 1, 4)).HasValue());
        REQUIRE(pool.Outstanding() == 2);
        RequireError(queue.Enqueue(MakePacket(pool, 1, 1)), NetworkErrors::PacketQueueFull);
        REQUIRE(pool.Outstanding() == 2);
        RequireError(queue.Enqueue(MakePacket(pool, 2, 1), TransportAdmissionState::Cancelled), NetworkErrors::TransportOperationCancelled);
        REQUIRE(pool.Outstanding() == 2);
        queue.Drain();
        REQUIRE(pool.Outstanding() == 0);
    }

    TEST_CASE("Packet queue applies every overload policy deterministically", "[unit][network][packet]") {
        for (const auto policy : {PacketQueueOverflowPolicy::DropOldest, PacketQueueOverflowPolicy::ReplaceOldestForConnection}) {
            auto pool = PacketBufferPool::Create({3, 8, 4}).Value();
            auto queue = PacketQueue::Create(QueueLimits(policy)).Value();
            REQUIRE(queue.Enqueue(MakePacket(pool, 1, 4)).Value() == PacketQueueAdmission::Enqueued);
            REQUIRE(queue.Enqueue(MakePacket(pool, 2, 4)).Value() == PacketQueueAdmission::Enqueued);
            const auto admitted = queue.Enqueue(MakePacket(pool, policy == PacketQueueOverflowPolicy::DropOldest ? 3 : 1, 4));
            REQUIRE(admitted.HasValue());
            REQUIRE(queue.Size() == 2);
            REQUIRE(pool.Outstanding() == 2);
        }
        auto pool = PacketBufferPool::Create({2, 8, 4}).Value();
        auto queue = PacketQueue::Create({1, 8, 1, 8, PacketQueueOverflowPolicy::CloseConnection}).Value();
        REQUIRE(queue.Enqueue(MakePacket(pool, 1, 4)).HasValue());
        REQUIRE(queue.Enqueue(MakePacket(pool, 2, 4)).Value() == PacketQueueAdmission::ConnectionMustClose);
        REQUIRE(pool.Outstanding() == 1);
    }

    TEST_CASE("Packet queue validates malformed overflow and shutdown boundaries", "[unit][network][packet]") {
        RequireError(PacketBufferPool::Create({0, 1, 1}), NetworkErrors::PacketBufferInvalid);
        RequireError(PacketBufferPool::Create({1, 1, 257}), NetworkErrors::PacketBufferInvalid);
        RequireError(PacketQueue::Create({}), NetworkErrors::PacketQueueInvalid);
        auto pool = PacketBufferPool::Create({1, 16, 4}).Value();
        auto createdMaximalQueue =
            PacketQueue::Create({2, std::numeric_limits<std::size_t>::max(), 2, std::numeric_limits<std::size_t>::max(),
                                 PacketQueueOverflowPolicy::RejectIncoming});
        REQUIRE(createdMaximalQueue.HasValue());
        auto maximalAccounting = std::move(createdMaximalQueue).Value();
        REQUIRE(maximalAccounting.Enqueue(MakePacket(pool, 1, 1)).HasValue());
        REQUIRE(maximalAccounting.Bytes() == 1);
        maximalAccounting.Drain();
        auto queue = PacketQueue::Create(QueueLimits(PacketQueueOverflowPolicy::RejectIncoming)).Value();
        RequireError(queue.Enqueue(MakePacket(pool, 1, 9)), NetworkErrors::PacketQueueCapacityExceeded);
        REQUIRE(pool.Outstanding() == 0);
        RequireError(queue.Enqueue(MakePacket(pool, 1, 1), TransportAdmissionState::ShuttingDown), NetworkErrors::TransportShuttingDown);
        REQUIRE(pool.Outstanding() == 0);
    }

    TEST_CASE("Packet queue enforces per-connection packet and byte limits independently", "[unit][network][packet]") {
        auto pool = PacketBufferPool::Create({5, 8, 4}).Value();
        auto countQueue = PacketQueue::Create({4, 16, 1, 8, PacketQueueOverflowPolicy::RejectIncoming}).Value();
        REQUIRE(countQueue.Enqueue(MakePacket(pool, 1, 4)).HasValue());
        REQUIRE(countQueue.Enqueue(MakePacket(pool, 2, 4)).HasValue());
        RequireError(countQueue.Enqueue(MakePacket(pool, 1, 1)), NetworkErrors::PacketQueueFull);
        REQUIRE(countQueue.Size() == 2);
        countQueue.Drain();

        auto byteQueue = PacketQueue::Create({4, 16, 3, 4, PacketQueueOverflowPolicy::RejectIncoming}).Value();
        REQUIRE(byteQueue.Enqueue(MakePacket(pool, 1, 3)).HasValue());
        REQUIRE(byteQueue.Enqueue(MakePacket(pool, 2, 3)).HasValue());
        RequireError(byteQueue.Enqueue(MakePacket(pool, 1, 2)), NetworkErrors::PacketQueueFull);
        REQUIRE(byteQueue.Bytes() == 6);
    }

    TEST_CASE("Packet queue accounting distinguishes recycled connection generations", "[unit][network][packet]") {
        auto pool = PacketBufferPool::Create({2, 4, 4}).Value();
        auto queue = PacketQueue::Create({2, 8, 1, 4, PacketQueueOverflowPolicy::RejectIncoming}).Value();
        const auto first = ConnectionHandle::Create(5, 1).Value();
        const auto recycled = first.NextGeneration().Value();
        REQUIRE(queue.Enqueue(MakePacket(pool, first, 4)).HasValue());
        REQUIRE(queue.Enqueue(MakePacket(pool, recycled, 4)).HasValue());
        REQUIRE(queue.Size() == 2);
        queue.Drain();
        REQUIRE(pool.Outstanding() == 0);
    }
}  // namespace Horo::Network
