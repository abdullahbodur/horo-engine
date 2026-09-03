#include "Horo/Extensions/ExtensionInventory.h"

#include "Horo/Extensions/ExtensionErrors.h"
#include "Horo/Foundation/Platform.h"
#include "Horo/Foundation/TransparentString.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <fstream>
#include <nlohmann/json.hpp>
#include <optional>
#include <ranges>
#include <span>
#include <sstream>
#include <unordered_map>

namespace Horo::Extensions {
    namespace {
        namespace fs = std::filesystem;
        using Json = nlohmann::json;

        constexpr ExtensionManifestLimits kManifestLimits{};
        constexpr std::size_t kMaximumPackageEntries = 4096;
        constexpr std::uintmax_t kMaximumPackageBytes = 1024ULL * 1024ULL * 1024ULL;

        /** @brief Tracks bounded resource consumption while staging a package tree. */
        struct PackageCopyBudget {
            std::size_t entryCount{};
            std::uintmax_t totalBytes{};
        };

        /** @brief Carries the validated relative path and filesystem type of one package entry. */
        struct ValidatedPackageEntry {
            fs::path relativePath;
            fs::file_status status;
        };

        /** @brief Preserves process-local activation state across inventory refreshes. */
        struct RuntimeState {
            bool active{};
            std::string loadError;
            std::string compositionVersion;
        };

        using RuntimeStateMap = TransparentStringMap<RuntimeState>;

        [[nodiscard]] std::string EnvironmentValue(const char *name) {
#if defined(_WIN32)
            std::size_t length = 0;
            if (getenv_s(&length, nullptr, 0, name) != 0 || length <= 1)
                return {};
            std::string value(length, '\0');
            if (getenv_s(&length, value.data(), value.size(), name) != 0 || length <= 1)
                return {};
            value.resize(length - 1);
            return value;
#else
            const char *value = std::getenv(name);
            return value != nullptr ? std::string{value} : std::string{};
#endif
        }

        [[nodiscard]] bool IsSafePackageId(const std::string_view value) {
            return !value.empty() && value != "." && value != ".." && std::ranges::all_of(value, [](const unsigned char character) {
                return std::isalnum(character) != 0 || character == '.' || character == '-' || character == '_';
            });
        }

        [[nodiscard]] bool IsPathContainedBy(const fs::path &root, const fs::path &candidate) {
            auto rootComponent = root.begin();
            auto candidateComponent = candidate.begin();
            for (; rootComponent != root.end(); ++rootComponent, ++candidateComponent) {
                if (candidateComponent == candidate.end() || *rootComponent != *candidateComponent)
                    return false;
            }
            return true;
        }

        [[nodiscard]] bool IsSafeRelativePath(const fs::path &relative) {
            if (relative.empty() || relative.is_absolute())
                return false;
            return std::ranges::none_of(relative, [](const fs::path &component) {
                return component == "..";
            });
        }

        [[nodiscard]] std::string BuildCompositionVersion(const std::string_view packageVersion,
                                                          const std::vector<ExtensionModuleManifest> &modules) {
            std::string fingerprint{packageVersion};
            for (const auto &mod : modules) {
                fingerprint.append("|");
                fingerprint.append(std::to_string(mod.id.size()));
                fingerprint.append(":");
                fingerprint.append(mod.id);
                fingerprint.append("@");
                fingerprint.append(mod.version);
            }
            return fingerprint;
        }

        [[nodiscard]] Result<ExtensionManifest> ReadManifest(const fs::path &absoluteManifestPath) {
            if (!absoluteManifestPath.is_absolute())
                return Result<ExtensionManifest>::Failure(
                    MakeError(ExtensionErrors::InvalidManifest, "Extension manifest path must be absolute."));
            std::error_code error;
            if (!fs::is_regular_file(absoluteManifestPath, error) || error ||
                fs::is_symlink(fs::symlink_status(absoluteManifestPath, error))) {
                return Result<ExtensionManifest>::Failure(
                    MakeError(ExtensionErrors::InvalidManifest, "Extension manifest must be a regular non-symlink file."));
            }
            if (const std::uintmax_t size = fs::file_size(absoluteManifestPath, error);
                error || size > kManifestLimits.maximumDocumentBytes)
                return Result<ExtensionManifest>::Failure(
                    MakeError(ExtensionErrors::InvalidManifest, "Extension manifest exceeds the bounded size."));
            std::ifstream input(absoluteManifestPath, std::ios::binary);
            std::ostringstream contents;
            contents << input.rdbuf();
            return ParseExtensionManifest(contents.str(), kManifestLimits);
        }

