#pragma once

/** @file PackagePath.h
 * @brief Validated portable package-relative file identities.
 */

#include "Horo/Foundation/Result.h"

#include <string>
#include <string_view>

namespace Horo::Packages {
    /** @brief An inert canonical UTF-8 file path; construction never accesses the filesystem. */
    class PackagePath {
    public:
        /**
         * @brief Validates a relative NFC UTF-8 path under the portable package policy.
         * @param text Slash-separated file path, at most 1024 bytes, 24 components and 255 bytes per component.
         * @return Validated path or packages.path.invalid; no normalization silently changes archive identity.
         */
        [[nodiscard]] static Result<PackagePath> Parse(std::string_view text);

        /** @brief Returns the original canonical file identity. @return Borrowed canonical path. */
        [[nodiscard]] const std::string &Value() const noexcept;

        /**
         * @brief Returns a conservative Unicode compatibility-normalized, case-folded collision key.
         * @return Borrowed key for collision detection only, never for extraction or file identity.
         */
        [[nodiscard]] const std::string &CollisionKey() const noexcept;

    private:
        /** @brief Constructs only after spelling and collision-key validation succeeds. */
        PackagePath(std::string value, std::string collisionKey);

        std::string m_value;
        std::string m_collisionKey;
    };
}  // namespace Horo::Packages
