#pragma once

/**
 * @file PackageCache.h
 * @brief Content-addressed package archive storage and quarantine contracts.
 */

#include "Horo/Foundation/Platform.h"
#include "Horo/Packages/PackageArchive.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>

namespace Horo::Packages {
    /** @brief Stable reasons for isolating bytes outside the active package cache. */
    enum class PackageQuarantineReason : std::uint8_t {
        HashMismatch,
        InvalidArchive,
        VerificationFailure,
        CorruptCacheEntry,
    };

    /** @brief Result of an atomic cache publication. */
    struct PackageCacheEntry {
        Sha256Digest digest;
        std::uint64_t byteSize{};
        bool alreadyPresent{}; /**< True when an independently reverified entry already existed. */
    };

    /** @brief Safe persisted evidence describing bytes moved outside the active cache namespace. */
    struct PackageQuarantineRecord {
        std::string quarantineId;
        PackageQuarantineReason reason{};
        std::optional<Sha256Digest> expectedDigest;
        Sha256Digest actualDigest;
        std::uint64_t byteSize{};
    };

    /**
     * @brief Stores immutable verified archives by digest and isolates failed or corrupted bytes.
     *
     * Each digest mutation uses a host-provided process-safe lock. Returned archives own their
     * bytes, so a later cleanup cannot invalidate an active caller snapshot.
     */
    class PackageCacheStore {
    public:
        PackageCacheStore(const PackageCacheStore &) = delete;
        PackageCacheStore &operator=(const PackageCacheStore &) = delete;
        PackageCacheStore(PackageCacheStore &&) noexcept = default;
        PackageCacheStore &operator=(PackageCacheStore &&) = delete;

        /**
         * @brief Validates an absolute host-owned cache root and resource policy.
         * @param files Durable same-filesystem publication and locking service.
         * @param root Absolute cache root resolved by the host's user-directory service.
         * @param limits Bounds applied again whenever cache bytes are read.
         * @return Cache service or a typed configuration failure.
         */
        [[nodiscard]] static Result<PackageCacheStore> Create(DurableFileSystem &files, const std::filesystem::path &root,
                                                              const PackageValidationLimits &limits = {});

        /**
         * @brief Atomically publishes a previously validated immutable archive by its digest.
         * @param archive Complete verified archive snapshot.
         * @return Published entry; an existing entry is returned only after independent revalidation.
         */
        [[nodiscard]] Result<PackageCacheEntry> Publish(const ValidatedPackageArchive &archive);

        /**
         * @brief Loads and independently reverifies one content-addressed archive.
         * @param digest Requested immutable artifact identity.
         * @return Empty optional for a miss, owned verified bytes for a hit, or typed corruption/I/O failure.
         * @note A readable corrupt hit is moved to quarantine before this method returns failure.
         */
        [[nodiscard]] Result<std::optional<ValidatedPackageArchive>> Load(const Sha256Digest &digest);

        /**
         * @brief Durably isolates untrusted or failed download bytes with bounded safe diagnostics.
         * @param bytes Complete bytes to isolate; size is bounded by the configured archive limit.
         * @param reason Stable failure category selected by the verifying boundary.
         * @param expectedDigest Expected identity when one was known before verification.
         * @return Persisted quarantine record or a typed failure; no active cache entry is created.
         */
        [[nodiscard]] Result<PackageQuarantineRecord> Quarantine(std::span<const std::byte> bytes, PackageQuarantineReason reason,
                                                                 const std::optional<Sha256Digest> &expectedDigest = std::nullopt);

        /**
         * @brief Removes one active cache entry while holding its digest lock.
         * @param digest Artifact identity to remove.
         * @return True when an entry was removed, false for a cache miss, or a typed I/O/concurrency failure.
         */
        [[nodiscard]] Result<bool> Remove(const Sha256Digest &digest);

    private:
        /** @brief Constructs a store only after root and resource-policy validation. */
        PackageCacheStore(DurableFileSystem &files, std::filesystem::path root, const PackageValidationLimits &limits) noexcept;

        DurableFileSystem &files_;
        std::filesystem::path root_;
        PackageValidationLimits limits_;
    };
}  // namespace Horo::Packages