        /** @brief Validates one source entry and accounts for the package entry limit. */
        [[nodiscard]] Result<ValidatedPackageEntry> ValidatePackageEntry(const fs::directory_entry &entry, const fs::path &source,
                                                                         PackageCopyBudget &budget) {
            if (++budget.entryCount > kMaximumPackageEntries) {
                return Result<ValidatedPackageEntry>::Failure(
                    MakeError(ExtensionErrors::LoadFailed, "Extension package traversal failed or exceeded entry limits."));
            }
            std::error_code error;
            const fs::file_status status = entry.symlink_status(error);
            if (error || fs::is_symlink(status)) {
                return Result<ValidatedPackageEntry>::Failure(
                    MakeError(ExtensionErrors::LoadFailed, "Extension packages may not contain symlinks."));
            }
            const fs::path relative = fs::relative(entry.path(), source, error);
            if (error || !IsSafeRelativePath(relative)) {
                return Result<ValidatedPackageEntry>::Failure(
                    MakeError(ExtensionErrors::LoadFailed, "Extension package path escaped its source root."));
            }
            return Result<ValidatedPackageEntry>::Success({.relativePath = relative, .status = status});
        }

        /** @brief Copies one validated package entry while enforcing the aggregate byte limit. */
        [[nodiscard]] Result<void> CopyPackageEntry(const fs::directory_entry &entry, const fs::path &target, const fs::file_status &status,
                                                    PackageCopyBudget &budget) {
            std::error_code error;
            if (fs::is_directory(status)) {
                fs::create_directories(target, error);
            } else if (fs::is_regular_file(status)) {
                const std::uintmax_t size = fs::file_size(entry.path(), error);
                if (error || size > kMaximumPackageBytes - budget.totalBytes) {
                    return Result<void>::Failure(
                        MakeError(ExtensionErrors::LoadFailed, "Extension package exceeds the bounded byte size."));
                }
                budget.totalBytes += size;
                fs::create_directories(target.parent_path(), error);
                if (!error)
                    fs::copy_file(entry.path(), target, fs::copy_options::none, error);
            } else {
                return Result<void>::Failure(
                    MakeError(ExtensionErrors::LoadFailed, "Extension packages may contain only regular files and directories."));
            }
            if (error)
                return Result<void>::Failure(MakeError(ExtensionErrors::LoadFailed, "Unable to stage extension package contents."));
            return Result<void>::Success();
        }

        /** @brief Copies a bounded, symlink-free package tree into a staging directory. */
        [[nodiscard]] Result<void> CopyPackageTree(const fs::path &source, const fs::path &destination) {
            std::error_code error;
            fs::create_directories(destination, error);
            if (error)
                return Result<void>::Failure(
                    MakeError(ExtensionErrors::LoadFailed, "Unable to create extension installation staging directory."));

            PackageCopyBudget budget;
            fs::recursive_directory_iterator it{source, fs::directory_options::skip_permission_denied, error};
            const fs::recursive_directory_iterator end;
            while (!error && it != end) {
                auto validated = ValidatePackageEntry(*it, source, budget);
                if (validated.HasError())
                    return Result<void>::Failure(validated.ErrorValue());
                const ValidatedPackageEntry &packageEntry = validated.Value();
                if (auto copied = CopyPackageEntry(*it, destination / packageEntry.relativePath, packageEntry.status, budget);
                    copied.HasError())
                    return copied;
                it.increment(error);
            }
            if (error)
                return Result<void>::Failure(MakeError(ExtensionErrors::LoadFailed, "Unable to stage extension package contents."));
            return Result<void>::Success();
        }

        /** @brief Moves current runtime-only state out of inventory entries before rediscovery. */
        [[nodiscard]] RuntimeStateMap CaptureRuntimeStates(std::vector<ExtensionInventoryEntry> &entries) {
            RuntimeStateMap runtimeStates;
            runtimeStates.reserve(entries.size());
            for (auto &entry : entries) {
                runtimeStates.try_emplace(entry.packageId, RuntimeState{.active = entry.runtimeActive,
                                                                        .loadError = std::move(entry.loadError),
                                                                        .compositionVersion = std::move(entry.runtimeCompositionVersion)});
            }
            return runtimeStates;
        }

