/** @file AssetImportService.h
 *  @brief Orchestrates asset import and reimport operations for the editor.
 */
#pragma once

#include <future>
#include <optional>
#include <string>

#include "ui/editor/AssetGuidRegistry.h"
#include "ui/editor/AssetImporterRegistry.h"

namespace Horo::Editor {
    /** @brief Outcome record for a single asset reimport pass. */
    struct AssetReimportRecord {
        std::string assetId;   /**< Logical asset identifier. */
        std::string assetGuid; /**< Persistent GUID of the asset. */
        std::string reason;    /**< Human-readable reason the reimport was triggered. */
        bool ok = false;       /**< True when the reimport succeeded. */
        std::string error;     /**< Error message on failure; empty on success. */
    };

    /** @brief Aggregate result of reimporting an asset together with all its dependents. */
    struct AssetReimportResult {
        bool ok = false;                          /**< True when all reimports succeeded. */
        std::vector<std::string> order;           /**< Processing order of asset GUIDs. */
        std::vector<AssetReimportRecord> records; /**< Per-asset outcome records. */
        std::string error;                        /**< Top-level error message, if any. */
    };

    /** @brief Optional texture slot overrides for asset import.
     *
     *  When provided, these paths override the auto-detected textures from the source file.
     *  Empty strings mean "use auto-detected" or "no texture" if none was detected.
     */
    struct TextureOverrides {
        std::string albedoMap;
        std::string normalMap;
        std::string metallicRoughnessMap;
        std::string emissiveMap;
        std::string occlusionMap;
    };

    /** @brief High-level service that drives asset import and reimport through the registry. */
    class AssetImportService {
    public:
        /** @brief Constructs a service with a default-constructed importer registry (built-in importers registered). */
        AssetImportService() = default;

        /** @brief Imports an asset from a source file path into the project.
         *  @param sourcePath  Absolute path to the source file.
         *  @param assetId     Logical identifier to assign to the asset.
         *  @param assetGuid   Persistent GUID to assign to the asset.
         *  @param displayName Human-readable display name shown in the editor.
         *  @param settings    Optional key-value importer settings.
         *  @param overrides   Optional texture slot overrides; when set, paths override auto-detected textures.
         *  @return Import result containing the produced AssetDef and any diagnostics.
         */
        AssetImportResult ImportAssetFromSource(
            const std::string &sourcePath, const std::string &assetId,
            const std::string &assetGuid, const std::string &displayName,
            const std::unordered_map<std::string, std::string, StringHash,
                std::equal_to<> > &settings = {},
            const std::optional<TextureOverrides> &overrides = std::nullopt) const;

        /** @brief Async variant — runs ImportAsync on the importer.
         *  @return Future that resolves to the import result on a worker thread. */
        std::future<AssetImportResult> ImportAssetFromSourceAsync(
            const std::string &sourcePath, const std::string &assetId,
            const std::string &assetGuid, const std::string &displayName,
            const std::unordered_map<std::string, std::string, StringHash,
                std::equal_to<> > &settings = {},
            const std::optional<TextureOverrides> &overrides = std::nullopt) const;

        /** @brief Imports or re-imports the texture file associated with an asset.
         *  @param sourcePath Absolute path to the texture source file.
         *  @param assetId    Logical identifier of the owning asset.
         *  @param asset      AssetDef to update with texture information.
         *  @param outError   Receives a diagnostic message on failure.
         *  @return True on success.
         */
        bool ImportTextureForAsset(const std::string &sourcePath,
                                   const std::string &assetId, AssetDef *asset,
                                   std::string *outError) const;

        /** @brief Persists the metadata sidecar file for an asset.
         *  @param assetId  Logical identifier of the asset.
         *  @param asset    AssetDef whose metadata should be written.
         *  @param outError Receives a diagnostic message on failure.
         *  @return True on success.
         */
        bool SaveMetadataForAsset(const std::string &assetId, const AssetDef &asset,
                                  std::string *outError) const;

        /** @brief Reimports an asset and all assets that depend on it.
         *  @param doc           Scene document providing the asset registry.
         *  @param rootAssetGuid GUID of the asset that triggered the reimport.
         *  @param reason        Human-readable reason string recorded in each outcome record.
         *  @return Aggregate result with per-asset outcome records.
         */
        AssetReimportResult
        ReimportAssetWithDependents(SceneDocument *doc,
                                    const std::string &rootAssetGuid,
                                    const std::string &reason) const;

        /** @brief Returns a const reference to the importer registry.
         *  @return The AssetImporterRegistry owned by this service.
         */
        const AssetImporterRegistry &Registry() const { return m_registry; }

        /** @brief Returns the in-memory GUID → metadata index used for cross-asset lookups.
         *
         *  The registry is refreshed at the start of every reimport flow and may be
         *  read at any time for dependents/lookup queries. Callers that mutate
         *  metadata on disk should call @ref AssetGuidRegistry::Invalidate or
         *  @ref AssetGuidRegistry::Insert to keep the cache consistent.
         *  @return Const reference to the owned registry.
         */
        const AssetGuidRegistry &GuidRegistry() const { return m_guidRegistry; }

    private:
        /** @brief Dispatches a single import request to the given importer.
         *  @param importer The concrete importer to run.
         *  @param request  Import parameters.
         *  @return The importer's result.
         */
        AssetImportResult RunImporter(const AssetImporter &importer,
                                      const AssetImportRequest &request) const;

        /** @brief Reimports one asset by GUID and appends the outcome to result.
         *  @param guid         GUID of the asset to reimport.
         *  @param recordReason Reason string recorded in the outcome.
         *  @param doc          Scene document providing the asset registry.
         *  @param result       Aggregate result accumulator.
         *  @return True when the asset reimport succeeded.
         */
        bool ReimportSingleAsset(const std::string &guid,
                                 const std::string &recordReason, SceneDocument *doc,
                                 AssetReimportResult &result) const;

        AssetImporterRegistry m_registry; /**< Registry of all registered importers. */
        mutable AssetGuidRegistry m_guidRegistry; /**< In-memory GUID → metadata cache, refreshed by reimport flows. */
    };
} // namespace Horo::Editor
