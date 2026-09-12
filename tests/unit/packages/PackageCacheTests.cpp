#include "Horo/Packages/PackageCache.h"
#include "PackageArchiveTestSupport.h"

#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <fstream>
#include <miniz.h>
#include <nlohmann/json.hpp>

namespace {
    using Horo::Packages::PackageCacheStore;
    using Horo::Packages::PackageQuarantineReason;
    using Horo::Packages::ValidatedPackageArchive;

    std::atomic_uint64_t TemporarySequence{0};

    class TemporaryDirectory final {
    public:
        TemporaryDirectory()
            : path_(std::filesystem::temp_directory_path() /
                    ("horo-package-cache-tests-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + '-' +
                     std::to_string(++TemporarySequence))) {
            std::filesystem::create_directories(path_);
        }

        ~TemporaryDirectory() {
            std::error_code error;
            for (std::filesystem::recursive_directory_iterator
                     iterator{path_, std::filesystem::directory_options::skip_permission_denied, error},
                 end;
                 !error && iterator != end; iterator.increment(error)) {
                std::filesystem::permissions(iterator->path(), std::filesystem::perms::owner_all, std::filesystem::perm_options::add,
                                             error);
                error.clear();
            }
            std::filesystem::remove_all(path_, error);
        }

        [[nodiscard]] const std::filesystem::path &Path() const noexcept {
            return path_;
        }

    private:
        std::filesystem::path path_;
    };

    struct File {
        std::string name;
        std::string content;
    };

    [[nodiscard]] std::vector<std::byte> ArchiveBytes() {
        const std::vector<File> files{{"horo-package.toml", "schemaVersion = 1\n"}, {"assets/data.bin", "verified bytes"}};
        nlohmann::json entries = nlohmann::json::array();
        for (const auto &file : files) {
            entries.push_back(Horo::Tests::Packages::FileInventoryEntry(file.name, file.content));
        }
        const std::string inventory = nlohmann::json{{"schemaVersion", 1}, {"files", entries}}.dump();

        mz_zip_archive zip{};
        REQUIRE(mz_zip_writer_init_heap(&zip, 0, 0));
        for (const auto &file : files)
            REQUIRE(mz_zip_writer_add_mem(&zip, file.name.c_str(), file.content.data(), file.content.size(), MZ_BEST_COMPRESSION));
        REQUIRE(mz_zip_writer_add_mem(&zip, "files.manifest.json", inventory.data(), inventory.size(), MZ_BEST_COMPRESSION));
        return Horo::Tests::Packages::FinalizeArchive(zip);
    }

    [[nodiscard]] ValidatedPackageArchive VerifiedArchive() {
        auto result = ValidatedPackageArchive::Verify(ArchiveBytes());
        REQUIRE(result.HasValue());
        return std::move(result).Value();
    }

    [[nodiscard]] std::string DigestHex(const Horo::Sha256Digest &digest) {
        return Horo::FormatSha256(digest).substr(7);
    }

    [[nodiscard]] std::filesystem::path ActivePath(const std::filesystem::path &root, const Horo::Sha256Digest &digest) {
        return root / "by-hash" / "sha256" / DigestHex(digest) / "archive.horopkg";
    }

    void CheckOwnerReadOnly(const std::filesystem::path &path) {
        const auto permissions = std::filesystem::status(path).permissions();
        CHECK((permissions & std::filesystem::perms::owner_read) != std::filesystem::perms::none);
        CHECK((permissions & (std::filesystem::perms::owner_write | std::filesystem::perms::group_all |
                              std::filesystem::perms::others_all)) == std::filesystem::perms::none);
    }

    const Horo::ErrorCodeDescriptor InjectedPermissionFailure{
        .domain = Horo::ErrorDomainId{"test.package-cache"},
        .code = Horo::ErrorCode{"test.permission_denied"},
        .defaultSeverity = Horo::ErrorSeverity::Error,
        .summary = "Injected permission failure.",
    };

    class DenyingFileSystem final : public Horo::DurableFileSystem {
    public:
        enum class Failure {
            Write,
            Replace
        };

        DenyingFileSystem(Horo::NativeDurableFileSystem &native, const Failure failure) : native_(native), failure_(failure) {}

        Horo::Result<Horo::ExclusiveFileLock> TryAcquireExclusive(const std::filesystem::path &path,
                                                                  const std::string_view owner) override {
            return native_.TryAcquireExclusive(path, owner);
        }