        /** @brief Restores runtime-only state onto newly discovered entries with matching identities. */
        void RestoreRuntimeStates(std::vector<ExtensionInventoryEntry> &entries, RuntimeStateMap &runtimeStates) {
            for (auto &entry : entries) {
                if (auto runtime = runtimeStates.find(entry.packageId); runtime != runtimeStates.end()) {
                    entry.runtimeActive = runtime->second.active;
                    entry.loadError = std::move(runtime->second.loadError);
                    entry.runtimeCompositionVersion = std::move(runtime->second.compositionVersion);
                }
            }
        }

        /** @brief Reports whether a directory entry is eligible for manifest discovery. */
        [[nodiscard]] bool IsDiscoverableDirectory(const fs::directory_entry &directory, std::error_code &error) {
            const fs::file_status status = directory.symlink_status(error);
            return !error && !fs::is_symlink(status) && fs::is_directory(status) &&
                   !directory.path().filename().string().starts_with(".install-");
        }

        /** @brief Builds one installed-package projection, or skips an invalid or duplicate package. */
        [[nodiscard]] std::optional<ExtensionInventoryEntry> ReadInstalledEntry(const fs::directory_entry &directory,
                                                                                const std::vector<ExtensionInventoryEntry> &existing,
                                                                                const TransparentStringSet &enabled,
                                                                                const TransparentStringSet &trusted) {
            auto parsed = ReadManifest(fs::absolute(directory.path() / "extension.json"));
            if (parsed.HasError())
                return std::nullopt;
            ExtensionManifest manifest = std::move(parsed).Value();
            if (const bool duplicate = std::ranges::any_of(existing,
                                                           [&manifest](const ExtensionInventoryEntry &entry) {
                return entry.packageId == manifest.id;
            });
                !IsSafePackageId(manifest.id) || directory.path().filename() != fs::path{manifest.id} || duplicate)
                return std::nullopt;
            const std::string compositionVersion = BuildCompositionVersion(manifest.version, manifest.modules);
            return ExtensionInventoryEntry{
                .packageId = manifest.id,
                .displayName = manifest.displayName.empty() ? manifest.id : manifest.displayName,
                .description = manifest.description,
                .version = manifest.version,
                .author = manifest.author,
                .origin = ExtensionOrigin::UserInstalled,
                .absoluteRootPath = fs::absolute(directory.path()).lexically_normal(),
                .absoluteManifestPath = fs::absolute(directory.path() / "extension.json").lexically_normal(),
                .modules = std::move(manifest.modules),
                .contributions = std::move(manifest.contributions),
                .enabled = enabled.contains(manifest.id),
                .locallyTrusted = trusted.contains(manifest.id),
                .compositionVersion = compositionVersion,
            };
        }

        /** @brief Adds valid installed packages from the managed root to the inventory projection. */
        [[nodiscard]] Result<void> DiscoverInstalledEntries(const fs::path &installRoot, std::vector<ExtensionInventoryEntry> &entries,
                                                            const TransparentStringSet &enabled, const TransparentStringSet &trusted) {
            std::error_code error;
            fs::create_directories(installRoot, error);
            if (error)
                return Result<void>::Failure(MakeError(ExtensionErrors::LoadFailed, "Unable to create the user extension directory."));
            for (const auto &directory : fs::directory_iterator(installRoot, error)) {
                if (error)
                    return Result<void>::Failure(MakeError(ExtensionErrors::LoadFailed, "Unable to enumerate installed extensions."));
                if (!IsDiscoverableDirectory(directory, error))
                    continue;
                if (auto entry = ReadInstalledEntry(directory, entries, enabled, trusted); entry.has_value())
                    entries.push_back(std::move(*entry));
            }
            return Result<void>::Success();
        }

