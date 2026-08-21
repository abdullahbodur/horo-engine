#pragma once

/**
 * @file AssetReimport.h
 * @brief Identity-preserving transactional reimport of one project asset.
 */

#include "Horo/Assets/AssetImportMetadata.h"
#include "Horo/Assets/AssetRegistry.h"
#include "Horo/Foundation/CancellationToken.h"
#include "Horo/Foundation/Platform.h"

#include <filesystem>
#include <vector>

namespace Horo::Assets {
    /** @brief Validated inputs required to reimport one committed project asset. */
    struct AssetReimportRequest {
        std::filesystem::path absoluteProjectRoot;             /**< Canonical absolute project root. */
        std::filesystem::path absoluteAssetPath;               /**< Absolute committed asset payload path. */
        const AssetImporterCatalogSnapshot *importerCatalog{}; /**< Pinned importer catalog. */
        AssetRegistry *registry{};                             /**< Owner-thread registry publisher. */
        DurableFileSystem *files{};                            /**< Durable same-filesystem publication service. */
    };

    /** @brief Result of a successful identity-preserving reimport transaction. */
    struct AssetReimportReport {
        AssetId assetId;                        /**< Stable identity preserved by the transaction. */
        std::vector<AssetImportReason> reasons; /**< Detected reasons recorded in metadata. */
        std::string sourceHash;                 /**< Newly committed source hash. */
        std::string importerVersion;            /**< Newly committed importer version. */
        std::string moduleVersion;              /**< Newly committed owning module version. */
    };

    /**
     * @brief Reimports one asset from durable provenance and atomically replaces its payload and sidecar.
     * @param request Absolute project/asset paths and host-owned services.
     * @param cancellation Cooperative cancellation token checked before publication.
     * @return Reimport report, or a typed error with the previous payload restored.
     */
    [[nodiscard]] Result<AssetReimportReport> ReimportProjectAsset(const AssetReimportRequest &request,
                                                                   const CancellationToken &cancellation);
}  // namespace Horo::Assets
