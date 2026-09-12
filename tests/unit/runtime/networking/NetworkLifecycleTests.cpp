#include "Horo/Network/NetworkErrors.h"
#include "Horo/Network/NetworkLifecycle.h"
#include "NetworkTestUtils.h"

#include <catch2/catch_test_macros.hpp>
#include <limits>
#include <utility>

namespace Horo::Network {
    using TestSupport::RequireError;

    namespace {
        ListenerHandle Listener(const std::uint32_t slot = 1, const std::uint32_t generation = 1) {
            return ListenerHandle::Create(slot, generation).Value();
        }

        ConnectionHandle Connection(const std::uint32_t slot = 1, const std::uint32_t generation = 1) {
            return ConnectionHandle::Create(slot, generation).Value();
        }

        NetworkOperationGeneration Operation(const std::uint64_t generation = 1) {
            return NetworkOperationGeneration::Create(generation).Value();
        }

        NetworkLifecycleTerminal Closed() {
            return NetworkLifecycleTerminal::Create(NetworkLifecycleTerminalKind::Closed).Value();
        }

        NetworkLifecycleTerminal Failed() {
            auto failure = MakeNetworkTerminalRecord(NetworkFailureLayer::Transport, NetworkFailureKind::TransportUnavailable);
            return NetworkLifecycleTerminal::Create(NetworkLifecycleTerminalKind::Failed, std::move(failure).Value()).Value();
        }

        NetworkLifecycleRegistry Registry(const NetworkLifecycleLimits limits = {2, 3}) {
            return NetworkLifecycleRegistry::Create(limits).Value();
        }
    }  // namespace

    TEST_CASE("Network listener lifecycle closes idempotently and retains first terminal result", "[unit][network][lifecycle]") {
        auto registry = Registry();
        REQUIRE(registry.AdmitListener(Listener(), Operation()).HasValue());
        REQUIRE(registry.MarkListening(Listener(), Operation()).HasValue());
        REQUIRE(registry.RequestListenerClose(Listener(), Operation()).HasValue());
        REQUIRE(registry.RequestListenerClose(Listener(), Operation()).HasValue());
        REQUIRE(registry.CompleteListener(Listener(), Operation(), Closed()).HasValue());
        REQUIRE(registry.RequestListenerClose(Listener(), Operation()).HasValue());
        RequireError(registry.CompleteListener(Listener(), Operation(), Failed()), NetworkErrors::TerminalAlreadyResolved);

        const auto snapshot = registry.Listener(Listener());
        REQUIRE(snapshot.HasValue());
        REQUIRE(snapshot.Value().state == NetworkListenerState::Closed);
        REQUIRE(snapshot.Value().terminal.has_value());
        REQUIRE(snapshot.Value().terminal->Kind() == NetworkLifecycleTerminalKind::Closed);
        REQUIRE(snapshot.Value().terminal->Failure() == nullptr);
    }

    TEST_CASE("Connection lifecycle enforces resolve connect authenticate and close order", "[unit][network][lifecycle]") {
        auto registry = Registry();
        REQUIRE(registry.AdmitConnection(Connection(), Operation(), true, 50).HasValue());
        RequireError(registry.AdvanceConnection(Connection(), Operation(), NetworkConnectionState::AuthenticationReady),
                     NetworkErrors::NetworkLifecycleTransitionInvalid);
        REQUIRE(registry.AdvanceConnection(Connection(), Operation(), NetworkConnectionState::Connecting).HasValue());
        REQUIRE(registry.AdvanceConnection(Connection(), Operation(), NetworkConnectionState::AuthenticationReady).HasValue());
        REQUIRE(registry.ExpireConnections(500).Value() == 0);
        REQUIRE(registry.RequestConnectionClose(Connection(), Operation()).HasValue());
        REQUIRE(registry.RequestConnectionClose(Connection(), Operation()).HasValue());
        REQUIRE(registry.CompleteConnection(Connection(), Operation(), Closed()).HasValue());

        const auto snapshot = registry.Connection(Connection());
        REQUIRE(snapshot.Value().state == NetworkConnectionState::Closed);
        REQUIRE(snapshot.Value().deadlineTick == 0);
        REQUIRE(snapshot.Value().terminal->Kind() == NetworkLifecycleTerminalKind::Closed);
    }