        /** @brief Resolves and validates an installation source outside the managed root. */
        [[nodiscard]] Result<fs::path> ValidateInstallSource(const fs::path &absoluteSourceDirectory, const fs::path &installRoot) {
            if (!absoluteSourceDirectory.is_absolute())
                return Result<fs::path>::Failure(
                    MakeError(ExtensionErrors::InvalidManifest, "Extension source directory must be absolute."));
            std::error_code error;
            const fs::path source = fs::weakly_canonical(absoluteSourceDirectory, error);
            if (error || !fs::is_directory(source, error) || fs::is_symlink(fs::symlink_status(source, error))) {
                return Result<fs::path>::Failure(
                    MakeError(ExtensionErrors::InvalidManifest, "Extension source must be a regular non-symlink directory."));
            }
            if (const fs::path canonicalInstallRoot = fs::weakly_canonical(installRoot, error);
                !error && IsPathContainedBy(canonicalInstallRoot, source)) {
                return Result<fs::path>::Failure(
                    MakeError(ExtensionErrors::InvalidManifest, "Extension source must be outside the managed installation directory."));
            }
            return Result<fs::path>::Success(source);
        }

        /** @brief Removes an unpublished staging tree without masking the primary operation result. */
        void RemoveStagingTree(const fs::path &staging) noexcept {
            std::error_code ignored;
            fs::remove_all(staging, ignored);
        }

        /** @brief Copies and atomically publishes a validated package directory. */
        [[nodiscard]] Result<void> PublishPackage(const fs::path &source, const fs::path &staging, const fs::path &destination) {
            if (auto copied = CopyPackageTree(source, staging); copied.HasError()) {
                RemoveStagingTree(staging);
                return copied;
            }
            std::error_code error;
            fs::rename(staging, destination, error);
            if (error) {
                RemoveStagingTree(staging);
                return Result<void>::Failure(MakeError(ExtensionErrors::LoadFailed, "Unable to publish the staged extension package."));
            }
            return Result<void>::Success();
        }

        /** @brief Reads and validates the identity needed to install a package. */
        [[nodiscard]] Result<ExtensionManifest> ReadInstallManifest(const fs::path &source) {
            auto parsed = ReadManifest(source / "extension.json");
            if (parsed.HasError())
                return Result<ExtensionManifest>::Failure(parsed.ErrorValue());
            if (!IsSafePackageId(parsed.Value().id))
                return Result<ExtensionManifest>::Failure(
                    MakeError(ExtensionErrors::InvalidManifest, "Extension package ID is unsafe for installation."));
            return parsed;
        }

        /** @brief Creates the managed root and resolves an unused package destination. */
        [[nodiscard]] Result<fs::path> PrepareInstallDestination(const fs::path &installRoot, const std::string_view packageId) {
            std::error_code error;
            fs::create_directories(installRoot, error);
            if (error)
                return Result<fs::path>::Failure(MakeError(ExtensionErrors::LoadFailed, "Unable to create the user extension directory."));
            const fs::path destination = installRoot / packageId;
            if (fs::exists(destination, error) || error)
                return Result<fs::path>::Failure(
                    MakeError(ExtensionErrors::LoadFailed, "An extension with this package ID is already installed."));
            return Result<fs::path>::Success(destination);
        }

        /** @brief Inserts string values from one persisted JSON array into a state set. */
        void LoadStringSet(const Json &state, const std::string_view field, TransparentStringSet &values) {
            for (const auto &id : state.value(field, Json::array())) {
                if (id.is_string())
                    values.insert(id.get<std::string>());
            }
        }
    }  // namespace

    /** @copydoc ExtensionInventory::ExtensionInventory */
    ExtensionInventory::ExtensionInventory(fs::path absoluteInstallRoot)
        : installRoot_(absoluteInstallRoot.empty() ? DefaultInstallRoot() : std::move(absoluteInstallRoot)) {
        if (!installRoot_.is_absolute())
            installRoot_ = fs::absolute(installRoot_);
        installRoot_ = installRoot_.lexically_normal();
        statePath_ = installRoot_ / "_state.json";
    }

    /** @copydoc ExtensionInventory::DefaultInstallRoot */
    fs::path ExtensionInventory::DefaultInstallRoot() {
#if defined(_WIN32)
        fs::path home{EnvironmentValue("USERPROFILE")};
#else
        fs::path home{EnvironmentValue("HOME")};
#endif
        if (home.empty())
            home = fs::temp_directory_path() / "horo-user";  // NOSONAR(cpp:S5443) Fallback root when HOME is unset in tests.
        return fs::absolute(home / ".horo" / "extensions").lexically_normal();
    }

