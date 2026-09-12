#pragma once

/**
 * @file ExtensionModuleResolution.h
 * @brief Pure deterministic resolution for modules inside one extension package.
 */

#include "Horo/Extensions/ExtensionManifest.h"
#include "Horo/Foundation/Result.h"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Horo::Extensions {
    /** @brief Selects which presentation authorities are available to a package activation attempt. */
    enum class ExtensionHostProfile : std::uint8_t {
        Headless,
        Interactive,
    };

    /** @brief Exact compatibility facts supplied by the host composition root. */
    struct ExtensionHostEnvironment {
        ExtensionHostProfile profile{ExtensionHostProfile::Interactive};          /**< Available presentation shape. */
        ExtensionHostPlatform platform{ExtensionHostPlatform::Count};             /**< Current operating system. */
        ExtensionHostArchitecture architecture{ExtensionHostArchitecture::Count}; /**< Current CPU architecture. */
        ExtensionBuildProfile buildProfile{ExtensionBuildProfile::Count};         /**< Current binary profile. */
        std::string_view engineVersion;                                           /**< Canonical running engine version. */
        std::uint32_t abiMajor{};                                                 /**< Generic extension ABI major version. */
        std::uint32_t abiMinor{};                                                 /**< Generic extension ABI minor version. */
        std::span<const std::string_view> capabilities; /**< Stable capability identities granted by composition. */
    };

    /** @brief Typed result of compatibility evaluation before executable loading. */
    enum class ExtensionCompatibilityStatus : std::uint8_t {
        Compatible,
        EngineVersionUnsupported,
        HostPlatformUnsupported,
        HostArchitectureUnsupported,
        BuildProfileUnsupported,
        AbiUnsupported,
        CapabilityUnavailable,
    };

    /** @brief Pure compatibility decision for one module and one explicit host. */
    struct ExtensionModuleCompatibility {
        ExtensionCompatibilityStatus status{ExtensionCompatibilityStatus::Compatible}; /**< Typed admission outcome. */
        std::string moduleId;                                                          /**< Module whose constraint was evaluated. */
        std::string requirement;   /**< Canonical unsupported requirement, when rejected. */
        std::string selectedEntry; /**< Selected package-relative entry when compatible. */

        /** @brief Tests whether every compatibility constraint was admitted. @return True only for a compatible module. */
        [[nodiscard]] bool IsCompatible() const noexcept {
            return status == ExtensionCompatibilityStatus::Compatible;
        }
    };

    /** @brief Explicit resolution state for one declared cross-module service import. */
    enum class ExtensionServiceImportStatus : std::uint8_t {
        Bound,
        Unavailable,
        Incompatible,
    };

    /** @brief Immutable service binding selected from the validated package module graph. */
    struct ResolvedExtensionServiceImport final {
        std::string consumerModuleId; /**< Module that declared and exclusively owns this import. */
        std::string importId;         /**< Stable import identity local to the consumer module. */
        std::string serviceId;        /**< Exact exported service identity. */
        std::string contractId;       /**< Exact typed callable contract identity. */
        std::string providerModuleId; /**< Selected provider module, empty only when unavailable. */
        std::string providerVersion;  /**< Selected service API version, empty only when unavailable. */
        ExtensionServiceImportStatus status{ExtensionServiceImportStatus::Unavailable}; /**< Explicit binding outcome. */
        bool required{true}; /**< Whether a non-bound outcome rejects composition. */
    };

    /** @brief Immutable deterministic module order and contribution ownership for one activation attempt. */
    struct ExtensionModulePlan {
        std::vector<std::string> moduleIds;                         /**< Dependency-first module identities. */
        std::vector<ExtensionContributionManifest> contributions;   /**< Contributions owned by selected modules. */
        std::vector<std::string> selectedEntries;                   /**< Entry selected for each module ID at the same index. */
        std::vector<ResolvedExtensionServiceImport> serviceImports; /**< Stable consumer/import ordered binding snapshot. */
    };

    /**
     * @brief Evaluates one module against explicit immutable host facts without loading code or mutating ambient state.
     * @param manifest Package metadata owning engine compatibility and legacy platform declarations.
     * @param module Module metadata whose ABI, capabilities and entry variants are evaluated.
     * @param host Exact host facts supplied by the application composition root.
     * @return Typed compatibility outcome naming the rejected constraint or selected entry.
     */
    [[nodiscard]] ExtensionModuleCompatibility EvaluateExtensionModuleCompatibility(const ExtensionManifest &manifest,
                                                                                    const ExtensionModuleManifest &module,
                                                                                    const ExtensionHostEnvironment &host);

    /**
     * @brief Resolves a fully parsed package manifest without loading code or mutating ambient state.
     * @param manifest Owned parsed package metadata whose module graph is resolved.
     * @param host Explicit host shape and compatibility facts; headless resolution excludes presentation-only modules.
     * @return A stable dependency-first plan, or a typed extension error naming every involved module.
     */
    [[nodiscard]] Result<ExtensionModulePlan> ResolveExtensionModules(const ExtensionManifest &manifest,
                                                                      const ExtensionHostEnvironment &host);
}  // namespace Horo::Extensions
