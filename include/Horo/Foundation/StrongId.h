#pragma once

/**
 * @file StrongId.h
 * @brief Internal reusable representation for domain-tagged non-zero identities.
 */

#include "Horo/Foundation/ErrorCode.h"
#include "Horo/Foundation/Result.h"

#include <compare>
#include <cstdint>

namespace Horo::Foundation::Detail {
    /**
     * @brief Non-zero persisted 64-bit identity distinguished by a domain tag and error contract.
     * @tparam Tag Incomplete domain-owned tag that prevents cross-domain conversion.
     * @tparam InvalidIdentityError Domain-owned descriptor returned for the reserved zero value.
     */
    template <typename Tag, const ErrorCodeDescriptor &InvalidIdentityError> class NonZeroId64 final {
    public:
        /** @brief Constructs the reserved invalid zero identity. */
        constexpr NonZeroId64() = default;

        /**
         * @brief Validates a domain-issued identity.
         * @param value Persisted non-zero value.
         * @return Typed identity or the domain-owned invalid-identity error.
         */
        [[nodiscard]] static Result<NonZeroId64> Create(const std::uint64_t value) {
            if (value == 0)
                return Result<NonZeroId64>::Failure(MakeError(InvalidIdentityError));
            return Result<NonZeroId64>::Success(NonZeroId64{value});
        }

        /** @brief Returns the domain-issued value. @return Zero only for the invalid identity. */
        [[nodiscard]] constexpr std::uint64_t Value() const noexcept {
            return value_;
        }

        /** @brief Checks representation. @return Whether the value is non-zero. */
        [[nodiscard]] constexpr bool IsValid() const noexcept {
            return value_ != 0;
        }

        [[nodiscard]] constexpr auto operator<=>(const NonZeroId64 &) const noexcept = default;

    private:
        explicit constexpr NonZeroId64(const std::uint64_t value) noexcept : value_(value) {}

        std::uint64_t value_{};
    };
}  // namespace Horo::Foundation::Detail
