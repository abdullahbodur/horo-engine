#pragma once

/**
 * @file ExtensionModuleResolution.h
 * @brief Pure deterministic resolution for modules inside one extension package.
 */

#include "Horo/Extensions/ExtensionManifest.h"
#include "Horo/Foundation/Result.h"

#include <string>
#include <vector>

namespace Horo::Extensions {
    /** @brief Selects which presentation authorities are available to a package activation attempt. */
    enum class ExtensionHostProfile : std::uint8_t {
        Headless,
        Interactive,
    };

    /** @brief Immutable deterministic module order and contribution ownership for one activation attempt. */
    struct ExtensionModulePlan {
        std::vector<std::string> moduleIds;                       /**< Dependency-first module identities. */
        std::vector<ExtensionContributionManifest> contributions; /**< Contributions owned by selected modules. */
    };

    /**
     * @brief Resolves a fully parsed package manifest without loading code or mutating ambient state.
     * @param manifest Owned parsed package metadata whose module graph is resolved.
     * @param profile Explicit host shape; headless resolution excludes presentation-only modules.
     * @return A stable dependency-first plan, or a typed extension error naming every involved module.
     */
    [[nodiscard]] Result<ExtensionModulePlan> ResolveExtensionModules(const ExtensionManifest &manifest, ExtensionHostProfile profile);
}  // namespace Horo::Extensions
