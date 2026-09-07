#pragma once

/**
 * @file NetworkAddress.h
 * @brief Canonical bounded IPv4, IPv6, and DNS transport endpoint values.
 */

#include "Horo/Foundation/Result.h"
#include "Horo/Network/NetworkHandles.h"

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace Horo::Network {
    /** @brief Closed canonical address representation; no native socket type is exposed. */
    enum class NetworkAddressKind : std::uint8_t {
        Ipv4,
        Ipv6,
        DnsHostname,
        Count
    };

    /** @brief Owned bounded text intended only for diagnostics, never endpoint identity or reparsing. */
    struct NetworkAddressDiagnostic final {
        static constexpr std::size_t MaximumLength = 263;
        std::array<char, MaximumLength> characters{};
        std::uint16_t length{};

        /** @brief Views the initialized diagnostic bytes. @return Borrowed text valid while this value lives. */
        [[nodiscard]] constexpr std::string_view View() const noexcept {
            return {characters.data(), length};
        }

        constexpr auto operator<=>(const NetworkAddressDiagnostic &) const noexcept = default;
    };

    /**
     * @brief Canonical backend-neutral transport endpoint with owned bounded storage.
     *
     * IPv4 and IPv6 identities are stored as bytes in network byte order. DNS hostnames are stored as lowercase
     * ASCII without a trailing dot. Unicode/IDNA, zone identifiers, URI schemes, service names, unbracketed IPv6,
     * and non-canonical numeric spellings fail closed. Display or diagnostic text is never authoritative identity.
     */
    class NetworkAddress final {
    public:
        static constexpr std::size_t Ipv4ByteCount = 4;
        static constexpr std::size_t Ipv6ByteCount = 16;
        static constexpr std::size_t MaximumHostnameLength = 253;

        /** @brief Constructs the reserved invalid endpoint. */
        constexpr NetworkAddress() = default;

        /**
         * @brief Parses and canonicalizes one strict host-and-port endpoint before backend admission.
         * @param text `a.b.c.d:port`, `[ipv6]:port`, or `dns-name:port` in ASCII.
         * @param state Explicit caller-owned cancellation or shutdown state.
         * @return Owned canonical endpoint or a typed network error.
         */
        [[nodiscard]] static Result<NetworkAddress> Parse(std::string_view text,
                                                          TransportAdmissionState state = TransportAdmissionState::Accepting);

        /** @brief Checks representation only. @return Whether kind, port, and owned address data are valid. */
        [[nodiscard]] bool IsValid() const noexcept;

        /** @brief Returns the canonical address category. @return Count only for an invalid endpoint. */
        [[nodiscard]] constexpr NetworkAddressKind Kind() const noexcept {
            return kind_;
        }

        /** @brief Returns the exact transport port in host integer form. @return Zero only for an invalid endpoint. */
        [[nodiscard]] constexpr std::uint16_t Port() const noexcept {
            return port_;
        }

        /** @brief Reports whether asynchronous DNS is required. @return True only for DnsHostname. */
        [[nodiscard]] constexpr bool RequiresResolution() const noexcept {
            return kind_ == NetworkAddressKind::DnsHostname;
        }

        /**
         * @brief Views canonical numeric bytes in network byte order.
         * @return Four bytes for IPv4, sixteen for IPv6, or an empty span for DNS/invalid endpoints.
         */
        [[nodiscard]] std::span<const std::uint8_t> AddressBytes() const noexcept;

        /** @brief Views the owned lowercase DNS name. @return Empty for numeric/invalid endpoints. */
        [[nodiscard]] std::string_view Hostname() const noexcept;

        /** @brief Formats a bounded safe diagnostic projection. @return Owned text not used for identity or parsing. */
        [[nodiscard]] NetworkAddressDiagnostic Diagnostic() const noexcept;

        constexpr auto operator<=>(const NetworkAddress &) const noexcept = default;

    private:
        /** @brief Constructs one already validated canonical endpoint representation. */
        NetworkAddress(NetworkAddressKind kind, std::uint16_t port, const std::array<std::uint8_t, Ipv6ByteCount> &addressBytes,
                       const std::array<char, MaximumHostnameLength> &hostname, std::uint16_t hostnameLength) noexcept;

        NetworkAddressKind kind_{NetworkAddressKind::Count};
        std::uint16_t port_{};
        std::array<std::uint8_t, Ipv6ByteCount> addressBytes_{};
        std::array<char, MaximumHostnameLength> hostname_{};
        std::uint16_t hostnameLength_{};
    };
}  // namespace Horo::Network