        Horo::Result<std::uint64_t> AvailableBytes(const std::filesystem::path &path) const override {
            return native_.AvailableBytes(path);
        }

        Horo::Result<void> WriteDurable(const std::filesystem::path &path, const std::span<const std::byte> bytes) override {
            if (failure_ == Failure::Write)
                return Horo::Result<void>::Failure(Horo::MakeError(InjectedPermissionFailure));
            return native_.WriteDurable(path, bytes);
        }

        Horo::Result<void> CopyDurable(const std::filesystem::path &source, const std::filesystem::path &destination) override {
            return native_.CopyDurable(source, destination);
        }

        Horo::Result<void> AtomicReplace(const std::filesystem::path &prepared, const std::filesystem::path &destination) override {
            if (failure_ == Failure::Replace)
                return Horo::Result<void>::Failure(Horo::MakeError(InjectedPermissionFailure));
            return native_.AtomicReplace(prepared, destination);
        }

        Horo::Result<void> RemoveDurable(const std::filesystem::path &path) override {
            return native_.RemoveDurable(path);
        }

        Horo::Result<void> SyncDirectory(const std::filesystem::path &path) override {
            return native_.SyncDirectory(path);
        }

    private:
        Horo::NativeDurableFileSystem &native_;
        Failure failure_;
    };

    [[nodiscard]] PackageCacheStore CreateStore(Horo::DurableFileSystem &files, const std::filesystem::path &root,
                                                const Horo::Packages::PackageValidationLimits &limits = {}) {
        auto result = PackageCacheStore::Create(files, root, limits);
        REQUIRE(result.HasValue());
        return std::move(result).Value();
    }

    class CacheFixture final {
    public:
        CacheFixture() : store(CreateStore(files, temporary.Path())) {}

        TemporaryDirectory temporary;
        Horo::NativeDurableFileSystem files;
        PackageCacheStore store;
    };

    TEST_CASE("Package cache atomically publishes, reverifies, snapshots and cleans entries", "[packages][cache]") {
        CacheFixture fixture;
        auto archive = VerifiedArchive();
        const auto digest = archive.Digest();

        const auto first = fixture.store.Publish(archive);
        REQUIRE(first.HasValue());
        CHECK_FALSE(first.Value().alreadyPresent);
        CHECK(first.Value().digest == digest);
        CheckOwnerReadOnly(ActivePath(fixture.temporary.Path(), digest));

        const auto second = fixture.store.Publish(archive);
        REQUIRE(second.HasValue());
        CHECK(second.Value().alreadyPresent);
        auto loaded = fixture.store.Load(digest);
        REQUIRE(loaded.HasValue());
        REQUIRE(loaded.Value().has_value());
        CHECK(loaded.Value()->Digest() == digest);

        REQUIRE(fixture.store.Remove(digest).Value());
        CHECK_FALSE(std::filesystem::exists(ActivePath(fixture.temporary.Path(), digest)));
        auto missing = fixture.store.Load(digest);
        REQUIRE(missing.HasValue());
        CHECK_FALSE(missing.Value().has_value());
        CHECK_FALSE(fixture.store.Remove(digest).Value());
    }

    TEST_CASE("Package cache quarantines a poisoned hit with safe persisted diagnostics", "[packages][cache][quarantine]") {
        CacheFixture fixture;
        auto archive = VerifiedArchive();
        const auto digest = archive.Digest();
        REQUIRE(fixture.store.Publish(archive).HasValue());

        const auto path = ActivePath(fixture.temporary.Path(), digest);
        std::filesystem::permissions(path, std::filesystem::perms::owner_write, std::filesystem::perm_options::add);
        std::fstream stream(path, std::ios::in | std::ios::out | std::ios::binary);
        REQUIRE(stream);
        stream.seekp(0);
        stream.put('x');
        stream.close();

        const auto loaded = fixture.store.Load(digest);
        REQUIRE(loaded.HasError());
        CHECK(loaded.ErrorValue().code.Value() == "packages.cache.corrupt");
        CHECK_FALSE(std::filesystem::exists(path));
        const auto quarantineRoot = fixture.temporary.Path() / "quarantine" / "corrupt-cache-entry";
        REQUIRE(std::filesystem::is_directory(quarantineRoot));
        const std::filesystem::directory_iterator records{quarantineRoot};
        const auto recordDirectory = records->path();
        CHECK(std::filesystem::is_regular_file(recordDirectory / "artifact.horopkg"));
        std::ifstream diagnostic{recordDirectory / "diagnostic.json"};
        const nlohmann::json document = nlohmann::json::parse(diagnostic);
        CHECK(document["reason"] == "corrupt-cache-entry");
        CHECK(document["expectedDigest"] == Horo::FormatSha256(digest));
        CHECK(document["actualDigest"] != document["expectedDigest"]);
    }

