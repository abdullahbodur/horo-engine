#include "Horo/Packages/PackageCache.h"

#include <array>
#include <atomic>
#include <fstream>
#include <limits>
#include <nlohmann/json.hpp>

namespace Horo::Packages {
    namespace {
        const ErrorDomainId CacheDomain{"horo.packages.cache"};
        const ErrorCodeDescriptor InvalidConfiguration{CacheDomain, ErrorCode{"packages.cache.invalid_configuration"}, ErrorSeverity::Error,
                                                       "Package cache configuration is invalid.",
                                                       "Use an absolute cache root and nonzero resource limits."};
        const ErrorCodeDescriptor IoFailure{CacheDomain,
                                            ErrorCode{"packages.cache.io_failed"},
                                            ErrorSeverity::Error,
                                            "Package cache storage failed.",
                                            "Check cache permissions and available storage.",
                                            true,
                                            true};
        const ErrorCodeDescriptor Busy{CacheDomain,
                                       ErrorCode{"packages.cache.busy"},
                                       ErrorSeverity::Error,
                                       "Another package cache operation owns this artifact.",
                                       "Retry after the operation completes.",
                                       true};
        const ErrorCodeDescriptor Corrupt{CacheDomain,
                                          ErrorCode{"packages.cache.corrupt"},
                                          ErrorSeverity::Error,
                                          "A package cache entry failed integrity verification.",
                                          "Restore the package again from its authoritative source.",
                                          true,
                                          true};
        const ErrorCodeDescriptor InvalidReason{CacheDomain, ErrorCode{"packages.cache.invalid_quarantine_reason"}, ErrorSeverity::Error,
                                                "The quarantine reason is invalid.", "Use a declared package quarantine reason."};
        const ErrorCodeDescriptor ResourceLimit{CacheDomain, ErrorCode{"packages.cache.resource_limit"}, ErrorSeverity::Error,
                                                "Package cache input exceeds its configured resource limit.",
                                                "Reject the artifact or raise the host-owned bound deliberately."};
        std::atomic_uint64_t Sequence{0};

        [[nodiscard]] std::string DigestHex(const Sha256Digest &digest) {
            constexpr std::string_view Prefix{"sha256:"};
            return FormatSha256(digest).substr(Prefix.size());
        }

        [[nodiscard]] const char *ReasonName(const PackageQuarantineReason reason) noexcept {
            static constexpr std::array Names{"hash-mismatch", "invalid-archive", "verification-failure", "corrupt-cache-entry"};
            const auto index = static_cast<std::size_t>(reason);
            return index < Names.size() ? Names[index] : nullptr;
        }

        [[nodiscard]] std::filesystem::path ArchivePath(const std::filesystem::path &root, const Sha256Digest &digest) {
            return root / "by-hash" / "sha256" / DigestHex(digest) / "archive.horopkg";
        }

        [[nodiscard]] std::filesystem::path LockPath(const std::filesystem::path &root, const Sha256Digest &digest) {
            return root / "locks" / (DigestHex(digest) + ".lock");
        }

        [[nodiscard]] Error StorageError(const Error &cause, const std::string_view operation) {
            return WrapError(IoFailure, cause, "Package cache failed to " + std::string{operation} + '.');
        }

        [[nodiscard]] Result<ExclusiveFileLock> Acquire(DurableFileSystem &files, const std::filesystem::path &root,
                                                        const Sha256Digest &digest) {
            auto lock = files.TryAcquireExclusive(LockPath(root, digest), "horo-package-cache");
            if (lock.HasError())
                return Result<ExclusiveFileLock>::Failure(WrapError(Busy, lock.ErrorValue()));
            return lock;
        }

