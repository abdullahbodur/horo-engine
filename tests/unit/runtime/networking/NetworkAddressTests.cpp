#include "Horo/Network/NetworkAddress.h"
#include "Horo/Network/NetworkErrors.h"
#include "NetworkTestUtils.h"

#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <string>
#include <string_view>

namespace Horo::Network {
    using TestSupport::RequireError;

    TEST_CASE("Network address owns canonical network-order numeric identity", "[unit][network][address]") {
        const auto ipv4 = NetworkAddress::Parse("127.0.0.1:7777");
        REQUIRE(ipv4.HasValue());
        REQUIRE(ipv4.Value().Kind() == NetworkAddressKind::Ipv4);
        constexpr std::array<std::uint8_t, 4> ExpectedIpv4{127, 0, 0, 1};
        REQUIRE(std::ranges::equal(ipv4.Value().AddressBytes(), ExpectedIpv4));
        REQUIRE(ipv4.Value().Port() == 7777);
        REQUIRE_FALSE(ipv4.Value().RequiresResolution());
        REQUIRE(ipv4.Value().Diagnostic().View() == "127.0.0.1:7777");

        const auto ipv6 = NetworkAddress::Parse("[2001:db8::1]:65535");
        REQUIRE(ipv6.HasValue());
        constexpr std::array<std::uint8_t, 16> ExpectedIpv6{0x20, 0x01, 0x0d, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
        REQUIRE(std::ranges::equal(ipv6.Value().AddressBytes(), ExpectedIpv6));
        REQUIRE(ipv6.Value().Diagnostic().View() == "[2001:0db8:0000:0000:0000:0000:0000:0001]:65535");
    }

    TEST_CASE("Network address canonicalizes bounded ASCII DNS without using display text as identity", "[unit][network][address]") {
        const auto uppercase = NetworkAddress::Parse("Game.Example.COM:443");
        const auto lowercase = NetworkAddress::Parse("game.example.com:443");
        REQUIRE(uppercase.HasValue());
        REQUIRE(lowercase.HasValue());
        REQUIRE(uppercase.Value() == lowercase.Value());
        REQUIRE(uppercase.Value().RequiresResolution());
        REQUIRE(uppercase.Value().Hostname() == "game.example.com");
        REQUIRE(uppercase.Value().AddressBytes().empty());
        REQUIRE(uppercase.Value().Diagnostic().View() == "game.example.com:443");

        std::string maximumHostname(63, 'a');
        maximumHostname += '.';
        maximumHostname.append(63, 'b');
        maximumHostname += '.';
        maximumHostname.append(63, 'c');
        maximumHostname += '.';
        maximumHostname.append(61, 'd');
        REQUIRE(maximumHostname.size() == NetworkAddress::MaximumHostnameLength);
        REQUIRE(NetworkAddress::Parse(maximumHostname + ":65535").HasValue());
    }

    TEST_CASE("Network address rejects malformed ambiguous oversized and unsupported forms", "[unit][network][address]") {
        for (const std::string_view malformed :
             {"", "localhost", ":80", "localhost:", "localhost:0", "localhost:080", "localhost:65536", "127.0.0.1.2:80", "127.00.0.1:80",
              "256.0.0.1:80", "2001:db8::1:80", "[2001::db8::1]:80", "[localhost]:80", ".host:80", "host.:80", "host..name:80", "-host:80",
              "host-:80", "ho_st:80"})
            RequireError(NetworkAddress::Parse(malformed), NetworkErrors::NetworkAddressInvalid);

        RequireError(NetworkAddress::Parse("udp://localhost:80"), NetworkErrors::NetworkAddressUnsupported);
        RequireError(NetworkAddress::Parse("[fe80::1%en0]:80"), NetworkErrors::NetworkAddressUnsupported);
        RequireError(NetworkAddress::Parse("xn--bcher-kva.example:80"), NetworkErrors::NetworkAddressUnsupported);
        RequireError(NetworkAddress::Parse("b\xC3\xBC"
                                           "cher.example:80"),
                     NetworkErrors::NetworkAddressUnsupported);

        std::string oversizedHostname(254, 'a');
        RequireError(NetworkAddress::Parse(oversizedHostname + ":80"), NetworkErrors::NetworkAddressCapacityExceeded);
    }

    TEST_CASE("Network address cancellation and shutdown reject before parsing or backend work", "[unit][network][address]") {
        RequireError(NetworkAddress::Parse("127.0.0.1:7777", TransportAdmissionState::Cancelled),
                     NetworkErrors::TransportOperationCancelled);
        RequireError(NetworkAddress::Parse("127.0.0.1:7777", TransportAdmissionState::ShuttingDown), NetworkErrors::TransportShuttingDown);
        RequireError(NetworkAddress::Parse("127.0.0.1:7777", TransportAdmissionState::Count), NetworkErrors::NetworkAddressInvalid);
    }
}  // namespace Horo::Network
