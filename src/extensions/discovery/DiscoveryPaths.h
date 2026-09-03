/** @file
 * @brief Filesystem containment checks for approved extension discovery records.
 */
#pragma once

#include "Horo/Foundation/Result.h"

#include <filesystem>

namespace Horo::Extensions::Discovery {
    /**
     * @brief Canonicalize an existing absolute discovery root without granting approval.
     * @param root Product/package-policy supplied absolute directory.
     * @return Existing canonical directory, or a typed path validation error.
     */
    [[nodiscard]] Result<std::filesystem::path> CanonicalRoot(const std::filesystem::path &root);

    /**
     * @brief Resolve a declared relative path strictly inside an approved canonical root.
     * @param canonicalRoot Existing canonical root returned by CanonicalRoot.
     * @param relativePath Nonempty relative path without parent traversal components.
     * @return Canonical existing descendant, or a typed error for missing or escaping content.
     * @note This is a discovery-time check, not a file lease or execution trust grant.
     *       The install-record owner must protect content from mutation before activation.
     */
    [[nodiscard]] Result<std::filesystem::path> ResolveContainedPath(const std::filesystem::path &canonicalRoot,
                                                                     const std::filesystem::path &relativePath);
}  // namespace Horo::Extensions::Discovery