    TEST_CASE("Package cache isolates failed downloads without publishing them", "[packages][cache][quarantine]") {
        CacheFixture fixture;
        const auto bytes = ArchiveBytes();
        const auto expected = Horo::ComputeSha256({});

        const auto result = fixture.store.Quarantine(bytes, PackageQuarantineReason::HashMismatch, expected);
        REQUIRE(result.HasValue());
        CHECK(result.Value().reason == PackageQuarantineReason::HashMismatch);
        CHECK(result.Value().expectedDigest == expected);
        CHECK(result.Value().actualDigest == Horo::ComputeSha256(bytes));
        CHECK_FALSE(std::filesystem::exists(ActivePath(fixture.temporary.Path(), expected)));
        const auto artifact = fixture.temporary.Path() / "quarantine" / "hash-mismatch" / result.Value().quarantineId / "artifact.horopkg";
        CHECK(std::filesystem::is_regular_file(artifact));
        CheckOwnerReadOnly(artifact);
    }

    TEST_CASE("Package cache returns busy while publication or cleanup owns the digest lock", "[packages][cache][concurrency]") {
        CacheFixture fixture;
        auto archive = VerifiedArchive();
        const auto lockPath = fixture.temporary.Path() / "locks" / (DigestHex(archive.Digest()) + ".lock");
        auto held = fixture.files.TryAcquireExclusive(lockPath, "test-owner");
        REQUIRE(held.HasValue());

        const auto publish = fixture.store.Publish(archive);
        REQUIRE(publish.HasError());
        CHECK(publish.ErrorValue().code.Value() == "packages.cache.busy");
        const auto remove = fixture.store.Remove(archive.Digest());
        REQUIRE(remove.HasError());
        CHECK(remove.ErrorValue().code.Value() == "packages.cache.busy");
    }

    TEST_CASE("Package cache preserves the active namespace on permission and atomic replacement failures", "[packages][cache]") {
        const auto archive = VerifiedArchive();
        for (const auto failure : {DenyingFileSystem::Failure::Write, DenyingFileSystem::Failure::Replace}) {
            TemporaryDirectory temporary;
            Horo::NativeDurableFileSystem native;
            DenyingFileSystem files{native, failure};
            auto store = CreateStore(files, temporary.Path());
            const auto result = store.Publish(archive);
            REQUIRE(result.HasError());
            CHECK(result.ErrorValue().code.Value() == "packages.cache.io_failed");
            REQUIRE(result.ErrorValue().cause);
            CHECK(result.ErrorValue().cause.Get()->code.Value() == "test.permission_denied");
            CHECK_FALSE(std::filesystem::exists(ActivePath(temporary.Path(), archive.Digest())));
        }
    }

    TEST_CASE("Package cache bounds quarantine input and rejects noncanonical roots", "[packages][cache]") {
        TemporaryDirectory temporary;
        Horo::NativeDurableFileSystem files;
        CHECK(PackageCacheStore::Create(files, std::filesystem::path{"relative/cache"}).HasError());
        auto store = CreateStore(files, temporary.Path(), {.archiveBytes = 1});
        const auto archive = VerifiedArchive();
        const auto publish = store.Publish(archive);
        REQUIRE(publish.HasError());
        CHECK(publish.ErrorValue().code.Value() == "packages.validation.limit");
        CHECK_FALSE(std::filesystem::exists(ActivePath(temporary.Path(), archive.Digest())));
        const std::array bytes{std::byte{1}, std::byte{2}};
        const auto result = store.Quarantine(bytes, PackageQuarantineReason::InvalidArchive);
        REQUIRE(result.HasError());
        CHECK(result.ErrorValue().code.Value() == "packages.cache.resource_limit");
    }
}  // namespace
