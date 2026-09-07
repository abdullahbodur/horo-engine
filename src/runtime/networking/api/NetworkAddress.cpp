#include "Horo/Network/NetworkAddress.h"

#include "Horo/Network/NetworkErrors.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <limits>
#include <string_view>

namespace Horo::Network {
    namespace {
        template <typename T> Result<T> Fail(const ErrorCodeDescriptor &code) {
            return Result<T>::Failure(MakeError(code));
        }

        constexpr bool IsAsciiDigit(const unsigned char character) noexcept {
            return character >= '0' && character <= '9';
        }

        constexpr bool IsAsciiLetter(const unsigned char character) noexcept {
            return (character >= 'A' && character <= 'Z') || (character >= 'a' && character <= 'z');
        }

        constexpr bool IsAsciiWhitespace(const unsigned char character) noexcept {
            return character == ' ' || (character >= '\t' && character <= '\r');
        }

        constexpr char AsciiLower(const unsigned char character) noexcept {
            return character >= 'A' && character <= 'Z' ? static_cast<char>(character + ('a' - 'A')) : static_cast<char>(character);
        }

        const ErrorCodeDescriptor *ValidateAddressInput(const std::string_view text, const TransportAdmissionState state) noexcept {
            if (state == TransportAdmissionState::Cancelled)
                return &NetworkErrors::TransportOperationCancelled;
            if (state == TransportAdmissionState::ShuttingDown)
                return &NetworkErrors::TransportShuttingDown;
            if (state != TransportAdmissionState::Accepting || text.empty())
                return &NetworkErrors::NetworkAddressInvalid;
            if (text.size() > NetworkAddressDiagnostic::MaximumLength)
                return &NetworkErrors::NetworkAddressCapacityExceeded;
            if (text.find("://") != std::string_view::npos || text.find('%') != std::string_view::npos ||
                std::ranges::any_of(text, [](const unsigned char character) {
                return character > 0x7fU || IsAsciiWhitespace(character);
            }))
                return &NetworkErrors::NetworkAddressUnsupported;
            return nullptr;
        }

        Result<std::uint16_t> ParsePort(const std::string_view text) {
            if (text.empty() || (text.size() > 1 && text.front() == '0'))
                return Fail<std::uint16_t>(NetworkErrors::NetworkAddressInvalid);
            std::uint32_t value{};
            const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
            if (error != std::errc{} || end != text.data() + text.size() || value == 0 || value > std::numeric_limits<std::uint16_t>::max())
                return Fail<std::uint16_t>(NetworkErrors::NetworkAddressInvalid);
            return Result<std::uint16_t>::Success(static_cast<std::uint16_t>(value));
        }

        bool IsDecimalOrDot(const std::string_view text) noexcept {
            return std::ranges::all_of(text, [](const unsigned char character) {
                return IsAsciiDigit(character) || character == '.';
            });
        }

        bool ParseIpv4(const std::string_view text, std::array<std::uint8_t, NetworkAddress::Ipv6ByteCount> &bytes) noexcept {
            std::size_t start{};
            for (std::size_t octet = 0; octet < NetworkAddress::Ipv4ByteCount; ++octet) {
                const auto separator = text.find('.', start);
                const auto end = separator == std::string_view::npos ? text.size() : separator;
                const auto field = text.substr(start, end - start);
                if (field.empty() || field.size() > 3 || (field.size() > 1 && field.front() == '0'))
                    return false;
                std::uint32_t value{};
                const auto [parsedEnd, error] = std::from_chars(field.data(), field.data() + field.size(), value);
                if (error != std::errc{} || parsedEnd != field.data() + field.size() || value > 255)
                    return false;
                bytes[octet] = static_cast<std::uint8_t>(value);
                if (octet + 1 < NetworkAddress::Ipv4ByteCount) {
                    if (separator == std::string_view::npos)
                        return false;
                    start = separator + 1;
                } else if (separator != std::string_view::npos) {
                    return false;
                }
            }
            return true;
        }