        [[nodiscard]] Result<std::vector<std::byte>> ReadBounded(const std::filesystem::path &path, const std::uint64_t limit) {
            std::error_code error;
            const auto status = std::filesystem::symlink_status(path, error);
            if (error || !std::filesystem::is_regular_file(status) || std::filesystem::is_symlink(status))
                return Result<std::vector<std::byte>>::Failure(MakeError(IoFailure, "Package cache entry is not a regular file."));
            const std::uintmax_t size = std::filesystem::file_size(path, error);
            if (error)
                return Result<std::vector<std::byte>>::Failure(MakeError(IoFailure, "Package cache entry size could not be read."));
            if (size > limit || size > std::numeric_limits<std::size_t>::max() ||
                size > static_cast<std::uintmax_t>(std::numeric_limits<std::streamsize>::max()))
                return Result<std::vector<std::byte>>::Failure(MakeError(ResourceLimit));
            std::vector<std::byte> bytes(static_cast<std::size_t>(size));
            std::ifstream stream(path, std::ios::binary);
            if (!stream || (size != 0 && !stream.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(size))))
                return Result<std::vector<std::byte>>::Failure(MakeError(IoFailure, "Package cache entry could not be read completely."));
            return Result<std::vector<std::byte>>::Success(std::move(bytes));
        }

        [[nodiscard]] Result<ValidatedPackageArchive> VerifyExpected(std::span<const std::byte> bytes, const Sha256Digest &expected,
                                                                     const PackageValidationLimits &limits) {
            auto archive = ValidatedPackageArchive::Verify(bytes, limits);
            if (archive.HasError())
                return archive;
            if (archive.Value().Digest() != expected)
                return Result<ValidatedPackageArchive>::Failure(MakeError(Corrupt));
            return archive;
        }

        [[nodiscard]] Result<void> MakeReadOnly(const std::filesystem::path &path) {
            std::error_code error;
            constexpr auto ReadOnly =
                std::filesystem::perms::owner_read | std::filesystem::perms::group_read | std::filesystem::perms::others_read;
            std::filesystem::permissions(path, ReadOnly, std::filesystem::perm_options::replace, error);
            return error ? Result<void>::Failure(MakeError(IoFailure, "Package cache could not restrict artifact permissions."))
                         : Result<void>::Success();
        }

        [[nodiscard]] Result<void> MakeOwnerWritable(const std::filesystem::path &path) {
            std::error_code error;
            std::filesystem::permissions(path, std::filesystem::perms::owner_write, std::filesystem::perm_options::add, error);
            return error ? Result<void>::Failure(MakeError(IoFailure, "Package cache could not prepare an entry for cleanup."))
                         : Result<void>::Success();
        }

        void BestEffortRemove(DurableFileSystem &files, const std::filesystem::path &path) {
            std::error_code ignored;
            std::filesystem::permissions(path, std::filesystem::perms::owner_write, std::filesystem::perm_options::add, ignored);
            static_cast<void>(files.RemoveDurable(path));
        }

        [[nodiscard]] std::string QuarantineJson(const PackageQuarantineRecord &record) {
            nlohmann::json document{{"schemaVersion", 1},
                                    {"quarantineId", record.quarantineId},
                                    {"reason", ReasonName(record.reason)},
                                    {"actualDigest", FormatSha256(record.actualDigest)},
                                    {"byteSize", record.byteSize}};
            document["expectedDigest"] =
                record.expectedDigest.has_value() ? nlohmann::json{FormatSha256(*record.expectedDigest)} : nlohmann::json{nullptr};
            return document.dump(2) + '\n';
        }

        [[nodiscard]] Result<std::string> AvailableQuarantineId(const std::filesystem::path &root, const char *reason,
                                                                const Sha256Digest &identity) {
            const std::string prefix = DigestHex(identity) + '-';
            constexpr std::uint32_t Attempts = 1024;
            for (std::uint32_t attempt = 0; attempt < Attempts; ++attempt) {
                const std::string candidate = prefix + std::to_string(++Sequence);
                std::error_code error;
                const bool exists = std::filesystem::exists(root / "quarantine" / reason / candidate, error);
                if (error)
                    return Result<std::string>::Failure(MakeError(IoFailure, "Package quarantine destination could not be inspected."));
                if (!exists)
                    return Result<std::string>::Success(candidate);
            }
            return Result<std::string>::Failure(MakeError(IoFailure, "Package quarantine identity capacity was exhausted."));
        }

