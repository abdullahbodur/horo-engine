#pragma once

#include "Horo/Foundation/Result.h"
#include <string>
#include <vector>

namespace Horo::Extensions
{
    /** @brief One versioned native or declarative module exported by an extension package. */
    struct ExtensionModuleManifest
    {
        std::string id;      /**< Stable module identity. */
        std::string version; /**< Canonical semantic module version. */
        std::string kind;    /**< Host-defined module kind. */
        std::string entry;   /**< Optional package-relative native entry name. */
    };

    /** @brief One manifest-declared contribution bound to a module in the same package. */
    struct ExtensionContributionManifest
    {
        std::string type;   /**< Typed extension point, for example `asset.importer`. */
        std::string id;     /**< Stable globally unique contribution identity. */
        std::string module; /**< Owning module identity from this manifest. */
    };

    /** @brief Represents a parsed extension.json manifest. */
    struct ExtensionManifest
    {
        std::string id;
        std::string version;
        std::string kind;
        std::string displayName;
        std::string description;
        std::string author;

        std::string engineMin;
        std::string engineMax;
        std::string sdkAbi;

        std::vector<std::string> platforms;
        std::vector<ExtensionModuleManifest> modules;
        std::vector<ExtensionContributionManifest> contributions;

        /** @brief The path to the directory containing this manifest. */
        std::string rootPath;
    };

    /**
     * @brief Parses an extension.json file.
     * @param jsonContent The content of the extension.json file.
     * @return Result containing the parsed manifest, or an error.
     */
    [[nodiscard]] Result<ExtensionManifest> ParseExtensionManifest(const std::string& jsonContent);

} // namespace Horo::Extensions
