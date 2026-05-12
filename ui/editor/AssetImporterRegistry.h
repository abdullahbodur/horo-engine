/** @file AssetImporterRegistry.h
 *  @brief Declares AssetImporter, AssetImporterRegistry, and associated request/result types.
 */
#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "ui/editor/AssetMetadata.h"

namespace Horo::Editor {
    /** @brief Input parameters passed to an AssetImporter::Import call. */
    struct AssetImportRequest {
        std::string assetId;     /**< Logical identifier to assign to the asset. */
        std::string assetGuid;   /**< Persistent GUID to assign to the asset. */
        std::string displayName; /**< Human-readable display name shown in the editor. */
        std::string sourcePath;  /**< Absolute path to the source file being imported. */
        std::unordered_map<std::string, std::string, StringHash, std::equal_to<> >
        settings; /**< Optional key-value importer configuration. */
    };

    /** @brief Result produced by an AssetImporter::Import call. */
    struct AssetImportResult {
        bool ok = false;                              /**< True when the import succeeded. */
        AssetDef asset;                               /**< Produced asset definition. */
        AssetMetadata metadata;                       /**< Produced asset metadata. */
        std::string error;                            /**< Error message on failure; empty on success. */
        std::vector<AssetImportDiagnostic> diagnostics; /**< Non-fatal import diagnostics. */
    };

    /** @brief Abstract base class for a single-format asset importer. */
    class AssetImporter {
    public:
        virtual ~AssetImporter() = default;

        /** @brief Returns a stable string identifier for this importer.
         *  @return Null-terminated importer ID string.
         */
        virtual const char *ImporterId() const = 0;

        /** @brief Returns the asset kind string produced by this importer (e.g. "mesh").
         *  @return Null-terminated asset kind string.
         */
        virtual const char *AssetKind() const = 0;

        /** @brief Returns the list of lowercase file extensions this importer handles.
         *  @return Vector of extensions including the leading dot (e.g. ".obj").
         */
        virtual std::vector<std::string> SupportedExtensions() const = 0;

        /** @brief Performs the import and returns a fully populated result.
         *  @param request Input parameters describing the source and destination.
         *  @return AssetImportResult with the produced asset or a failure description.
         */
        virtual AssetImportResult Import(const AssetImportRequest &request) const = 0;
    };

    /** @brief Owns all registered AssetImporter instances and dispatches lookups by extension or ID. */
    class AssetImporterRegistry {
    public:
        /** @brief Constructs the registry and registers built-in importers. */
        AssetImporterRegistry();

        /** @brief Registers an importer, transferring ownership to the registry.
         *  @param importer Importer to register.
         */
        void Register(std::unique_ptr<AssetImporter> importer);

        /** @brief Finds an importer that handles the extension of the given source path.
         *  @param sourcePath Path whose extension is used for lookup.
         *  @return Pointer to a matching importer, or nullptr if none is registered.
         */
        const AssetImporter *FindByExtension(const std::string &sourcePath) const;

        /** @brief Finds an importer by its stable importer ID string.
         *  @param importerId The importer ID to look up.
         *  @return Pointer to a matching importer, or nullptr if not found.
         */
        const AssetImporter *FindById(std::string_view importerId) const;

        /** @brief Returns the IDs of all currently registered importers.
         *  @return Vector of importer ID strings.
         */
        std::vector<std::string> RegisteredImporterIds() const;

    private:
        std::vector<std::unique_ptr<AssetImporter> > m_importers; /**< Owned importer instances. */
    };
} // namespace Horo::Editor
