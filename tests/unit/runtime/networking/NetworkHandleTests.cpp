#include "Horo/Network/NetworkErrors.h"
#include "Horo/Network/NetworkHandles.h"
#include "NetworkTestUtils.h"

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <limits>
#include <type_traits>

namespace Horo::Network {
    using TestSupport::RequireError;

    static_assert(!std::is_convertible_v<ConnectionHandle, ListenerHandle>);
    static_assert(!std::is_convertible_v<ConnectionHandle, PeerHandle>);
    static_assert(!std::is_convertible_v<ListenerHandle, PeerHandle>);

    TEST_CASE("Transport handles reject malformed stale and recycled generations", "[unit][network][handle]") {
        RequireError(ConnectionHandle::Create(Horo::Handle<ConnectionHandleTag>::InvalidIndex, 1), NetworkErrors::TransportHandleInvalid);
        RequireError(ConnectionHandle::Create(7, 0), NetworkErrors::TransportHandleInvalid);

        const auto first = ConnectionHandle::Create(7, 1).Value();
        const auto recycled = first.NextGeneration();
        REQUIRE(recycled.HasValue());
        REQUIRE(recycled.Value().Slot() == first.Slot());
        REQUIRE(recycled.Value().Generation() == 2);
        REQUIRE(recycled.Value() != first);
        RequireError(ValidateTransportHandle(first, recycled.Value()), NetworkErrors::TransportHandleInvalid);
        REQUIRE(ValidateTransportHandle(recycled.Value(), recycled.Value()).HasValue());
        REQUIRE(recycled.Value().Diagnostic() == TransportHandleDiagnostic{7, 2});
    }

    TEST_CASE("Transport handle generation never wraps and terminal admission is explicit", "[unit][network][handle]") {
        const auto exhausted = ConnectionHandle::Create(3, std::numeric_limits<std::uint32_t>::max()).Value();
        RequireError(exhausted.NextGeneration(), NetworkErrors::TransportGenerationExhausted);

        const auto listener = ListenerHandle::Create(4, 9).Value();
        RequireError(ValidateTransportHandle(listener, listener, TransportAdmissionState::Cancelled),
                     NetworkErrors::TransportOperationCancelled);
        RequireError(ValidateTransportHandle(listener, listener, TransportAdmissionState::ShuttingDown),
                     NetworkErrors::TransportShuttingDown);
        RequireError(ValidateTransportHandle(listener, listener, TransportAdmissionState::Count), NetworkErrors::TransportHandleInvalid);
    }

    TEST_CASE("Peer handles are transport correlation only and channel identity obeys negotiated bounds", "[unit][network][handle]") {
        const auto peer = PeerHandle::Create(0, 1);
        REQUIRE(peer.HasValue());
        REQUIRE(peer.Value().Diagnostic() == TransportHandleDiagnostic{0, 1});

        REQUIRE(ChannelId::Create(0, 1).Value() == ChannelId{});
        REQUIRE(ChannelId::Create(3, 4).Value().Value() == 3);
        REQUIRE(ChannelId::Create(3, 4).Value().IsWithin(4));
        REQUIRE_FALSE(ChannelId::Create(4, 5).Value().IsWithin(4));
        RequireError(ChannelId::Create(0, 0), NetworkErrors::TransportLimitExceeded);
        RequireError(ChannelId::Create(4, 4), NetworkErrors::TransportLimitExceeded);
    }
}  // namespace Horo::Network