    /** @copydoc ExtensionInventory::Refresh */
    Result<void> ExtensionInventory::Refresh() {
        RuntimeStateMap runtimeStates = CaptureRuntimeStates(entries_);
        entries_.clear();
        if (auto loaded = LoadState(); loaded.HasError())
            return loaded;
        AddBuiltInPackages();
        if (auto discovered = DiscoverInstalledEntries(installRoot_, entries_, enabled_, trusted_); discovered.HasError())
            return discovered;
        RestoreRuntimeStates(entries_, runtimeStates);
        std::ranges::sort(entries_, [](const ExtensionInventoryEntry &left, const ExtensionInventoryEntry &right) {
            if (left.origin != right.origin)
                return left.origin == ExtensionOrigin::BuiltIn;
            return left.packageId < right.packageId;
        });
        return Result<void>::Success();
    }

    /** @copydoc ExtensionInventory::Entries */
    const std::vector<ExtensionInventoryEntry> &ExtensionInventory::Entries() const noexcept {
        return entries_;
    }

    /** @copydoc ExtensionInventory::InstallRoot */
    const fs::path &ExtensionInventory::InstallRoot() const noexcept {
        return installRoot_;
    }

    /** @copydoc ExtensionInventory::InstallFromDirectory */
    Result<std::string> ExtensionInventory::InstallFromDirectory(const fs::path &absoluteSourceDirectory) {
        auto validatedSource = ValidateInstallSource(absoluteSourceDirectory, installRoot_);
        if (validatedSource.HasError())
            return Result<std::string>::Failure(validatedSource.ErrorValue());
        const fs::path source = std::move(validatedSource).Value();
        auto parsed = ReadInstallManifest(source);
        if (parsed.HasError())
            return Result<std::string>::Failure(parsed.ErrorValue());
        const ExtensionManifest &manifest = parsed.Value();
        auto preparedDestination = PrepareInstallDestination(installRoot_, manifest.id);
        if (preparedDestination.HasError())
            return Result<std::string>::Failure(preparedDestination.ErrorValue());
        const fs::path destination = std::move(preparedDestination).Value();
        const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
        const fs::path staging = installRoot_ / std::format(".install-{}-{}", manifest.id, nonce);
        if (auto published = PublishPackage(source, staging, destination); published.HasError())
            return Result<std::string>::Failure(published.ErrorValue());
        enabled_.erase(manifest.id);
        trusted_.erase(manifest.id);
        if (auto saved = SaveState(); saved.HasError()) {
            std::error_code ignored;
            fs::remove_all(destination, ignored);
            return Result<std::string>::Failure(saved.ErrorValue());
        }
        if (auto refreshed = Refresh(); refreshed.HasError())
            return Result<std::string>::Failure(refreshed.ErrorValue());
        return Result<std::string>::Success(manifest.id);
    }

    /** @copydoc ExtensionInventory::SetEnabled */
    Result<void> ExtensionInventory::SetEnabled(const std::string_view packageId, const bool enabled) {
        const auto entry = std::ranges::find(entries_, packageId, &ExtensionInventoryEntry::packageId);
        if (entry == entries_.end())
            return Result<void>::Failure(MakeError(ExtensionErrors::InvalidManifest, "Unknown extension package ID."));
        const bool wasEnabled = enabled_.contains(entry->packageId);
        const bool wasTrusted = trusted_.contains(entry->packageId);
        if (enabled) {
            enabled_.insert(entry->packageId);
            trusted_.insert(entry->packageId);
        } else {
            enabled_.erase(entry->packageId);
        }
        if (auto saved = SaveState(); saved.HasError()) {
            if (wasEnabled)
                enabled_.insert(entry->packageId);
            else
                enabled_.erase(entry->packageId);
            if (wasTrusted)
                trusted_.insert(entry->packageId);
            else
                trusted_.erase(entry->packageId);
            return saved;
        }
        entry->enabled = enabled;
        entry->locallyTrusted = trusted_.contains(entry->packageId);
        return Result<void>::Success();
    }

    /** @copydoc ExtensionInventory::MarkRuntimeActive */
    void ExtensionInventory::MarkRuntimeActive(const std::string_view packageId) {
        if (auto entry = std::ranges::find(entries_, packageId, &ExtensionInventoryEntry::packageId); entry != entries_.end()) {
            entry->runtimeActive = true;
            entry->runtimeCompositionVersion = entry->compositionVersion;
            entry->loadError.clear();
        }
    }

