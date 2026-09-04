#pragma once

/** @file PackageArchive.h
 * @brief Immutable validated archive snapshot for later package installation transactions.
 */

#include "Horo/Packages/PackageFileManifest.h"

namespace Horo::Packages {
    /**
     * @brief Owns archive bytes whose complete regular-file inventory and contents were verified.
     *
     * This is not publisher trust, semantic TOML validation or permission to activate code.
     * Later bundle verification binds these digests to package intent and signature evidence.
     */
    class ValidatedPackageArchive {
    public:
        /**
         * @brief Copies and validates ZIP bytes without writing files or loading executable content.
         * @param bytes Complete archive; the caller must not mutate it concurrently during this call.
         * @param limits Host-owned resource ceilings, applied before decompression.
         * @return Immutable validated snapshot or typed failure; no partial filesystem state is created.
         */
        [[nodiscard]] static Result<ValidatedPackageArchive> Verify(std::span<const std::byte> bytes,
                                                                    const PackageValidationLimits &limits = {});

        /** @brief Returns the validated inventory. @return Borrowed immutable inventory. */
        [[nodiscard]] const ValidatedPackageFileManifestV1 &Manifest() const noexcept;

        /** @brief Returns the verified snapshot, not the caller's original buffer. @return Borrowed archive bytes. */
        [[nodiscard]] std::span<const std::byte> Bytes() const noexcept;

        /** @brief Returns the exact archive digest for later signature binding. @return Archive SHA-256 digest. */
        [[nodiscard]] const Sha256Digest &Digest() const noexcept;

    private:
        /** @brief Constructs only after checking the complete inventory and every file's actual content. */
        ValidatedPackageArchive(std::vector<std::byte> bytes, ValidatedPackageFileManifestV1 manifest, const Sha256Digest &digest);

        std::vector<std::byte> m_bytes;
        ValidatedPackageFileManifestV1 m_manifest;
        Sha256Digest m_digest;
    };
}  // namespace Horo::Packages