        /** @brief Publishes the inert diagnostic before its quarantined artifact can become visible. */
        [[nodiscard]] Result<void> PublishQuarantineDiagnostic(DurableFileSystem &files, const std::filesystem::path &root,
                                                               const std::filesystem::path &diagnostic,
                                                               const PackageQuarantineRecord &record) {
            const std::filesystem::path staging = root / "staging" / (record.quarantineId + ".diagnostic.tmp");
            const std::string json = QuarantineJson(record);
            if (auto write = files.WriteDurable(staging, std::as_bytes(std::span{json})); write.HasError())
                return Result<void>::Failure(StorageError(write.ErrorValue(), "write quarantine diagnostics"));
            if (auto publish = files.AtomicReplace(staging, diagnostic); publish.HasError()) {
                BestEffortRemove(files, staging);
                return Result<void>::Failure(StorageError(publish.ErrorValue(), "publish quarantine diagnostics"));
            }
            return Result<void>::Success();
        }

        /** @brief Selects an existing corrupt entry or durably stages caller-provided failed bytes. */
        [[nodiscard]] Result<std::filesystem::path> PrepareQuarantineArtifact(DurableFileSystem &files, const std::filesystem::path &root,
                                                                              const std::string_view quarantineId,
                                                                              const std::span<const std::byte> bytes,
                                                                              const std::optional<std::filesystem::path> &existingPath) {
            if (existingPath.has_value())
                return Result<std::filesystem::path>::Success(*existingPath);
            const std::filesystem::path staging = root / "staging" / (std::string{quarantineId} + ".artifact.tmp");
            if (auto write = files.WriteDurable(staging, bytes); write.HasError())
                return Result<std::filesystem::path>::Failure(StorageError(write.ErrorValue(), "write quarantined bytes"));
            if (auto permissions = MakeReadOnly(staging); permissions.HasError()) {
                BestEffortRemove(files, staging);
                return Result<std::filesystem::path>::Failure(permissions.ErrorValue());
            }
            return Result<std::filesystem::path>::Success(staging);
        }

