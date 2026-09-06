#pragma once

/**
 * @file PhysicsFilterIdentity.h
 * @brief Stable project collision-layer, profile and query-channel identities.
 */

#include "Horo/Foundation/Result.h"

#include <array>
#include <compare>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace Horo::Physics {
    namespace Detail {
        /** @brief Parses the canonical project UUID representation shared by tagged Physics IDs. */
        [[nodiscard]] Result<std::array<std::uint8_t, 16>> ParsePhysicsProjectId(std::string_view value);
        /** @brief Formats one project identity as a canonical lowercase UUID. */
        [[nodiscard]] std::string FormatPhysicsProjectId(const std::array<std::uint8_t, 16> &bytes);
    }  // namespace Detail

    /**
     * @brief Non-interchangeable, persistent 128-bit project identity.
     *
     * The value is inert metadata. It does not resolve project state, acquire a schema lease,
     * register native filters or prove that a definition exists in the active generation.
     */
    template <typename Tag> class PhysicsProjectId final {
    public:
        /** @brief Constructs an invalid all-zero identity. */
        PhysicsProjectId() = default;

        /**
         * @brief Parses an RFC 9562 canonical lowercase UUID without braces or prefixes.
         * @param value Exact 36-character textual representation.
         * @return Tagged non-zero identity or PhysicsErrors::DescriptorInvalid.
         */
        [[nodiscard]] static Result<PhysicsProjectId> Parse(const std::string_view value) {
            auto parsed = Detail::ParsePhysicsProjectId(value);
            if (parsed.HasError())
                return Result<PhysicsProjectId>::Failure(parsed.ErrorValue());
            return Result<PhysicsProjectId>::Success(PhysicsProjectId{std::move(parsed).Value()});
        }

        /**
         * @brief Wraps persistent UUID bytes without resolving project state.
         * @param bytes Exact 128-bit representation; the all-zero value remains invalid.
         * @return Tagged value containing the supplied bytes.
         */
        [[nodiscard]] static constexpr PhysicsProjectId FromBytes(const std::array<std::uint8_t, 16> bytes) noexcept {
            return PhysicsProjectId{bytes};
        }

        /** @brief Checks representation only. @return Whether at least one byte is non-zero. */
        [[nodiscard]] constexpr bool IsValid() const noexcept {
            for (const std::uint8_t byte : bytes_) {
                if (byte != 0)
                    return true;
            }
            return false;
        }

        /** @brief Formats the canonical lowercase UUID. @return Newly allocated 36-character text. */
        [[nodiscard]] std::string ToString() const {
            return Detail::FormatPhysicsProjectId(bytes_);
        }

        /** @brief Returns the persistent representation. @return Borrowed bytes owned by this value. */
        [[nodiscard]] constexpr const std::array<std::uint8_t, 16> &Bytes() const noexcept {
            return bytes_;
        }

        constexpr auto operator<=>(const PhysicsProjectId &) const noexcept = default;

    private:
        explicit constexpr PhysicsProjectId(const std::array<std::uint8_t, 16> bytes) noexcept : bytes_(bytes) {}

        std::array<std::uint8_t, 16> bytes_{};
    };

    struct CollisionLayerTag;
    struct CollisionProfileTag;
    struct PhysicsQueryChannelTag;

    /** @brief Stable project identity of one simulation collision layer. */
    using CollisionLayerId = PhysicsProjectId<CollisionLayerTag>;
    /** @brief Stable project identity of one complete reusable collision profile. */
    using CollisionProfileId = PhysicsProjectId<CollisionProfileTag>;
    /** @brief Stable project identity naming one query intent, never a target class or native bit. */
    using PhysicsQueryChannelId = PhysicsProjectId<PhysicsQueryChannelTag>;

    /** @brief Stable asset-local material slot, never a native material or face index. */
    class PhysicsMaterialSlotId final {
    public:
        PhysicsMaterialSlotId() = default;

        [[nodiscard]] static constexpr PhysicsMaterialSlotId FromValue(const std::uint64_t value) noexcept {
            return PhysicsMaterialSlotId{value};
        }

        [[nodiscard]] constexpr std::uint64_t Value() const noexcept {
            return value_;
        }

        [[nodiscard]] constexpr bool IsValid() const noexcept {
            return value_ != 0;
        }

        constexpr auto operator<=>(const PhysicsMaterialSlotId &) const noexcept = default;

    private:
        explicit constexpr PhysicsMaterialSlotId(const std::uint64_t value) noexcept : value_(value) {}

        std::uint64_t value_{};
    };
}  // namespace Horo::Physics
