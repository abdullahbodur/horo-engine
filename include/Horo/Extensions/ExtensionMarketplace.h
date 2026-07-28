#pragma once

/**
 * @file ExtensionMarketplace.h
 * @brief Asynchronous registry-backed extension discovery and verified installation.
 */

#include "Horo/Foundation/JobSystem.h"
#include "Horo/Foundation/Result.h"

#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Horo::Extensions
{
class ExtensionInventory;

/** @brief One installable version projected from the configured extension registry. */
struct ExtensionMarketplaceEntry
{
    std::string packageId;   /**< Stable extension package ID. */
    std::string displayName; /**< Human-facing package name. */
    std::string description; /**< Marketplace summary. */
    std::string author;      /**< Registry publisher name. */
    std::string version;     /**< Exact immutable version. */
    std::string packageUrl;  /**< HTTPS URL of the immutable ZIP artifact. */
    std::string sha256;      /**< Canonical SHA-256 artifact identity. */
};

/** @brief Current user-visible state of the marketplace service. */
enum class ExtensionMarketplaceStatus
{
    Idle,
    Searching,
    Ready,
    Installing,
    Installed,
    Error,
};

/** @brief Immutable marketplace state copied by editor presentation code. */
struct ExtensionMarketplaceSnapshot
{
    ExtensionMarketplaceStatus status = ExtensionMarketplaceStatus::Idle;
    std::vector<ExtensionMarketplaceEntry> entries;
    std::string activePackageId;
    std::string message;
};

/**
 * @brief Parses and filters one registry-as-code document.
 * @param json UTF-8 registry JSON.
 * @param query Case-insensitive search query.
 * @return Compatible immutable marketplace entries.
 */
[[nodiscard]] Result<std::vector<ExtensionMarketplaceEntry>>
ParseExtensionMarketplaceRegistry(std::string_view json, std::string_view query);

/**
 * @brief Coordinates bounded registry fetches and verified extension installation.
 *
 * Work executes through the injected job system. The owner thread calls
 * Update() to publish a verified staged package through ExtensionInventory.
 */
class ExtensionMarketplaceService final
{
  public:
    /**
     * @brief Creates a marketplace service.
     * @param jobs Job system that outlives this service.
     * @param inventory Installed-extension authority that outlives this service.
     * @param registryUrl HTTPS registry-as-code JSON URL.
     */
    ExtensionMarketplaceService(
        JobSystem& jobs, ExtensionInventory& inventory, std::string registryUrl);

    /** @brief Cancels and joins active marketplace work before releasing staging. */
    ~ExtensionMarketplaceService();

    ExtensionMarketplaceService(const ExtensionMarketplaceService&) = delete;
    ExtensionMarketplaceService& operator=(const ExtensionMarketplaceService&) = delete;

    /** @brief Returns the default GitHub-hosted registry-as-code URL. */
    [[nodiscard]] static std::string DefaultRegistryUrl();

    /**
     * @brief Starts an asynchronous registry search.
     * @param query Case-insensitive package/name/description query.
     * @return Success after job admission, or an error when another operation is active.
     */
    [[nodiscard]] Result<void> Search(std::string query);

    /**
     * @brief Starts asynchronous download and verification for one search result.
     * @param packageId Exact marketplace package ID.
     * @return Success after job admission.
     */
    [[nodiscard]] Result<void> Install(std::string_view packageId);

    /** @brief Commits a completed staged install on the owner thread. */
    void Update();

    /** @brief Returns a thread-safe copy of current marketplace state. */
    [[nodiscard]] ExtensionMarketplaceSnapshot Snapshot() const;

    /** @brief Returns the configured absolute registry URL. */
    [[nodiscard]] const std::string& RegistryUrl() const noexcept;

  private:
    [[nodiscard]] bool OperationActiveLocked() const;
    void FinishWithError(std::string message);

    JobSystem& jobs_;
    ExtensionInventory& inventory_;
    std::string registryUrl_;
    mutable std::mutex mutex_;
    ExtensionMarketplaceSnapshot snapshot_;
    std::optional<JobHandle> activeJob_;
    std::optional<std::filesystem::path> pendingInstallRoot_;
    std::optional<std::filesystem::path> pendingCleanupRoot_;
};
} // namespace Horo::Extensions
