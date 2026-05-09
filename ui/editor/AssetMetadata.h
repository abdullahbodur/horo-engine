/**
 * @file AssetMetadata.h
 * @brief Persistent metadata for imported assets, including import diagnostics and file dependencies.
 */
#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

#include "ui/editor/SceneDocument.h"

namespace Horo::Editor {
    /** @brief Categorizes the relationship of a dependency entry to its asset. */
    enum class AssetDependencyKind {
        Source,          /**< The raw source file consumed by the importer. */
        ProducedOutput,  /**< A file produced by the import step. */
        DownstreamAsset, /**< Another asset that depends on this one. */
    };

    /** @brief Severity level for a diagnostic message emitted during asset import. */
    enum class AssetDiagnosticSeverity {
        Info,    /**< Informational message; no action required. */
        Warning, /**< Non-fatal issue that may affect runtime behavior. */
        Error,   /**< Fatal issue that prevented a successful import. */
    };

    /**
     * @brief A single diagnostic message emitted by the asset importer.
     */
    struct AssetImportDiagnostic {
        AssetDiagnosticSeverity severity = AssetDiagnosticSeverity::Error; /**< Severity of the diagnostic. */
        std::string code;       /**< Short machine-readable diagnostic code. */
        std::string message;    /**< Human-readable description of the issue. */
        std::string assetGuid;  /**< GUID of the asset this diagnostic belongs to. */
        std::string sourcePath; /**< Source file path relevant to this diagnostic. */
        std::string importerId; /**< ID of the importer that produced this diagnostic. */
    };

    /**
     * @brief Records a single file dependency for an imported asset.
     */
    struct AssetDependencyRecord {
        AssetDependencyKind kind = AssetDependencyKind::Source; /**< Nature of this dependency. */
        std::string value; /**< File path or asset ID that this record references. */
    };

    /**
     * @brief Persistent metadata stored alongside an imported asset, capturing
     *        import settings, produced files, dependencies, and diagnostics.
     */
    struct AssetMetadata {
        int version = 1;             /**< Schema version for forward compatibility. */
        std::string assetId;         /**< Logical asset identifier used in the scene document. */
        std::string assetGuid;       /**< Stable GUID that survives renames and moves. */
        std::string displayName;     /**< Human-facing name shown in the assets panel. */
        std::string importerId;      /**< ID of the importer that processed this asset. */
        std::string sourcePath;      /**< Original source file path on disk. */
        std::unordered_map<std::string, std::string, StringHash, std::equal_to<> >
        settings;                    /**< Importer settings as key-value pairs. */
        std::vector<std::string> producedFiles;            /**< Paths of files produced by the last import. */
        std::vector<AssetDependencyRecord> dependencies;   /**< Recorded file and asset dependencies. */
        bool lastImportSucceeded = true;                   /**< True when the most recent import completed without errors. */
        std::string lastImportReason;                      /**< Human-readable reason for the last import run. */
        std::vector<AssetImportDiagnostic> diagnostics;    /**< Diagnostics emitted during the last import. */
    };

    /**
     * @brief Returns the managed asset directory for the given AssetDef.
     * @param asset The asset definition whose directory is requested.
     * @return Filesystem path to the managed directory for this asset.
     */
    std::filesystem::path GetManagedAssetDirectory(const AssetDef &asset);

    /**
     * @brief Returns the managed asset directory for the given asset GUID.
     * @param assetGuid Stable GUID identifying the asset.
     * @return Filesystem path to the managed directory for this asset.
     */
    std::filesystem::path GetManagedAssetDirectory(const std::string &assetGuid);

    /**
     * @brief Returns the path to the JSON metadata file for the given asset GUID.
     * @param assetGuid Stable GUID identifying the asset.
     * @return Filesystem path to the .meta JSON file.
     */
    std::filesystem::path GetAssetMetadataPath(const std::string &assetGuid);

    /**
     * @brief Loads the AssetMetadata for the given GUID from disk.
     * @param assetGuid   Stable GUID identifying the asset.
     * @param outMetadata Output parameter populated on success.
     * @param outError    Optional output parameter populated with an error message on failure.
     * @return True on success, false if the file could not be read or parsed.
     */
    bool LoadAssetMetadata(const std::string &assetGuid, AssetMetadata *outMetadata,
                           std::string *outError);

    /**
     * @brief Saves the given AssetMetadata to disk.
     * @param metadata The metadata to persist.
     * @param outError Optional output parameter populated with an error message on failure.
     * @return True on success, false if the file could not be written.
     */
    bool SaveAssetMetadata(const AssetMetadata &metadata, std::string *outError);

    /**
     * @brief Constructs a default AssetMetadata from an asset ID and its AssetDef.
     * @param assetId Logical asset identifier.
     * @param asset   The asset definition to populate metadata from.
     * @return Initialized AssetMetadata ready to be persisted or further populated.
     */
    AssetMetadata BuildAssetMetadata(const std::string &assetId,
                                     const AssetDef &asset);

    /**
     * @brief Ensures that every asset in the document has valid metadata on disk,
     *        creating missing metadata files as needed.
     * @param doc      The scene document whose assets are checked.
     * @param outError Optional output parameter populated with an error message on failure.
     * @return True if all metadata is present and valid, false if any file could not be written.
     */
    bool EnsureAssetMetadataForDocument(SceneDocument *doc, std::string *outError);
} // namespace Horo::Editor