        bool ParseHexGroup(const std::string_view text, std::uint16_t &value) noexcept {
            if (text.empty() || text.size() > 4)
                return false;
            std::uint32_t parsed{};
            const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), parsed, 16);
            if (error != std::errc{} || end != text.data() + text.size())
                return false;
            value = static_cast<std::uint16_t>(parsed);
            return true;
        }

        bool ParseIpv6Side(const std::string_view text, std::array<std::uint16_t, 8> &groups, std::size_t &count) noexcept {
            if (text.empty())
                return true;
            std::size_t start{};
            while (start <= text.size()) {
                if (count == groups.size())
                    return false;
                const auto separator = text.find(':', start);
                const auto end = separator == std::string_view::npos ? text.size() : separator;
                if (!ParseHexGroup(text.substr(start, end - start), groups[count++]))
                    return false;
                if (separator == std::string_view::npos)
                    return true;
                start = separator + 1;
            }
            return false;
        }

        bool ParseIpv6(const std::string_view text, std::array<std::uint8_t, NetworkAddress::Ipv6ByteCount> &bytes) noexcept {
            if (text.empty() || text.find('.') != std::string_view::npos || text.find('%') != std::string_view::npos)
                return false;
            const auto compression = text.find("::");
            if (compression != std::string_view::npos && text.find("::", compression + 2) != std::string_view::npos)
                return false;

            std::array<std::uint16_t, 8> left{};
            std::array<std::uint16_t, 8> right{};
            std::size_t leftCount{};
            std::size_t rightCount{};
            if (compression == std::string_view::npos) {
                if (!ParseIpv6Side(text, left, leftCount) || leftCount != left.size())
                    return false;
            } else {
                if (!ParseIpv6Side(text.substr(0, compression), left, leftCount) ||
                    !ParseIpv6Side(text.substr(compression + 2), right, rightCount) || leftCount + rightCount >= left.size())
                    return false;
            }

            std::array<std::uint16_t, 8> groups{};
            std::copy_n(left.begin(), leftCount, groups.begin());
            std::copy_n(right.begin(), rightCount, groups.end() - static_cast<std::ptrdiff_t>(rightCount));
            for (std::size_t index = 0; index < groups.size(); ++index) {
                bytes[index * 2] = static_cast<std::uint8_t>(groups[index] >> 8U);
                bytes[index * 2 + 1] = static_cast<std::uint8_t>(groups[index] & 0xffU);
            }
            return true;
        }

        enum class HostnameStatus : std::uint8_t {
            Valid,
            Invalid,
            Unsupported,
            CapacityExceeded
        };

        HostnameStatus CanonicalizeHostname(const std::string_view text,
                                            std::array<char, NetworkAddress::MaximumHostnameLength> &hostname) noexcept {
            if (text.size() > NetworkAddress::MaximumHostnameLength)
                return HostnameStatus::CapacityExceeded;
            if (text.empty() || text.front() == '.' || text.back() == '.')
                return HostnameStatus::Invalid;
            std::size_t labelStart{};
            for (std::size_t index = 0; index <= text.size(); ++index) {
                if (index != text.size() && text[index] != '.') {
                    const auto character = static_cast<unsigned char>(text[index]);
                    if (character > 0x7fU)
                        return HostnameStatus::Unsupported;
                    if (!IsAsciiLetter(character) && !IsAsciiDigit(character) && character != '-')
                        return HostnameStatus::Invalid;
                    hostname[index] = AsciiLower(character);
                    continue;
                }
                const auto labelLength = index - labelStart;
                if (labelLength == 0 || labelLength > 63 || hostname[labelStart] == '-' || hostname[index - 1] == '-')
                    return HostnameStatus::Invalid;
                if (labelLength >= 4 && std::string_view{hostname.data() + labelStart, 4} == "xn--")
                    return HostnameStatus::Unsupported;
                if (index != text.size())
                    hostname[index] = '.';
                labelStart = index + 1;
            }
            return HostnameStatus::Valid;
        }

        class DiagnosticWriter final {
        public:
            explicit DiagnosticWriter(NetworkAddressDiagnostic &output) noexcept : output_(output) {}

            void Character(const char character) noexcept {
                output_.characters[output_.length++] = character;
            }

            void Text(const std::string_view text) noexcept {
                std::copy(text.begin(), text.end(), output_.characters.begin() + output_.length);
                output_.length = static_cast<std::uint16_t>(output_.length + text.size());
            }

            void Decimal(const std::uint32_t value) noexcept {
                auto *begin = output_.characters.data() + output_.length;
                const auto [end, error] = std::to_chars(begin, output_.characters.data() + output_.characters.size(), value);
                if (error == std::errc{})
                    output_.length = static_cast<std::uint16_t>(end - output_.characters.data());
            }

            void HexGroup(const std::uint16_t value) noexcept {
                static constexpr std::string_view Digits = "0123456789abcdef";
                for (int shift = 12; shift >= 0; shift -= 4)
                    Character(Digits[(value >> static_cast<unsigned int>(shift)) & 0xfU]);
            }

        private:
            NetworkAddressDiagnostic &output_;
        };
    }  // namespace

    /** @copydoc NetworkAddress::NetworkAddress */
    NetworkAddress::NetworkAddress(const NetworkAddressKind kind, const std::uint16_t port,
                                   std::array<std::uint8_t, Ipv6ByteCount> addressBytes, std::array<char, MaximumHostnameLength> hostname,
                                   const std::uint16_t hostnameLength) noexcept
        : kind_(kind), port_(port), addressBytes_(addressBytes), hostname_(hostname), hostnameLength_(hostnameLength) {}

    /** @copydoc NetworkAddress::Parse */
    Result<NetworkAddress> NetworkAddress::Parse(const std::string_view text, const TransportAdmissionState state) {
        if (const auto *error = ValidateAddressInput(text, state); error != nullptr)
            return Fail<NetworkAddress>(*error);

        NetworkAddressKind kind{NetworkAddressKind::Count};
        std::array<std::uint8_t, Ipv6ByteCount> addressBytes{};
        std::array<char, MaximumHostnameLength> hostname{};
        std::uint16_t hostnameLength{};
        std::string_view host;
        std::string_view portText;
        if (text.front() == '[') {
            const auto closing = text.find(']');
            if (closing == std::string_view::npos || closing + 1 >= text.size() || text[closing + 1] != ':' ||
                text.find('[', 1) != std::string_view::npos || text.find(']', closing + 1) != std::string_view::npos)
                return Fail<NetworkAddress>(NetworkErrors::NetworkAddressInvalid);
            host = text.substr(1, closing - 1);
            portText = text.substr(closing + 2);
            if (!ParseIpv6(host, addressBytes))
                return Fail<NetworkAddress>(NetworkErrors::NetworkAddressInvalid);
            kind = NetworkAddressKind::Ipv6;
        } else {
            const auto separator = text.find(':');
            if (separator == std::string_view::npos || text.find(':', separator + 1) != std::string_view::npos)
                return Fail<NetworkAddress>(NetworkErrors::NetworkAddressInvalid);
            host = text.substr(0, separator);
            portText = text.substr(separator + 1);
            if (host.empty())
                return Fail<NetworkAddress>(NetworkErrors::NetworkAddressInvalid);
            if (IsDecimalOrDot(host)) {
                if (!ParseIpv4(host, addressBytes))
                    return Fail<NetworkAddress>(NetworkErrors::NetworkAddressInvalid);
                kind = NetworkAddressKind::Ipv4;
            } else {
                const auto hostnameStatus = CanonicalizeHostname(host, hostname);
                if (hostnameStatus == HostnameStatus::Unsupported)
                    return Fail<NetworkAddress>(NetworkErrors::NetworkAddressUnsupported);
                if (hostnameStatus == HostnameStatus::CapacityExceeded)
                    return Fail<NetworkAddress>(NetworkErrors::NetworkAddressCapacityExceeded);
                if (hostnameStatus != HostnameStatus::Valid)
                    return Fail<NetworkAddress>(NetworkErrors::NetworkAddressInvalid);
                hostnameLength = static_cast<std::uint16_t>(host.size());
                kind = NetworkAddressKind::DnsHostname;
            }
        }

        auto port = ParsePort(portText);
        if (!port.HasValue())
            return Fail<NetworkAddress>(NetworkErrors::NetworkAddressInvalid);
        return Result<NetworkAddress>::Success(NetworkAddress{kind, port.Value(), addressBytes, hostname, hostnameLength});
    }

    /** @copydoc NetworkAddress::IsValid */
    bool NetworkAddress::IsValid() const noexcept {
        if (port_ == 0 || kind_ >= NetworkAddressKind::Count)
            return false;
        if (kind_ == NetworkAddressKind::DnsHostname)
            return hostnameLength_ != 0 && hostnameLength_ <= hostname_.size();
        return hostnameLength_ == 0;
    }

    /** @copydoc NetworkAddress::AddressBytes */
    std::span<const std::uint8_t> NetworkAddress::AddressBytes() const noexcept {
        if (kind_ == NetworkAddressKind::Ipv4)
            return {addressBytes_.data(), Ipv4ByteCount};
        if (kind_ == NetworkAddressKind::Ipv6)
            return addressBytes_;
        return {};
    }

    /** @copydoc NetworkAddress::Hostname */
    std::string_view NetworkAddress::Hostname() const noexcept {
        return kind_ == NetworkAddressKind::DnsHostname ? std::string_view{hostname_.data(), hostnameLength_} : std::string_view{};
    }

    /** @copydoc NetworkAddress::Diagnostic */
    NetworkAddressDiagnostic NetworkAddress::Diagnostic() const noexcept {
        NetworkAddressDiagnostic output;
        DiagnosticWriter writer{output};
        if (!IsValid()) {
            writer.Text("<invalid>");
            return output;
        }
        if (kind_ == NetworkAddressKind::Ipv4) {
            for (std::size_t index = 0; index < Ipv4ByteCount; ++index) {
                if (index != 0)
                    writer.Character('.');
                writer.Decimal(addressBytes_[index]);
            }
        } else if (kind_ == NetworkAddressKind::Ipv6) {
            writer.Character('[');
            for (std::size_t index = 0; index < Ipv6ByteCount; index += 2) {
                if (index != 0)
                    writer.Character(':');
                writer.HexGroup(
                    static_cast<std::uint16_t>((static_cast<std::uint16_t>(addressBytes_[index]) << 8U) | addressBytes_[index + 1]));
            }
            writer.Character(']');
        } else {
            writer.Text(Hostname());
        }
        writer.Character(':');
        writer.Decimal(port_);
        return output;
    }
}  // namespace Horo::Network
