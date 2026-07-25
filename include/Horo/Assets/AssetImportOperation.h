#pragma once

/**
 * @file AssetImportOperation.h
 * @brief Headless transactional import operation and host-owned commit boundary.
 */

#include "Horo/Assets/AssetImporter.h"
#include "Horo/Assets/AssetRegistry.h"
#include "Horo/Foundation/CancellationToken.h"
#include "Horo/Foundation/JobSystem.h"
#include "Horo/Foundation/Paths.h"
#include "Horo/Foundation/Result.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Horo::Editor
{
class ProjectMutationCoordinator;
class ProjectMutationLease;
} // namespace Horo::Editor

namespace Horo::Assets
{

// ---------------------------------------------------------------------------
// Operation phases and snapshot model
// ---------------------------------------------------------------------------

/** @brief Lifecycle phase of one import operation. */
enum class AssetImportPhase : std::uint8_t
{
    Selecting,    /**< User is choosing files and importers. */
    Preparing,    /**< Importers are producing editor payloads. */
    ReadyToCommit, /**< All parsing complete, awaiting host commit. */
    Committing,    /**< Host is writing sidecars and publishing registry. */
    Completed,     /**< Operation succeeded. */
    Failed,        /**< Operation failed with diagnostics. */
    Cancelled,     /**< Operation was cancelled. */
};

/** @brief One item in an import operation. */
struct AssetImportItem
{
    ProjectPath sourceFile;                        /**< Project-relative source path (display only). */
    std::filesystem::path absoluteSourcePath;      /**< Absolute path to source file for reading. */
    std::string importerContributionId;            /**< Selected importer contribution. */
    std::optional<AssetTypeId> resolvedType;       /**< Resolved asset type after import. */
    std::optional<PreparedAssetImport> result;     /**< Import result when completed. */
    std::vector<ImportDiagnostic> diagnostics;     /**< Per-item diagnostics. */
    std::string sourceExtension;                   /**< Lowercase file extension without dot. */
    std::string displayName;                       /**< File name for display. */
    std::string destinationFolder;                 /**< Project-relative destination folder. */
    std::unordered_map<std::string, std::string> settings; /**< Per-item importer settings (key=settingId, value=serialized). */
};

/** @brief Bounded snapshot of one import operation, safe for UI consumption. */
struct AssetImportSnapshot
{
    std::string operationId;                       /**< Stable operation identity. */
    std::uint64_t revision{};                      /**< Monotonic snapshot revision. */
    AssetImportPhase phase{AssetImportPhase::Selecting};
    std::vector<AssetImportItem> items;            /**< All items in deterministic order. */
    std::size_t selectedItemIndex{0};              /**< Index of the selected item for settings panel. */
    bool canCommit{false};                         /**< True when ReadyToCommit. */
    bool canCancel{false};                         /**< True before Committing/Completed/Failed. */
};

// ---------------------------------------------------------------------------
// Import request
// ---------------------------------------------------------------------------

/** @brief Typed request to start one import operation. */
struct AssetImportRequest
{
    std::filesystem::path projectRoot;             /**< Absolute project root. */
    std::vector<std::filesystem::path> sourceFiles; /**< Absolute paths to source files. */
    std::string destinationFolder;                  /**< Project-relative destination folder. */
};

// ---------------------------------------------------------------------------
// Asset ID generator (injectable)
// ---------------------------------------------------------------------------

/** @brief Host-owned injectable asset identity generator. */
class IAssetIdGenerator
{
  public:
    virtual ~IAssetIdGenerator() = default;

    /**
     * @brief Generates one unique asset identity.
     * @return A new canonical AssetId.
     */
    [[nodiscard]] virtual AssetId Generate() = 0;
};

// ---------------------------------------------------------------------------
// Asset import committer (host-owned)
// ---------------------------------------------------------------------------

/** @brief Validated batch ready for host commit. */
struct PreparedAssetImportBatch
{
    std::string operationId;
    std::filesystem::path projectRoot;
    ProjectPath destinationFolder;
    std::vector<AssetImportItem> items;
};

/**
 * @brief Host-owned commit boundary for asset import.
 * @details Acquires a project mutation lease, validates the batch against
 *          the current registry, assigns AssetIds, writes sidecars and
 *          editor payloads, and atomically publishes one registry snapshot.
 */
class IAssetImportCommitter
{
  public:
    virtual ~IAssetImportCommitter() = default;

    /**
     * @brief Commits a validated import batch to durable project storage.
     * @param batch Fully validated batch with items in deterministic order.
     * @param idGenerator Injected identity generator.
     * @param cancellation Cancellation token for cooperative abort.
     * @return Success or a typed commit failure. The registry is not modified on failure.
     */
    [[nodiscard]] virtual Result<void> Commit(PreparedAssetImportBatch batch,
                                              IAssetIdGenerator &idGenerator,
                                              const CancellationToken &cancellation) = 0;
};

// ---------------------------------------------------------------------------
// Asset import operation
// ---------------------------------------------------------------------------

/**
 * @brief Headless bounded import operation.
 * @details Accepts source files, selects importers from the pinned catalog snapshot,
 *          runs importer strategies through the job system, collects results,
 *          and produces a ReadyToCommit snapshot. The host committer owns
 *          the mutation boundary.
 *
 *          The operation never writes to the project, assigns AssetIds, or
 *          publishes registry snapshots. Importers receive only borrowed
 *          source bytes and host-owned output sinks.
 */
class AssetImportOperation final
{
  public:
    /**
     * @brief Constructs an operation pinned to one catalog snapshot.
     * @param jobs Job system that outlives this operation.
     * @param catalog Published immutable importer catalog snapshot.
     */
    AssetImportOperation(JobSystem &jobs,
                         std::shared_ptr<const AssetImporterCatalogSnapshot> catalog);

    /**
     * @brief Starts the import operation.
     * @param request Typed import request.
     * @param cancellation Parent cancellation token.
     * @return Initial Selecting-phase snapshot.
     /** @brief Analyses source files and selects importers without running them. */
     [[nodiscard]] Result<AssetImportSnapshot> Start(const AssetImportRequest &request,
                                                     const CancellationToken &cancellation);

     /** @brief Adds additional files to an existing operation queue. */
     [[nodiscard]] Result<AssetImportSnapshot> AddFiles(const std::vector<std::filesystem::path> &sourceFiles,
                                                        const std::filesystem::path &projectRoot,
                                                        const CancellationToken &cancellation);

    /** @brief Imports a single item by index, running its importer strategy. */
    [[nodiscard]] Result<AssetImportSnapshot> ImportSingleItem(std::size_t index,
                                                               const CancellationToken &cancellation);

    /**
     * @brief Returns the current snapshot for UI polling.
     */
    [[nodiscard]] AssetImportSnapshot Snapshot() const noexcept;

    /**
     * @brief Cancels the operation.
     */
    void Cancel();

  private:
    JobSystem &jobs_;
    std::shared_ptr<const AssetImporterCatalogSnapshot> catalog_;
    AssetImportSnapshot snapshot_;
    std::uint64_t revision_{0};
    bool cancelled_{false};
};

} // namespace Horo::Assets
