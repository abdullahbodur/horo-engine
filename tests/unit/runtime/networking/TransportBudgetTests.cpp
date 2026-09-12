#include "Horo/Network/NetworkErrors.h"
#include "Horo/Network/TransportBudget.h"
#include "NetworkTestUtils.h"

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>

namespace Horo::Network {
    using TestSupport::RequireError;

    namespace {
        TransportBudgetCapacity Capacity() {
            return {2, 4, 32};
        }

        TransportLimitPolicyV1 Policy(const std::uint64_t revision = 1) {
            return {
                .contractVersion = 1,
                .revision = revision,
                .maximumActiveConnections = 2,
                .maximumQueuedMessages = 3,
                .maximumQueuedBytes = 12,
                .maximumQueuedMessagesPerConnection = 2,
                .maximumQueuedBytesPerConnection = 8,
                .maximumMessagesPerTick = 3,
                .maximumBytesPerTick = 12,
                .maximumMessagesPerConnectionPerTick = 2,
                .maximumBytesPerConnectionPerTick = 8,
                .saturationGraceTicks = 2,
            };
        }

        ConnectionHandle Connection(const std::uint32_t slot = 0, const std::uint32_t generation = 1) {
            return ConnectionHandle::Create(slot, generation).Value();
        }

        TransportBudgetSubmission Reliable(const ConnectionHandle connection, const std::size_t bytes) {
            return {connection, TransportTrafficClass::Reliable, 0, bytes};
        }

        TransportBudgetSubmission Replaceable(const ConnectionHandle connection, const std::uint64_t key, const std::size_t bytes) {
            return {connection, TransportTrafficClass::ReplaceableState, key, bytes};
        }

        TransportBudgetController Controller() {
            auto created = TransportBudgetController::Create(Capacity(), Policy());
            REQUIRE(created.HasValue());
            return std::move(created).Value();
        }
    }  // namespace