        [[nodiscard]] Result<PackageQuarantineRecord> QuarantineLocked(
            DurableFileSystem &files, const std::filesystem::path &root, std::span<const std::byte> bytes,
            const PackageQuarantineReason reason, const std::optional<Sha256Digest> expectedDigest,
            const std::optional<std::filesystem::path> existingPath = std::nullopt) {
            const char *reasonName = ReasonName(reason);
            const Sha256Digest actualDigest = ComputeSha256(bytes);
            const Sha256Digest identity = expectedDigest.value_or(actualDigest);
            auto quarantineId = AvailableQuarantineId(root, reasonName, identity);
            if (quarantineId.HasError())
                return Result<PackageQuarantineRecord>::Failure(quarantineId.ErrorValue());
            PackageQuarantineRecord record{.quarantineId = std::move(quarantineId).Value(),
                                           .reason = reason,
                                           .expectedDigest = expectedDigest,
                                           .actualDigest = actualDigest,
                                           .byteSize = bytes.size()};
            const std::filesystem::path destination = root / "quarantine" / reasonName / record.quarantineId;
            const std::filesystem::path diagnostic = destination / "diagnostic.json";
            if (auto publish = PublishQuarantineDiagnostic(files, root, diagnostic, record); publish.HasError())
                return Result<PackageQuarantineRecord>::Failure(publish.ErrorValue());
            auto prepared = PrepareQuarantineArtifact(files, root, record.quarantineId, bytes, existingPath);
            if (prepared.HasError()) {
                BestEffortRemove(files, diagnostic);
                return Result<PackageQuarantineRecord>::Failure(prepared.ErrorValue());
            }
            if (auto publish = files.AtomicReplace(prepared.Value(), destination / "artifact.horopkg"); publish.HasError()) {
                if (!existingPath.has_value())
                    BestEffortRemove(files, prepared.Value());
                BestEffortRemove(files, diagnostic);
                return Result<PackageQuarantineRecord>::Failure(StorageError(publish.ErrorValue(), "isolate quarantined bytes"));
            }
            return Result<PackageQuarantineRecord>::Success(std::move(record));
        }
    }  // namespace

    /** @copydoc PackageCacheStore::Create */
    Result<PackageCacheStore> PackageCacheStore::Create(DurableFileSystem &files, const std::filesystem::path &root,
                                                        const PackageValidationLimits &limits) {
        if (root.empty() || !root.is_absolute() || root != root.lexically_normal() || limits.archiveBytes == 0 ||
            limits.manifestBytes == 0 || limits.fileBytes == 0 || limits.expandedBytes == 0 || limits.entries == 0)
            return Result<PackageCacheStore>::Failure(MakeError(InvalidConfiguration));
        return Result<PackageCacheStore>::Success(PackageCacheStore{files, root, limits});
    }

    /** @copydoc PackageCacheStore::PackageCacheStore */
    PackageCacheStore::PackageCacheStore(DurableFileSystem &files, std::filesystem::path root,
                                         const PackageValidationLimits limits) noexcept
        : files_(files), root_(std::move(root)), limits_(limits) {}

    /** @copydoc PackageCacheStore::Publish */
    Result<PackageCacheEntry> PackageCacheStore::Publish(const ValidatedPackageArchive &archive) {
        const Sha256Digest digest = archive.Digest();
        auto lock = Acquire(files_, root_, digest);
        if (lock.HasError())
            return Result<PackageCacheEntry>::Failure(lock.ErrorValue());
        const std::filesystem::path destination = ArchivePath(root_, digest);
        std::error_code existsError;
        if (std::filesystem::exists(destination, existsError)) {
            auto bytes = ReadBounded(destination, limits_.archiveBytes);
            if (bytes.HasError())
                return Result<PackageCacheEntry>::Failure(bytes.ErrorValue());
            auto existing = VerifyExpected(bytes.Value(), digest, limits_);
            if (existing.HasValue())
                return Result<PackageCacheEntry>::Success({digest, bytes.Value().size(), true});
            auto quarantined =
                QuarantineLocked(files_, root_, bytes.Value(), PackageQuarantineReason::CorruptCacheEntry, digest, destination);
            if (quarantined.HasError())
                return Result<PackageCacheEntry>::Failure(WrapError(Corrupt, quarantined.ErrorValue()));
        } else if (existsError) {
            return Result<PackageCacheEntry>::Failure(MakeError(IoFailure, "Package cache destination could not be inspected."));
        }

        const std::filesystem::path staging = root_ / "staging" / (DigestHex(digest) + ".publish.tmp");
        if (auto write = files_.WriteDurable(staging, archive.Bytes()); write.HasError())
            return Result<PackageCacheEntry>::Failure(StorageError(write.ErrorValue(), "write a staged archive"));
        auto permissions = MakeReadOnly(staging);
        if (permissions.HasError()) {
            BestEffortRemove(files_, staging);
            return Result<PackageCacheEntry>::Failure(permissions.ErrorValue());
        }
        if (auto publish = files_.AtomicReplace(staging, destination); publish.HasError()) {
            BestEffortRemove(files_, staging);
            return Result<PackageCacheEntry>::Failure(StorageError(publish.ErrorValue(), "publish an archive atomically"));
        }
        auto publishedBytes = ReadBounded(destination, limits_.archiveBytes);
        if (publishedBytes.HasError())
            return Result<PackageCacheEntry>::Failure(publishedBytes.ErrorValue());
        auto verified = VerifyExpected(publishedBytes.Value(), digest, limits_);
        if (verified.HasError()) {
            auto quarantined =
                QuarantineLocked(files_, root_, publishedBytes.Value(), PackageQuarantineReason::CorruptCacheEntry, digest, destination);
            return Result<PackageCacheEntry>::Failure(quarantined.HasError() ? WrapError(Corrupt, quarantined.ErrorValue())
                                                                             : MakeError(Corrupt));
        }
        return Result<PackageCacheEntry>::Success({digest, publishedBytes.Value().size(), false});
    }

    /** @copydoc PackageCacheStore::Load */
    Result<std::optional<ValidatedPackageArchive>> PackageCacheStore::Load(const Sha256Digest &digest) {
        auto lock = Acquire(files_, root_, digest);
        if (lock.HasError())
            return Result<std::optional<ValidatedPackageArchive>>::Failure(lock.ErrorValue());
        const std::filesystem::path path = ArchivePath(root_, digest);
        std::error_code existsError;
        if (!std::filesystem::exists(path, existsError)) {
            if (existsError)
                return Result<std::optional<ValidatedPackageArchive>>::Failure(MakeError(IoFailure));
            return Result<std::optional<ValidatedPackageArchive>>::Success(std::nullopt);
        }
        auto bytes = ReadBounded(path, limits_.archiveBytes);
        if (bytes.HasError())
            return Result<std::optional<ValidatedPackageArchive>>::Failure(bytes.ErrorValue());
        auto archive = VerifyExpected(bytes.Value(), digest, limits_);
        if (archive.HasValue())
            return Result<std::optional<ValidatedPackageArchive>>::Success(
                std::optional<ValidatedPackageArchive>{std::move(archive).Value()});
        auto quarantined = QuarantineLocked(files_, root_, bytes.Value(), PackageQuarantineReason::CorruptCacheEntry, digest, path);
        return Result<std::optional<ValidatedPackageArchive>>::Failure(quarantined.HasError() ? WrapError(Corrupt, quarantined.ErrorValue())
                                                                                              : MakeError(Corrupt));
    }

    /** @copydoc PackageCacheStore::Quarantine */
    Result<PackageQuarantineRecord> PackageCacheStore::Quarantine(const std::span<const std::byte> bytes,
                                                                  const PackageQuarantineReason reason,
                                                                  const std::optional<Sha256Digest> expectedDigest) {
        if (bytes.size() > limits_.archiveBytes)
            return Result<PackageQuarantineRecord>::Failure(MakeError(ResourceLimit));
        if (ReasonName(reason) == nullptr)
            return Result<PackageQuarantineRecord>::Failure(MakeError(InvalidReason));
        const Sha256Digest identity = expectedDigest.value_or(ComputeSha256(bytes));
        auto lock = Acquire(files_, root_, identity);
        if (lock.HasError())
            return Result<PackageQuarantineRecord>::Failure(lock.ErrorValue());
        return QuarantineLocked(files_, root_, bytes, reason, expectedDigest);
    }

    /** @copydoc PackageCacheStore::Remove */
    Result<bool> PackageCacheStore::Remove(const Sha256Digest &digest) {
        auto lock = Acquire(files_, root_, digest);
        if (lock.HasError())
            return Result<bool>::Failure(lock.ErrorValue());
        const std::filesystem::path path = ArchivePath(root_, digest);
        std::error_code statusError;
        const auto status = std::filesystem::symlink_status(path, statusError);
        if (statusError == std::errc::no_such_file_or_directory || status.type() == std::filesystem::file_type::not_found)
            return Result<bool>::Success(false);
        if (statusError)
            return Result<bool>::Failure(MakeError(IoFailure));
        if (!std::filesystem::is_regular_file(status) || std::filesystem::is_symlink(status))
            return Result<bool>::Failure(MakeError(IoFailure, "Package cache cleanup rejected a non-regular entry."));
        if (auto permissions = MakeOwnerWritable(path); permissions.HasError())
            return Result<bool>::Failure(permissions.ErrorValue());
        if (auto remove = files_.RemoveDurable(path); remove.HasError())
            return Result<bool>::Failure(StorageError(remove.ErrorValue(), "remove an archive"));
        return Result<bool>::Success(true);
    }
}  // namespace Horo::Packages
