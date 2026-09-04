#pragma once

/** @file PackageFileManifest.h
 * @brief Bounded immutable package file inventory, separate from package intent and trust.
 */

#include "Horo/Foundation/Sha256.h"
#include "Horo/Packages/PackagePath.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace Horo::Packages {
    /** @brief Resource ceilings applied before allocating or decompressing untrusted package data. */
    struct PackageValidationLimits {
        std::uint64_t archiveBytes = 64 * 1024 * 1024;
        std::uint64_t manifestBytes = 4 * 1024 * 1024;
        std::uint64_t fileBytes = 64 * 1024 * 1024;
        std::uint64_t expandedBytes = 256 * 1024 * 1024;
        std::uint32_t entries = 4096; /**< Includes explicit directory and inventory entries in an archive. */
    };

    /** @brief File identity domain within one package, never a host filesystem path. */
    struct PackageFileId {
        PackagePath path;
    };

    /** @brief Validated inventory metadata; hashes are author claims until compared against archive bytes. */
    struct PackageFileEntry {
        PackageFileId id;
        Sha256Digest digest;
        std::uint64_t size = 0;
        bool executable = false;
        std::optional<PackagePath> contributionRoot; /**< Explicit absence for package-level metadata. */
    };

    /** @brief Immutable schema-validated inventory; it does not prove archive integrity or publisher trust. */
    class ValidatedPackageFileManifestV1 {
    public:
        /**
         * @brief Parses strict JSON v1 with bounded depth, counts, sizes and canonical path identities.
         * @param json Complete files.manifest.json bytes; duplicate and unknown fields reject the input.
         * @param limits Caller-owned resource policy.
         * @return Complete inventory or a typed failure, never a partially validated value.
         */
        [[nodiscard]] static Result<ValidatedPackageFileManifestV1> Parse(std::string_view json,
                                                                          const PackageValidationLimits &limits = {});

        /** @brief Returns the immutable file list. @return Borrowed entries in manifest order. */
        [[nodiscard]] std::span<const PackageFileEntry> Entries() const noexcept;

        /** @brief Returns the digest of the exact input bytes. @return File-manifest SHA-256 digest. */
        [[nodiscard]] const Sha256Digest &Digest() const noexcept;

    private:
        /** @brief Constructs only after complete inventory validation. */
        ValidatedPackageFileManifestV1(std::vector<PackageFileEntry> entries, const Sha256Digest &digest);

        std::vector<PackageFileEntry> m_entries;
        Sha256Digest m_digest;
    };
}  // namespace Horo::Packages
