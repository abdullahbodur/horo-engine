#include "Horo/Extensions/ExtensionMarketplace.h"

#include "Horo/Extensions/ExtensionErrors.h"
#include "Horo/Extensions/ExtensionInventory.h"
#include "Horo/Foundation/Sha256.h"

#include <algorithm>
#include <chrono>
#include <curl/curl.h>
#include <fstream>
#include <miniz.h>
#include <nlohmann/json.hpp>
#include <random>
#include <ranges>
#include <span>
#include <system_error>

namespace Horo::Extensions {
    namespace {
        namespace fs = std::filesystem;
        using Json = nlohmann::json;

        constexpr std::size_t MaxRegistryBytes = 2ULL * 1024ULL * 1024ULL;
        constexpr std::size_t MaxPackageBytes = 256ULL * 1024ULL * 1024ULL;
        constexpr std::size_t MaxArchiveEntries = 4096;
        constexpr std::uint64_t MaxExtractedBytes = 512ULL * 1024ULL * 1024ULL;

        struct DownloadBuffer {
            std::vector<std::byte> bytes;
            std::size_t limit{};
            bool exceeded{};
        };

        [[nodiscard]] Error MarketplaceError(const char *message) {
            return MakeError(ExtensionErrors::LoadFailed, message);
        }

        [[nodiscard]] bool IsHttpsUrl(const std::string_view url) {
            return url.starts_with("https://") && url.size() > 8;
        }

        [[nodiscard]] std::string Lower(std::string value) {
            std::ranges::transform(value, value.begin(), [](const unsigned char value) {
                return static_cast<char>(std::tolower(value));
            });
            return value;
        }

        [[nodiscard]] bool Matches(const ExtensionMarketplaceEntry &entry, const std::string_view query) {
            if (query.empty())
                return true;
            const std::string needle = Lower(std::string{query});
            return Lower(entry.packageId).find(needle) != std::string::npos || Lower(entry.displayName).find(needle) != std::string::npos ||
                   Lower(entry.description).find(needle) != std::string::npos || Lower(entry.author).find(needle) != std::string::npos;
        }

        [[nodiscard]] std::string CurrentPlatformId() {
#if defined(_WIN32)
            return "windows-x64";
#elif defined(__APPLE__) && defined(__aarch64__)
            return "macos-arm64";
#elif defined(__APPLE__)
            return "macos-x64";
#elif defined(__aarch64__)
            return "linux-arm64";
#else
            return "linux-x64";
#endif
        }

        [[nodiscard]] bool SupportsCurrentPlatform(const Json &version) {
            if (!version.contains("platforms"))
                return true;
            if (!version["platforms"].is_array())
                return false;
            const std::string platform = CurrentPlatformId();
            return std::ranges::any_of(version["platforms"], [&platform](const Json &value) {
                return value.is_string() && value.get_ref<const std::string &>() == platform;
            });
        }

        std::size_t WriteDownload(const char *data, const std::size_t size, const std::size_t count, void *userData) {
            auto *buffer = static_cast<DownloadBuffer *>(userData);
            if (buffer == nullptr) {
                return 0;
            }
            const std::size_t byteCount = size * count;
            if (byteCount > buffer->limit - (std::min)(buffer->limit, buffer->bytes.size())) {
                buffer->exceeded = true;
                return 0;
            }
            const auto *begin = reinterpret_cast<const std::byte *>(data);
            buffer->bytes.insert(buffer->bytes.end(), begin, begin + byteCount);
            return byteCount;
        }

        int ReportTransfer(void *userData, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
            const auto *cancellation = static_cast<const CancellationToken *>(userData);
            if (cancellation == nullptr) {
                return 0;
            }
            return cancellation->IsCancellationRequested() ? 1 : 0;
        }

