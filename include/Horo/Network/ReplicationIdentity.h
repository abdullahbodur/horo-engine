#pragma once

/**
 * @file ReplicationIdentity.h
 * @brief Stable schema, field, value-type, and codec identities for replication contracts.
 */

#include "Horo/Foundation/Result.h"
#include "Horo/Network/NetworkErrors.h"

#include <compare>
#include <cstdint>

namespace Horo::Network {
    /** @brief Non-zero stable replication identity distinguished by its domain tag. */
    template <typename Tag, typename ValueType> class ReplicationIdentity final {
    public:
        /** @brief Constructs the reserved invalid zero identity. */
        constexpr ReplicationIdentity() = default;

        /**
         * @brief Validates a stable identity value.
         * @param value Non-zero value issued by the declaring owner.
         * @return Typed identity or NetworkErrors::IdentityInvalid.
         */
        [[nodiscard]] static Result<ReplicationIdentity> Create(const ValueType value) {
            if (value == 0)
                return Result<ReplicationIdentity>::Failure(MakeError(NetworkErrors::IdentityInvalid));
            return Result<ReplicationIdentity>::Success(ReplicationIdentity{value});
        }

        /** @brief Returns the stable numeric value. @return Zero only for the invalid identity. */
        [[nodiscard]] constexpr ValueType Value() const noexcept {
            return value_;
        }

        /** @brief Checks representation, not registry membership. @return Whether the value is non-zero. */
        [[nodiscard]] constexpr bool IsValid() const noexcept {
            return value_ != 0;
        }

        constexpr auto operator<=>(const ReplicationIdentity &) const noexcept = default;

    private:
        explicit constexpr ReplicationIdentity(const ValueType value) noexcept : value_(value) {}

        ValueType value_{};
    };

    struct ReplicationSchemaIdentityTag;
    struct ReplicationFieldIdentityTag;
    struct ReplicationValueTypeIdentityTag;
    struct ReplicationCodecIdentityTag;

    /** @brief Globally stable semantic identity of one replication schema. */
    using ReplicationSchemaId = ReplicationIdentity<ReplicationSchemaIdentityTag, std::uint64_t>;
    /** @brief Stable non-zero 32-bit wire identity scoped by one schema. */
    using FieldId = ReplicationIdentity<ReplicationFieldIdentityTag, std::uint32_t>;
    /** @brief Stable semantic identity of a registered bounded value type. */
    using ReplicationValueTypeId = ReplicationIdentity<ReplicationValueTypeIdentityTag, std::uint32_t>;
    /** @brief Stable semantic identity of a registered canonical field codec. */
    using ReplicationCodecId = ReplicationIdentity<ReplicationCodecIdentityTag, std::uint32_t>;

    /** @brief Explicit schema version; major changes are not implicitly compatible. */
    struct ReplicationSchemaVersion final {
        std::uint16_t major{};
        std::uint16_t minor{};

        /** @brief Checks representation. @return Whether the major version is non-zero. */
        [[nodiscard]] constexpr bool IsValid() const noexcept {
            return major != 0;
        }

        constexpr auto operator<=>(const ReplicationSchemaVersion &) const noexcept = default;
    };

    /** @brief Closed same-major version interval explicitly accepted by one schema descriptor. */
    struct ReplicationCompatibilityRange final {
        ReplicationSchemaVersion minimum;
        ReplicationSchemaVersion maximum;

        /** @brief Tests explicit membership without guessing cross-major compatibility. */
        [[nodiscard]] constexpr bool Contains(const ReplicationSchemaVersion version) const noexcept {
            return minimum.IsValid() && maximum.IsValid() && version.major == minimum.major && version.major == maximum.major &&
                   version >= minimum && version <= maximum;
        }

        constexpr auto operator<=>(const ReplicationCompatibilityRange &) const noexcept = default;
    };
}  // namespace Horo::Network
