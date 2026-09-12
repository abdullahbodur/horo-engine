#pragma once

/**
 * @file AssetCookTargetId.h
 * @brief Canonical cross-subsystem identity for one asset cook target.
 */

#include "Horo/Foundation/Result.h"

#include <compare>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

namespace Horo {
    inline constexpr std::size_t MaximumAssetCookTargetIdBytes = 65'535;

    namespace AssetCookTargetErrors {
        /** @brief Canonical asset cook-target parsing failed. */
        extern const ErrorCodeDescriptor Invalid;
    }  // namespace AssetCookTargetErrors

    /** @brief Validated lowercase, hyphen-separated asset cook-target identity. */
    class AssetCookTargetId final {
    public:
        AssetCookTargetId() = default;

        /**
         * @brief Parses a canonical target identifier containing at least two segments.
         * @param text Text to validate.
         * @return Validated target ID or a typed format error.
         */
        [[nodiscard]] static Result<AssetCookTargetId> Parse(std::string_view text);
        /** @brief Returns the canonical identifier text. @return Borrowed text owned by this value. */
        [[nodiscard]] const std::string &Value() const noexcept;
        /** @brief Reports whether this value contains a parsed identity. */
        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] auto operator<=>(const AssetCookTargetId &) const noexcept = default;

    private:
        explicit AssetCookTargetId(std::string value) : value_(std::move(value)) {}

        std::string value_;
    };
}  // namespace Horo