        [[nodiscard]] Result<std::vector<std::byte>> Download(const std::string &url, const std::size_t limit,
                                                              const CancellationToken &cancellation) {
            if (!IsHttpsUrl(url))
                return Result<std::vector<std::byte>>::Failure(MarketplaceError("Marketplace URLs must use HTTPS."));

            if (static const bool CurlReady = curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK; !CurlReady)
                return Result<std::vector<std::byte>>::Failure(MarketplaceError("Unable to initialise the HTTPS client."));

            CURL *curl = curl_easy_init();
            if (curl == nullptr)
                return Result<std::vector<std::byte>>::Failure(MarketplaceError("Unable to create the HTTPS request."));

            DownloadBuffer buffer{.limit = limit};
            curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
            curl_easy_setopt(curl, CURLOPT_USERAGENT, "horo-editor-extension-marketplace/1");
            curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
            curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
            curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "https");
            curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "https");
            curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
            curl_easy_setopt(curl, CURLOPT_TIMEOUT, 300L);
            curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteDownload);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffer);
            curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
            curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, ReportTransfer);
            curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &cancellation);
            const CURLcode result = curl_easy_perform(curl);
            curl_easy_cleanup(curl);

            if (result != CURLE_OK) {
                return Result<std::vector<std::byte>>::Failure(MarketplaceError(
                    buffer.exceeded ? "Marketplace response exceeded its bounded size." : "Marketplace HTTPS request failed."));
            }
            return Result<std::vector<std::byte>>::Success(std::move(buffer.bytes));
        }

        [[nodiscard]] Result<std::vector<ExtensionMarketplaceEntry>> ParseRegistry(const std::span<const std::byte> bytes,
                                                                                   const std::string_view query) {
            const Json root = Json::parse(reinterpret_cast<const char *>(bytes.data()),
                                          reinterpret_cast<const char *>(bytes.data() + bytes.size()), nullptr, false);
            if (root.is_discarded() || !root.is_object() || !root.contains("packages") || !root["packages"].is_array()) {
                return Result<std::vector<ExtensionMarketplaceEntry>>::Failure(MarketplaceError("Extension registry JSON is malformed."));
            }

            std::vector<ExtensionMarketplaceEntry> entries;
            for (const Json &package : root["packages"]) {
                if (!package.is_object() || !package.contains("id") || !package.contains("latest") || !package.contains("versions"))
                    continue;
                const std::string latest = package.value("latest", "");
                const Json &versions = package["versions"];
                if (latest.empty() || !versions.is_object() || !versions.contains(latest) || !versions[latest].is_object())
                    continue;
                const Json &version = versions[latest];
                if (!SupportsCurrentPlatform(version))
                    continue;
                ExtensionMarketplaceEntry entry{
                    .packageId = package.value("id", ""),
                    .displayName = package.value("displayName", package.value("id", "")),
                    .description = package.value("description", ""),
                    .author = package.value("publisher", ""),
                    .version = latest,
                    .packageUrl = version.value("packageUrl", ""),
                    .sha256 = version.value("sha256", ""),
                };
                if (entry.packageId.empty() || !IsHttpsUrl(entry.packageUrl) || ParseSha256(entry.sha256).HasError() ||
                    !Matches(entry, query))
                    continue;
                entries.push_back(std::move(entry));
            }
            std::ranges::sort(entries, {}, &ExtensionMarketplaceEntry::displayName);
            return Result<std::vector<ExtensionMarketplaceEntry>>::Success(std::move(entries));
        }

        [[nodiscard]] bool SafeArchivePath(const std::string_view name) {
            if (name.empty() || name.front() == '/' || name.find('\\') != std::string_view::npos)
                return false;
            fs::path path{name};
            if (path.is_absolute())
                return false;
            std::size_t depth = 0;
            for (const fs::path &component : path) {
                if (component == ".." || component == ".")
                    return false;
                if (++depth > 24)
                    return false;
            }
            return true;
        }

        [[nodiscard]] bool ExtractZipEntries(mz_zip_archive &archive, const mz_uint fileCount, const fs::path &extractionRoot,
                                             const CancellationToken &cancellation) {
            std::uint64_t extractedBytes = 0;
            std::error_code error;
            for (mz_uint index = 0; index < fileCount; ++index) {
                if (cancellation.IsCancellationRequested()) {
                    return false;
                }
                mz_zip_archive_file_stat stat{};
                if (mz_zip_reader_file_stat(&archive, index, &stat) == 0 || !SafeArchivePath(stat.m_filename)) {
                    return false;
                }
                extractedBytes += stat.m_uncomp_size;
                if (extractedBytes > MaxExtractedBytes) {
                    return false;
                }
                const fs::path target = (extractionRoot / fs::path{stat.m_filename}).lexically_normal();
                if (mz_zip_reader_is_file_a_directory(&archive, index)) {
                    fs::create_directories(target, error);
                    if (error) {
                        return false;
                    }
                } else {
                    fs::create_directories(target.parent_path(), error);
                    if (error || mz_zip_reader_extract_to_file(&archive, index, target.string().c_str(), 0) == 0) {
                        return false;
                    }
                }
            }
            return true;
        }

        [[nodiscard]] Result<fs::path> ResolvePackageRoot(const fs::path &extractionRoot) {
            std::error_code error;
            if (fs::is_regular_file(extractionRoot / "extension.json", error)) {
                return Result<fs::path>::Success(extractionRoot);
            }
            std::vector<fs::path> children;
            for (const auto &child : fs::directory_iterator(extractionRoot, error)) {
                if (error) {
                    break;
                }
                if (child.is_directory() && fs::is_regular_file(child.path() / "extension.json", error)) {
                    children.push_back(child.path());
                }
            }
            if (error || children.size() != 1) {
                return Result<fs::path>::Failure(MarketplaceError("Extension archive must contain one package manifest."));
            }
            return Result<fs::path>::Success(fs::absolute(children.front()).lexically_normal());
        }

        [[nodiscard]] bool IsSafeExtractionRoot(const fs::path &base, const fs::path &root) noexcept {
            std::error_code error;
            const fs::path canonicalBase = fs::weakly_canonical(base, error);
            if (error)
                return false;
            const fs::path canonicalRoot = fs::weakly_canonical(root, error);
            if (error || canonicalRoot.empty() || canonicalRoot.parent_path() != canonicalBase)
                return false;
            return canonicalRoot.filename().string().starts_with("horo-extension-marketplace-");
        }

        [[nodiscard]] std::uint64_t SecureNonce() {
            std::random_device random;
            std::uint64_t nonce{};
            for (std::size_t byte = 0; byte < sizeof(nonce); ++byte)
                nonce = (nonce << 8U) | (static_cast<std::uint64_t>(random()) & 0xffU);
            return nonce;
        }

        [[nodiscard]] Result<fs::path> ExtractPackage(const std::span<const std::byte> archiveBytes,
                                                      const CancellationToken &cancellation) {
            std::error_code error;
            // The child is created atomically, permission-restricted, name-validated, and never reused.
            const fs::path temporaryBase = fs::weakly_canonical(fs::temp_directory_path(error), error);  // NOSONAR(cpp:S5443)
            if (error || temporaryBase.empty())
                return Result<fs::path>::Failure(MarketplaceError("Unable to resolve extension extraction staging."));
            fs::path extractionRoot;
            for (int attempt = 0; attempt < 16; ++attempt) {
                extractionRoot = (temporaryBase / std::format("horo-extension-marketplace-{:016x}", SecureNonce())).lexically_normal();
                if (!IsSafeExtractionRoot(temporaryBase, extractionRoot))
                    continue;
                error.clear();
                if (fs::exists(extractionRoot, error))
                    continue;
                if (fs::create_directory(extractionRoot, error) && !error) {
                    fs::permissions(extractionRoot, fs::perms::owner_all, fs::perm_options::replace, error);
                    if (!error && IsSafeExtractionRoot(temporaryBase, extractionRoot))
                        break;
                    fs::remove_all(extractionRoot, error);
                }
                if (attempt == 15)
                    return Result<fs::path>::Failure(MarketplaceError("Unable to create extension extraction staging."));
            }
            if (!IsSafeExtractionRoot(temporaryBase, extractionRoot))
                return Result<fs::path>::Failure(MarketplaceError("Unable to create extension extraction staging."));

            mz_zip_archive archive{};
            if (!mz_zip_reader_init_mem(&archive, archiveBytes.data(), archiveBytes.size(), 0)) {
                fs::remove_all(extractionRoot, error);
                return Result<fs::path>::Failure(MarketplaceError("Downloaded extension is not a valid ZIP archive."));
            }

            const mz_uint fileCount = mz_zip_reader_get_num_files(&archive);
            const bool valid = (fileCount <= MaxArchiveEntries) && ExtractZipEntries(archive, fileCount, extractionRoot, cancellation);
            mz_zip_reader_end(&archive);
            if (!valid) {
                fs::remove_all(extractionRoot, error);
                return Result<fs::path>::Failure(MarketplaceError("Extension archive failed bounded path or size validation."));
            }

            auto packageRootResult = ResolvePackageRoot(extractionRoot);
            if (packageRootResult.HasError()) {
                fs::remove_all(extractionRoot, error);
                return packageRootResult;
            }
            return packageRootResult;
        }

        struct PreparedMarketplacePackage {
            fs::path installRoot;
            fs::path cleanupRoot;
        };

        [[nodiscard]] Result<PreparedMarketplacePackage> PrepareMarketplacePackage(const ExtensionMarketplaceEntry &entry,
                                                                                   const CancellationToken &cancellation) {
            auto downloaded = Download(entry.packageUrl, MaxPackageBytes, cancellation);
            if (downloaded.HasError())
                return Result<PreparedMarketplacePackage>::Failure(downloaded.ErrorValue());
            if (const std::string actual = FormatSha256(ComputeSha256(downloaded.Value())); actual != entry.sha256)
                return Result<PreparedMarketplacePackage>::Failure(MarketplaceError("Downloaded extension failed SHA-256 verification."));
            auto extracted = ExtractPackage(downloaded.Value(), cancellation);
            if (extracted.HasError())
                return Result<PreparedMarketplacePackage>::Failure(extracted.ErrorValue());
            fs::path installRoot = std::move(extracted).Value();
            fs::path cleanupRoot =
                installRoot.filename().string().starts_with("horo-extension-marketplace-") ? installRoot : installRoot.parent_path();
            return Result<PreparedMarketplacePackage>::Success({std::move(installRoot), std::move(cleanupRoot)});
        }
    }  // namespace

    /** @copydoc ParseExtensionMarketplaceRegistry */
    Result<std::vector<ExtensionMarketplaceEntry>> ParseExtensionMarketplaceRegistry(const std::string_view json,
                                                                                     const std::string_view query) {
        const auto *bytes = reinterpret_cast<const std::byte *>(json.data());
        return ParseRegistry(std::span<const std::byte>{bytes, json.size()}, query);
    }

    /** @copydoc ExtensionMarketplaceService::ExtensionMarketplaceService */
    ExtensionMarketplaceService::ExtensionMarketplaceService(JobSystem &jobs, ExtensionInventory &inventory, std::string registryUrl)
        : jobs_(jobs), inventory_(inventory), registryUrl_(std::move(registryUrl)) {}

    /** @copydoc ExtensionMarketplaceService::~ExtensionMarketplaceService */
    ExtensionMarketplaceService::~ExtensionMarketplaceService() {
        std::optional<JobHandle> job;
        std::optional<fs::path> cleanup;
        {
            std::lock_guard lock(mutex_);
            if (activeJob_.has_value()) {
                static_cast<void>(jobs_.RequestCancel(activeJob_->Id()));
                job = std::move(activeJob_);
            }
            cleanup = std::move(pendingCleanupRoot_);
            pendingCleanupRoot_.reset();
        }
        if (job.has_value())
            static_cast<void>(job->Wait());
        if (cleanup.has_value()) {
            std::error_code ignored;
            fs::remove_all(*cleanup, ignored);
        }
    }

    /** @copydoc ExtensionMarketplaceService::DefaultRegistryUrl */
    std::string ExtensionMarketplaceService::DefaultRegistryUrl() {
        return "https://raw.githubusercontent.com/horo-engine/extension-registry/main/registry.json";
    }

    /** @brief Reports whether the currently owned job still executes. */
    bool ExtensionMarketplaceService::OperationActiveLocked() const {
        return snapshot_.status == ExtensionMarketplaceStatus::Searching || snapshot_.status == ExtensionMarketplaceStatus::Installing;
    }

    /** @brief Publishes a terminal marketplace failure to presentation state. */
    void ExtensionMarketplaceService::FinishWithError(std::string message) {
        std::lock_guard lock(mutex_);
        snapshot_.status = ExtensionMarketplaceStatus::Error;
        snapshot_.message = std::move(message);
        snapshot_.activePackageId.clear();
    }

    /** @copydoc ExtensionMarketplaceService::Search */
    Result<void> ExtensionMarketplaceService::Search(std::string query) {
        std::lock_guard lock(mutex_);
        if (OperationActiveLocked())
            return Result<void>::Failure(MarketplaceError("Another marketplace operation is already active."));
        snapshot_.status = ExtensionMarketplaceStatus::Searching;
        snapshot_.message.clear();
        snapshot_.activePackageId.clear();
        const std::string registryUrl = registryUrl_;
        auto submitted = jobs_.SubmitResult({}, [this, registryUrl, query = std::move(query)](const CancellationToken &cancellation) {
            auto downloaded = Download(registryUrl, MaxRegistryBytes, cancellation);
            if (downloaded.HasError()) {
                FinishWithError(downloaded.ErrorValue().message);
                return Result<void>::Success();
            }
            auto parsed = ParseRegistry(downloaded.Value(), query);
            if (parsed.HasError()) {
                FinishWithError(parsed.ErrorValue().message);
                return Result<void>::Success();
            }
            std::lock_guard resultLock(mutex_);
            snapshot_.entries = std::move(parsed).Value();
            snapshot_.status = ExtensionMarketplaceStatus::Ready;
            snapshot_.message.clear();
            return Result<void>::Success();
        });
        if (submitted.HasError()) {
            snapshot_.status = ExtensionMarketplaceStatus::Error;
            snapshot_.message = submitted.ErrorValue().message;
            return Result<void>::Failure(submitted.ErrorValue());
        }
        activeJob_ = std::move(submitted).Value();
        return Result<void>::Success();
    }

    /** @copydoc ExtensionMarketplaceService::Install */
    Result<void> ExtensionMarketplaceService::Install(const std::string_view packageId) {
        std::lock_guard lock(mutex_);
        if (OperationActiveLocked())
            return Result<void>::Failure(MarketplaceError("Another marketplace operation is already active."));
        const auto found = std::ranges::find(snapshot_.entries, packageId, &ExtensionMarketplaceEntry::packageId);
        if (found == snapshot_.entries.end())
            return Result<void>::Failure(MarketplaceError("Marketplace package is not in the current search results."));
        const ExtensionMarketplaceEntry entry = *found;
        snapshot_.status = ExtensionMarketplaceStatus::Installing;
        snapshot_.activePackageId = entry.packageId;
        snapshot_.message.clear();
        auto submitted = jobs_.SubmitResult({}, [this, entry](const CancellationToken &cancellation) {
            auto prepared = PrepareMarketplacePackage(entry, cancellation);
            if (prepared.HasError()) {
                FinishWithError(prepared.ErrorValue().message);
                return Result<void>::Success();
            }
            PreparedMarketplacePackage package = std::move(prepared).Value();
            std::lock_guard resultLock(mutex_);
            pendingInstallRoot_ = std::move(package.installRoot);
            pendingCleanupRoot_ = std::move(package.cleanupRoot);
            return Result<void>::Success();
        });
        if (submitted.HasError()) {
            snapshot_.status = ExtensionMarketplaceStatus::Error;
            snapshot_.message = submitted.ErrorValue().message;
            return Result<void>::Failure(submitted.ErrorValue());
        }
        activeJob_ = std::move(submitted).Value();
        return Result<void>::Success();
    }

    /** @copydoc ExtensionMarketplaceService::Update */
    void ExtensionMarketplaceService::Update() {
        std::optional<fs::path> installRoot;
        std::optional<fs::path> cleanupRoot;
        {
            using enum JobState;
            std::lock_guard lock(mutex_);
            if (activeJob_.has_value()) {
                const JobState state = jobs_.Query(activeJob_->Id()).state;
                if (state == Succeeded || state == Failed || state == Cancelled)
                    activeJob_.reset();
            }
            if (pendingInstallRoot_.has_value()) {
                installRoot = std::move(pendingInstallRoot_);
                cleanupRoot = std::move(pendingCleanupRoot_);
                pendingInstallRoot_.reset();
                pendingCleanupRoot_.reset();
            }
        }
        if (!installRoot.has_value())
            return;

        Result<std::string> installed = inventory_.InstallFromDirectory(*installRoot);
        if (cleanupRoot.has_value()) {
            std::error_code ignored;
            fs::remove_all(*cleanupRoot, ignored);
        }
        std::lock_guard lock(mutex_);
        if (installed.HasError()) {
            snapshot_.status = ExtensionMarketplaceStatus::Error;
            snapshot_.message = installed.ErrorValue().message;
            snapshot_.activePackageId.clear();
            return;
        }
        snapshot_.status = ExtensionMarketplaceStatus::Installed;
        snapshot_.activePackageId = installed.Value();
        snapshot_.message = "Extension installed in a disabled state. Enable it from Installed.";
    }

    /** @copydoc ExtensionMarketplaceService::Snapshot */
    ExtensionMarketplaceSnapshot ExtensionMarketplaceService::Snapshot() const {
        std::lock_guard lock(mutex_);
        return snapshot_;
    }

    /** @copydoc ExtensionMarketplaceService::RegistryUrl */
    const std::string &ExtensionMarketplaceService::RegistryUrl() const noexcept {
        return registryUrl_;
    }
}  // namespace Horo::Extensions