    /** @copydoc ExtensionInventory::SetLoadError */
    void ExtensionInventory::SetLoadError(const std::string_view packageId, std::string message) {
        if (auto entry = std::ranges::find(entries_, packageId, &ExtensionInventoryEntry::packageId); entry != entries_.end()) {
            entry->runtimeActive = false;
            entry->loadError = std::move(message);
        }
    }

    /** @copydoc ExtensionInventory::EnabledUserPackageRoots */
    std::vector<fs::path> ExtensionInventory::EnabledUserPackageRoots() const {
        std::vector<fs::path> roots;
        for (const auto &entry : entries_) {
            if (entry.origin == ExtensionOrigin::UserInstalled && entry.enabled && entry.locallyTrusted &&
                entry.absoluteRootPath.is_absolute())
                roots.push_back(entry.absoluteRootPath);
        }
        return roots;
    }

    /** @copydoc ExtensionInventory::IsEnabled */
    bool ExtensionInventory::IsEnabled(const std::string_view packageId) const noexcept {
        return enabled_.contains(std::string{packageId});
    }

    Result<void> ExtensionInventory::LoadState() {
        enabled_.clear();
        trusted_.clear();
        if (std::error_code error; !fs::exists(statePath_, error) || error) {
            enabled_.emplace("horo.builtin.assets");
            trusted_.emplace("horo.builtin.assets");
            return Result<void>::Success();
        }
        std::ifstream input(statePath_, std::ios::binary);
        try {
            const Json state = Json::parse(input);
            LoadStringSet(state, "enabled", enabled_);
            LoadStringSet(state, "trusted", trusted_);
        } catch (const Json::exception &) {
            return Result<void>::Failure(MakeError(ExtensionErrors::InvalidManifest, "Extension activation state is malformed."));
        }
        return Result<void>::Success();
    }

    Result<void> ExtensionInventory::SaveState() const {
        std::error_code error;
        fs::create_directories(installRoot_, error);
        if (error)
            return Result<void>::Failure(MakeError(ExtensionErrors::LoadFailed, "Unable to create extension state directory."));
        std::vector<std::string> enabled{enabled_.begin(), enabled_.end()};
        std::vector<std::string> trusted{trusted_.begin(), trusted_.end()};
        std::ranges::sort(enabled);
        std::ranges::sort(trusted);
        const std::string serialized =
            Json{
                {"schemaVersion", 1},
                {"enabled", enabled},
                {"trusted", trusted},
            }
                .dump(2) +
            "\n";
        const fs::path prepared = statePath_.string() + ".tmp";
        NativeDurableFileSystem files;
        const auto bytes = std::as_bytes(std::span{serialized.data(), serialized.size()});
        if (auto written = files.WriteDurable(prepared, bytes); written.HasError())
            return written;
        return files.AtomicReplace(prepared, statePath_);
    }

    void ExtensionInventory::AddBuiltInPackages() {
        ExtensionInventoryEntry builtIn{
            .packageId = "horo.builtin.assets",
            .displayName = "Horo Asset Importers",
            .description = "Built-in OBJ and FBX mesh import, metadata, reimport, and preview providers.",
            .version = "1.0.0",
            .author = "Horo Engine",
            .origin = ExtensionOrigin::BuiltIn,
            .modules =
                {
                    {.id = "horo.builtin.assets.importer.obj", .version = "1.0.0", .kind = "asset_importer"},
                    {.id = "horo.builtin.assets.importer.fbx", .version = "1.0.0", .kind = "asset_importer"},
                },
            .contributions =
                {
                    {.type = "asset.importer", .id = "horo.asset-importer.obj-mesh", .owningModule = "horo.builtin.assets.importer.obj"},
                    {.type = "asset.importer", .id = "horo.asset-importer.fbx-mesh", .owningModule = "horo.builtin.assets.importer.fbx"},
                },
            .enabled = enabled_.contains("horo.builtin.assets"),
            .locallyTrusted = true,
        };
        builtIn.compositionVersion = BuildCompositionVersion(builtIn.version, builtIn.modules);
        entries_.push_back(std::move(builtIn));
    }
}  // namespace Horo::Extensions
