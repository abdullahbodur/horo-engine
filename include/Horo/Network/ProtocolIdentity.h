#pragma once

/**
 * @file ProtocolIdentity.h
 * @brief Stable fixed-width protocol, message, schema, feature, and close-reason identities.
 */

#include "Horo/Foundation/Result.h"
#include "Horo/Network/NetworkErrors.h"

#include <compare>
#include <cstdint>

namespace Horo::Network {
    /** @brief Closed 16-bit wire namespace selected by the identity high bit. */
    enum class ProtocolIdentityNamespace : std::uint8_t {
        Engine,
        Game,
        Count
    };

    /** @brief Strong non-zero 16-bit wire identity; names and C++ symbols never participate. */
    template <typename Tag> class ProtocolIdentity final {
    public:
        using ValueType = std::uint16_t;
        static constexpr ValueType EngineMaximum = 0x7fff;
        static constexpr ValueType GameMinimum = 0x8000;
        static constexpr ValueType GameMaximum = 0xffff;

        constexpr ProtocolIdentity() = default;

        /** @brief Validates a raw fixed-width identity. @param value Non-zero wire value. @return Typed identity or IdentityInvalid. */
        [[nodiscard]] static Result<ProtocolIdentity> Create(const ValueType value) {
            if (value == 0)
                return Result<ProtocolIdentity>::Failure(MakeError(NetworkErrors::IdentityInvalid));
            return Result<ProtocolIdentity>::Success(ProtocolIdentity{value});
        }

        /** @brief Returns the exact 16-bit wire representation. @return Zero only for the invalid identity. */
        [[nodiscard]] constexpr ValueType Value() const noexcept {
            return value_;
        }

        /** @brief Checks representation only. @return Whether the value is non-zero. */
        [[nodiscard]] constexpr bool IsValid() const noexcept {
            return value_ != 0;
        }

        /** @brief Derives the closed namespace without arithmetic or narrowing. @return Count for zero, Engine below 0x8000, otherwise
         * Game. */
        [[nodiscard]] constexpr ProtocolIdentityNamespace Namespace() const noexcept {
            if (!IsValid())
                return ProtocolIdentityNamespace::Count;
            return value_ < GameMinimum ? ProtocolIdentityNamespace::Engine : ProtocolIdentityNamespace::Game;
        }

        constexpr auto operator<=>(const ProtocolIdentity &) const noexcept = default;

    private:
        explicit constexpr ProtocolIdentity(const ValueType value) noexcept : value_(value) {}

        ValueType value_{};
    };

    struct ProtocolIdentityTag;
    struct MessageTypeIdentityTag;
    struct MessageSchemaIdentityTag;
    struct ProtocolFeatureIdentityTag;
    struct CloseReasonIdentityTag;

    using ProtocolId = ProtocolIdentity<ProtocolIdentityTag>;
    using MessageTypeId = ProtocolIdentity<MessageTypeIdentityTag>;
    using MessageSchemaId = ProtocolIdentity<MessageSchemaIdentityTag>;
    using ProtocolFeatureId = ProtocolIdentity<ProtocolFeatureIdentityTag>;
    using CloseReasonId = ProtocolIdentity<CloseReasonIdentityTag>;

    /** @brief Explicit protocol version; zero major is reserved invalid. */
    struct ProtocolVersion final {
        std::uint16_t major{};
        std::uint16_t minor{};

        [[nodiscard]] constexpr bool IsValid() const noexcept {
            return major != 0;
        }

        constexpr auto operator<=>(const ProtocolVersion &) const noexcept = default;
    };

    /** @brief Closed same-major protocol interval used for exact compatibility selection. */
    struct ProtocolVersionRange final {
        ProtocolVersion minimum;
        ProtocolVersion maximum;

        [[nodiscard]] constexpr bool IsValid() const noexcept {
            return minimum.IsValid() && maximum.IsValid() && minimum.major == maximum.major && minimum <= maximum;
        }

        [[nodiscard]] constexpr bool Contains(const ProtocolVersion version) const noexcept {
            return IsValid() && version.major == minimum.major && version >= minimum && version <= maximum;
        }

        constexpr auto operator<=>(const ProtocolVersionRange &) const noexcept = default;
    };

    /** @brief Explicit message schema version; zero major is reserved invalid. */
    struct MessageSchemaVersion final {
        std::uint16_t major{};
        std::uint16_t minor{};

        [[nodiscard]] constexpr bool IsValid() const noexcept {
            return major != 0;
        }

        constexpr auto operator<=>(const MessageSchemaVersion &) const noexcept = default;
    };

    /** @brief Closed same-major message-schema interval used for exact codec compatibility. */
    struct MessageSchemaVersionRange final {
        MessageSchemaVersion minimum;
        MessageSchemaVersion maximum;

        /** @brief Validates the interval. @return Whether both endpoints are valid, same-major, and ordered. */
        [[nodiscard]] constexpr bool IsValid() const noexcept {
            return minimum.IsValid() && maximum.IsValid() && minimum.major == maximum.major && minimum <= maximum;
        }

        /** @brief Tests exact interval membership. @param version Candidate version. @return Whether it is supported. */
        [[nodiscard]] constexpr bool Contains(const MessageSchemaVersion version) const noexcept {
            return IsValid() && version.major == minimum.major && version >= minimum && version <= maximum;
        }

        constexpr auto operator<=>(const MessageSchemaVersionRange &) const noexcept = default;
    };

    /** @brief Safe terminal classification; numeric CloseReasonId remains the durable identity. */
    enum class CloseReasonKind : std::uint8_t {
        Normal,
        ProtocolError,
        Timeout,
        Shutdown,
        Count
    };
}  // namespace Horo::Network
