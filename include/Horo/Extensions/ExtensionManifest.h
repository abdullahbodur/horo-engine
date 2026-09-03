#pragma once

/**
 * @file ExtensionManifest.h
 * @brief Bounded extension manifest parsing and validated package metadata.
 */

#include "Horo/Foundation/Result.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Horo::Extensions {
    /** @brief Resource limits applied before an untrusted manifest can reach extension resolution. */
    struct ExtensionManifestLimits {
        std::size_t maximumDocumentBytes{64U * 1024U}; /**< Maximum encoded JSON document size. */
        std::size_t maximumNestingDepth{16};           /**< Maximum number of nested JSON containers. */
        std::size_t maximumObjectMembers{512};         /**< Maximum object members across the document. */
        std::size_t maximumArrayElements{512};         /**< Maximum array elements across the document. */
        std::size_t maximumStringBytes{4U * 1024U};    /**< Maximum decoded bytes in one general string. */
        std::size_t maximumIdentifierBytes{256};       /**< Maximum decoded bytes in one stable identity. */
        std::size_t maximumModules{64};                /**< Maximum modules declared by one package. */
        std::size_t maximumContributions{256};         /**< Maximum contributions declared by one package. */
        std::size_t maximumPlatforms{32};              /**< Maximum compatibility platforms. */
    };

    /** @brief One versioned native or declarative module exported by an extension package. */
    struct ExtensionModuleManifest {
        std::string id;      /**< Stable module identity. */
        std::string version; /**< Canonical semantic module version. */
        std::string kind;    /**< Host-defined module kind. */
        std::string entry;   /**< Optional package-relative native entry name. */
    };

    /** @brief One manifest-declared contribution bound to a module in the same package. */
    struct ExtensionContributionManifest {
        std::string type;         /**< Typed extension point, for example `asset.importer`. */
        std::string id;           /**< Stable globally unique contribution identity. */
        std::string owningModule; /**< Owning module identity from this manifest. */
    };

    /** @brief Represents a parsed extension.json manifest. */
    struct ExtensionManifest {
        std::uint32_t schemaVersion{1};
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
     * @brief Parses and validates an untrusted extension.json document without loading extension code.
     * @param jsonContent Encoded JSON content; the returned manifest owns every decoded value.
     * @param limits Resource limits enforced during syntax and schema decoding.
     * @return Validated manifest, or an error whose diagnostic message starts with the exact JSON field path.
     */
    [[nodiscard]] Result<ExtensionManifest> ParseExtensionManifest(std::string_view jsonContent,
                                                                   const ExtensionManifestLimits &limits = {});

}  // namespace Horo::Extensions