    TEST_CASE("Transport budget validates complete finite hard and active bounds", "[unit][network][budget]") {
        auto capacity = Capacity();
        auto policy = Policy();
        REQUIRE(TransportBudgetController::Create(capacity, policy).HasValue());

        policy.contractVersion = 2;
        RequireError(TransportBudgetController::Create(capacity, policy), NetworkErrors::TransportBudgetInvalid);
        policy = Policy();
        policy.revision = 0;
        RequireError(TransportBudgetController::Create(capacity, policy), NetworkErrors::TransportBudgetInvalid);
        policy = Policy();
        policy.maximumActiveConnections = capacity.maximumConnections + 1;
        RequireError(TransportBudgetController::Create(capacity, policy), NetworkErrors::TransportBudgetInvalid);
        policy = Policy();
        policy.maximumQueuedMessagesPerConnection = policy.maximumQueuedMessages + 1;
        RequireError(TransportBudgetController::Create(capacity, policy), NetworkErrors::TransportBudgetInvalid);
        capacity.maximumQueuedMessages = static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()) + 1ULL;
        RequireError(TransportBudgetController::Create(capacity, Policy()), NetworkErrors::TransportBudgetInvalid);
    }

    TEST_CASE("Transport budget owns exact connection generations and bounded connection capacity", "[unit][network][budget]") {
        auto controller = Controller();
        REQUIRE(controller.OpenConnection(Connection(0)).HasValue());
        RequireError(controller.OpenConnection(Connection(0)), NetworkErrors::TransportBudgetInvalid);
        RequireError(controller.OpenConnection(Connection(0, 2)), NetworkErrors::NetworkLifecycleOperationStale);
        REQUIRE(controller.OpenConnection(Connection(1)).HasValue());
        RequireError(controller.OpenConnection(ConnectionHandle::Create(2, 1).Value()), NetworkErrors::TransportHandleInvalid);

        REQUIRE(controller.CloseConnection(Connection(0)).Value() == 0);
        REQUIRE(controller.CloseConnection(Connection(0)).Value() == 0);
        RequireError(controller.OpenConnection(Connection(0, 3)), NetworkErrors::NetworkLifecycleOperationStale);
        REQUIRE(controller.OpenConnection(Connection(0, 2)).HasValue());
    }

    TEST_CASE("Reliable traffic is never silently discarded and tickets release exact queued generations", "[unit][network][budget]") {
        auto controller = Controller();
        const auto connection = Connection();
        REQUIRE(controller.OpenConnection(connection).HasValue());
        REQUIRE(controller.BeginTick(1).HasValue());

        const auto first = controller.Admit(Reliable(connection, 4));
        const auto second = controller.Admit(Reliable(connection, 4));
        REQUIRE(first.Value().admission == TransportBudgetAdmission::Enqueued);
        REQUIRE(second.Value().admission == TransportBudgetAdmission::Enqueued);
        REQUIRE(first.Value().ticket != second.Value().ticket);
        RequireError(controller.Admit(Reliable(connection, 1)), NetworkErrors::TransportReliableBackpressure);
        REQUIRE(controller.Snapshot().queuedMessages == 2);
        REQUIRE(controller.Snapshot().queuedBytes == 8);

        REQUIRE(controller.Complete(first.Value().ticket).HasValue());
        RequireError(controller.Complete(first.Value().ticket), NetworkErrors::TransportBudgetTicketStale);
        RequireError(controller.Admit(Reliable(connection, 1)), NetworkErrors::TransportReliableBackpressure);
        REQUIRE(controller.BeginTick(2).HasValue());
        const auto reused = controller.Admit(Reliable(connection, 1));
        REQUIRE(reused.HasValue());
        REQUIRE(reused.Value().ticket.Slot() == first.Value().ticket.Slot());
        REQUIRE(reused.Value().ticket.Generation() == first.Value().ticket.Generation() + 1);
    }

    TEST_CASE("Replaceable state coalesces in place and overload remains bounded", "[unit][network][budget]") {
        auto controller = Controller();
        const auto connection = Connection();
        REQUIRE(controller.OpenConnection(connection).HasValue());
        REQUIRE(controller.BeginTick(1).HasValue());

        const auto first = controller.Admit(Replaceable(connection, 91, 3));
        const auto replacement = controller.Admit(Replaceable(connection, 91, 5));
        REQUIRE(first.Value().admission == TransportBudgetAdmission::Enqueued);
        REQUIRE(replacement.Value().admission == TransportBudgetAdmission::Replaced);
        REQUIRE(replacement.Value().ticket == first.Value().ticket);
        REQUIRE(replacement.Value().replacedBytes == 3);
        REQUIRE(controller.Snapshot().queuedMessages == 1);
        REQUIRE(controller.Snapshot().queuedBytes == 5);

        const auto dropped = controller.Admit(Replaceable(connection, 92, 1));
        REQUIRE(dropped.Value().admission == TransportBudgetAdmission::DroppedReplaceable);
        REQUIRE_FALSE(dropped.Value().ticket.IsValid());
        REQUIRE(controller.Snapshot().queuedMessages == 1);
    }

    TEST_CASE("Distinct saturated ticks escalate one abusive connection without log-shaped work", "[unit][network][budget]") {
        auto controller = Controller();
        const auto connection = Connection();
        REQUIRE(controller.OpenConnection(connection).HasValue());
        REQUIRE(controller.BeginTick(1).HasValue());
        REQUIRE(controller.Admit(Reliable(connection, 4)).HasValue());
        REQUIRE(controller.Admit(Reliable(connection, 4)).HasValue());
        REQUIRE(controller.Admit(Replaceable(connection, 1, 1)).Value().admission == TransportBudgetAdmission::DroppedReplaceable);
        REQUIRE(controller.Admit(Replaceable(connection, 2, 1)).Value().admission == TransportBudgetAdmission::DroppedReplaceable);

        REQUIRE(controller.BeginTick(2).HasValue());
        REQUIRE(controller.Admit(Replaceable(connection, 2, 1)).Value().admission == TransportBudgetAdmission::ConnectionMustClose);
        REQUIRE(controller.Snapshot().queuedMessages == 2);
    }

    TEST_CASE("Policy replacement is atomic and preserves last good policy on every failure", "[unit][network][budget]") {
        auto controller = Controller();
        const auto connection = Connection();
        REQUIRE(controller.OpenConnection(connection).HasValue());
        REQUIRE(controller.BeginTick(1).HasValue());
        REQUIRE(controller.Admit(Reliable(connection, 4)).HasValue());

        auto candidate = Policy(2);
        candidate.maximumQueuedBytesPerConnection = 3;
        RequireError(controller.ReplacePolicy(1, candidate), NetworkErrors::TransportBudgetCapacityExceeded);
        REQUIRE(controller.Policy().revision == 1);
        RequireError(controller.ReplacePolicy(9, Policy(2)), NetworkErrors::TransportBudgetPolicyStale);
        REQUIRE(controller.Policy().revision == 1);
        RequireError(controller.ReplacePolicy(1, Policy(2), TransportAdmissionState::Cancelled),
                     NetworkErrors::TransportOperationCancelled);
        REQUIRE(controller.Policy().revision == 1);
        REQUIRE(controller.ReplacePolicy(1, Policy(2)).HasValue());
        REQUIRE(controller.Policy().revision == 2);
    }

    TEST_CASE("Malformed traffic tick cancellation close and shutdown preserve bounded accounting", "[unit][network][budget]") {
        auto controller = Controller();
        const auto connection = Connection();
        REQUIRE(controller.OpenConnection(connection).HasValue());
        RequireError(controller.BeginTick(0), NetworkErrors::TransportBudgetInvalid);
        REQUIRE(controller.BeginTick(1).HasValue());
        RequireError(controller.BeginTick(1), NetworkErrors::TransportBudgetInvalid);
        RequireError(controller.Admit({connection, TransportTrafficClass::ReplaceableState, 0, 1}), NetworkErrors::TransportBudgetInvalid);
        RequireError(controller.Admit({connection, TransportTrafficClass::Reliable, 7, 1}), NetworkErrors::TransportBudgetInvalid);
        RequireError(controller.Admit(Reliable(connection, 0)), NetworkErrors::TransportBudgetInvalid);
        RequireError(controller.Admit(Reliable(connection, 1), TransportAdmissionState::Cancelled),
                     NetworkErrors::TransportOperationCancelled);
        REQUIRE(controller.Admit(Reliable(connection, 4)).HasValue());
        REQUIRE(controller.CloseConnection(connection).Value() == 1);
        REQUIRE(controller.Snapshot().queuedMessages == 0);

        REQUIRE(controller.OpenConnection(Connection(0, 2)).HasValue());
        REQUIRE(controller.BeginTick(2).HasValue());
        REQUIRE(controller.Admit(Reliable(Connection(0, 2), 4)).HasValue());
        REQUIRE(controller.Shutdown() == 1);
        REQUIRE(controller.Shutdown() == 0);
        REQUIRE(controller.Snapshot().activeConnections == 0);
        REQUIRE(controller.Snapshot().queuedMessages == 0);
        RequireError(controller.OpenConnection(Connection(1)), NetworkErrors::TransportShuttingDown);
        RequireError(controller.BeginTick(3), NetworkErrors::TransportShuttingDown);
        RequireError(controller.Admit(Reliable(Connection(0, 2), 1)), NetworkErrors::TransportShuttingDown);
    }
}  // namespace Horo::Network