    TEST_CASE("Cancellation wins exactly once and replacement fences stale completions", "[unit][network][lifecycle]") {
        auto registry = Registry();
        const ConnectionHandle first = Connection(3, 7);
        const ConnectionHandle replacement = first.NextGeneration().Value();
        REQUIRE(registry.AdmitConnection(first, Operation(4), false, 20).HasValue());
        REQUIRE(registry.CancelConnection(first, Operation(4)).HasValue());
        RequireError(registry.CompleteConnection(first, Operation(4), Failed()), NetworkErrors::TerminalAlreadyResolved);
        REQUIRE(registry.Connection(first).Value().terminal->Failure()->Kind() == NetworkFailureKind::SessionCancelled);

        REQUIRE(registry.AdmitConnection(replacement, Operation(5), false, 40).HasValue());
        RequireError(registry.AdvanceConnection(first, Operation(4), NetworkConnectionState::Connecting),
                     NetworkErrors::NetworkLifecycleOperationStale);
        RequireError(registry.AdvanceConnection(replacement, Operation(4), NetworkConnectionState::Connecting),
                     NetworkErrors::NetworkLifecycleOperationStale);
        REQUIRE(registry.AdvanceConnection(replacement, Operation(5), NetworkConnectionState::Connecting).HasValue());
        RequireError(registry.Connection(first), NetworkErrors::NetworkLifecycleOperationStale);
    }

    TEST_CASE("Connection deadline scan is bounded deterministic and terminalizes each operation once", "[unit][network][lifecycle]") {
        auto registry = Registry();
        REQUIRE(registry.AdmitConnection(Connection(1), Operation(1), false, 10).HasValue());
        REQUIRE(registry.AdmitConnection(Connection(2), Operation(2), true, 20).HasValue());
        REQUIRE(registry.ExpireConnections(9).Value() == 0);
        REQUIRE(registry.ExpireConnections(10).Value() == 1);
        REQUIRE(registry.ExpireConnections(20).Value() == 1);
        REQUIRE(registry.ExpireConnections(100).Value() == 0);
        REQUIRE(registry.Connection(Connection(1)).Value().state == NetworkConnectionState::TimedOut);
        REQUIRE(registry.Connection(Connection(2)).Value().terminal->Failure()->Kind() == NetworkFailureKind::SessionTimedOut);
    }

    TEST_CASE("Lifecycle validation rejects malformed terminals stale generations and exhausted capacity", "[unit][network][lifecycle]") {
        RequireError(NetworkOperationGeneration::Create(0), NetworkErrors::NetworkLifecycleInvalid);
        RequireError(NetworkLifecycleRegistry::Create({}), NetworkErrors::NetworkLifecycleInvalid);
        RequireError(NetworkLifecycleRegistry::Create({MaximumNetworkLifecycleListeners + 1, 1}), NetworkErrors::NetworkLifecycleInvalid);
        auto failure = MakeNetworkTerminalRecord(NetworkFailureLayer::Session, NetworkFailureKind::SessionTimedOut).Value();
        RequireError(NetworkLifecycleTerminal::Create(NetworkLifecycleTerminalKind::Closed, failure),
                     NetworkErrors::NetworkLifecycleInvalid);
        RequireError(NetworkLifecycleTerminal::Create(NetworkLifecycleTerminalKind::Cancelled, failure),
                     NetworkErrors::NetworkLifecycleInvalid);

        auto registry = Registry({1, 1});
        REQUIRE(registry.AdmitListener(Listener(1), Operation(1)).HasValue());
        RequireError(registry.AdmitListener(Listener(2), Operation(2)), NetworkErrors::NetworkLifecycleCapacityExceeded);
        REQUIRE(registry.AdmitConnection(Connection(1), Operation(1), false, 10).HasValue());
        RequireError(registry.AdmitConnection(Connection(2), Operation(2), false, 10), NetworkErrors::NetworkLifecycleCapacityExceeded);
        RequireError(registry.AdmitConnection(Connection(1, 3), Operation(3), false, 10), NetworkErrors::TerminalGenerationStale);
    }

    TEST_CASE("Lifecycle shutdown is idempotent and preserves already published terminal evidence", "[unit][network][lifecycle]") {
        auto registry = Registry();
        REQUIRE(registry.AdmitListener(Listener(), Operation()).HasValue());
        REQUIRE(registry.AdmitConnection(Connection(), Operation(), false, 10).HasValue());
        REQUIRE(registry.CancelConnection(Connection(), Operation()).HasValue());
        REQUIRE(registry.Shutdown().Value() == 1);
        REQUIRE(registry.Shutdown().Value() == 0);
        REQUIRE(registry.IsShuttingDown());
        REQUIRE(registry.Listener(Listener()).Value().state == NetworkListenerState::ShuttingDown);
        REQUIRE(registry.Connection(Connection()).Value().state == NetworkConnectionState::Cancelled);
        RequireError(registry.AdmitConnection(Connection(2), Operation(2), false, 20), NetworkErrors::TransportShuttingDown);
        RequireError(registry.ExpireConnections(20), NetworkErrors::TransportShuttingDown);
    }
}  // namespace Horo::Network
