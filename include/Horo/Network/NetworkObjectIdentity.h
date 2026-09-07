#pragma once

/**
 * @file NetworkObjectIdentity.h
 * @brief Session-scoped authority and generation-safe replicated-object identities.
 */

#include "Horo/Foundation/Result.h"
#include "Horo/Network/NetworkErrors.h"

#include <compare>
#include <cstdint>
#include <limits>

namespace Horo::Network {
    /** @brief Non-zero authority generation that scopes every replicated-object occurrence in one session. */
    class ReplicationAuthorityEpoch final {
    public:
        /** @brief Constructs the reserved invalid epoch. */
        constexpr ReplicationAuthorityEpoch() = default;

        /**
         * @brief Validates an authority epoch allocated by the session owner.
         * @param value Non-zero monotonic authority generation.
         * @return Valid epoch or NetworkErrors::NetworkObjectIdentityInvalid.
         */
        [[nodiscard]] static Result<ReplicationAuthorityEpoch> Create(std::uint64_t value);

        /** @brief Checks representation only. @return Whether the epoch is non-zero. */
        [[nodiscard]] constexpr bool IsValid() const noexcept {
            return value_ != 0;
        }

        /** @brief Returns the opaque authority generation. @return Zero only for an invalid epoch. */
        [[nodiscard]] constexpr std::uint64_t Value() const noexcept {
            return value_;
        }

        constexpr auto operator<=>(const ReplicationAuthorityEpoch &) const noexcept = default;

    private:
        explicit constexpr ReplicationAuthorityEpoch(const std::uint64_t value) noexcept : value_(value) {}

        std::uint64_t value_{};
    };

    /** @brief Replicated-object occurrence scoped by authority epoch, slot, and exact non-zero reuse generation. */
    class NetworkObjectId final {
    public:
        /** @brief Constructs the reserved invalid identity. */
        constexpr NetworkObjectId() = default;

        /**
         * @brief Validates an authority-issued object occurrence identity.
         * @param epoch Non-zero authority epoch that prevents identity aliasing across replacement sessions/worlds.
         * @param slot Non-zero opaque slot unique within the authority epoch.
         * @param generation Non-zero exact slot generation.
         * @return Valid identity or NetworkErrors::NetworkObjectIdentityInvalid.
         */
        [[nodiscard]] static Result<NetworkObjectId> Create(ReplicationAuthorityEpoch epoch, std::uint64_t slot, std::uint32_t generation);

        /** @brief Checks representation, not mapping residency. @return Whether slot and generation are non-zero. */
        [[nodiscard]] constexpr bool IsValid() const noexcept {
            return epoch_.IsValid() && slot_ != 0 && generation_ != 0;
        }

        /** @brief Returns the exact authority scope. @return Invalid only for an invalid object identity. */
        [[nodiscard]] constexpr ReplicationAuthorityEpoch Epoch() const noexcept {
            return epoch_;
        }

        /** @brief Returns the opaque authority-owned slot. @return Zero only for an invalid identity. */
        [[nodiscard]] constexpr std::uint64_t Slot() const noexcept {
            return slot_;
        }

        /** @brief Returns the exact occurrence generation. @return Zero only for an invalid identity. */
        [[nodiscard]] constexpr std::uint32_t Generation() const noexcept {
            return generation_;
        }

        /**
         * @brief Produces the only valid successor after this exact occurrence has been retired.
         * @return Next generation or NetworkErrors::NetworkObjectGenerationExhausted; never wraps.
         * @pre The owning mapping has retired this identity.
         */
        [[nodiscard]] Result<NetworkObjectId> NextGeneration() const;

        constexpr auto operator<=>(const NetworkObjectId &) const noexcept = default;

    private:
        constexpr NetworkObjectId(const ReplicationAuthorityEpoch epoch, const std::uint64_t slot, const std::uint32_t generation) noexcept
            : epoch_(epoch), slot_(slot), generation_(generation) {}

        ReplicationAuthorityEpoch epoch_;
        std::uint64_t slot_{};
        std::uint32_t generation_{};
    };
}  // namespace Horo::Network
