#include "Horo/Extensions/ExtensionInventory.h"

#include "Horo/Extensions/ExtensionErrors.h"
#include "Horo/Foundation/Platform.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <fstream>
#include <nlohmann/json.hpp>
#include <ranges>
#include <span>
#include <sstream>
#include <unordered_map>

namespace Horo::Extensions {
    namespace {
        namespace fs = std::filesystem;
        using Json = nlohmann::json;

        constexpr std::uintmax_t kMaximumManifestBytes = 1024U * 1024U;
        constexpr std::size_t kMaximumPackageEntries = 4096;
        constexpr std::uintmax_t kMaximumPackageBytes = 1024ULL * 1024ULL * 1024ULL;

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
            if (const std::uintmax_t size = fs::file_size(absoluteManifestPath, error); error || size > kMaximumManifestBytes)
                return Result<ExtensionManifest>::Failure(
                    MakeError(ExtensionErrors::InvalidManifest, "Extension manifest exceeds the bounded size."));
            std::ifstream input(absoluteManifestPath, std::ios::binary);
            std::ostringstream contents;
            contents << input.rdbuf();
            return ParseExtensionManifest(contents.str());
        }

        [[nodiscard]] Result<void> CopyPackageTree(const fs::path &source, const fs::path &destination) {
            std::error_code error;
            fs::create_directories(destination, error);
            if (error)
                return Result<void>::Failure(
                    MakeError(ExtensionErrors::LoadFailed, "Unable to create extension installation staging directory."));

            std::size_t entryCount = 0;
            std::uintmax_t totalBytes = 0;
            for (fs::recursive_directory_iterator it{source, fs::directory_options::skip_permission_denied, error}, end; it != end;
                 it.increment(error)) {
                ++entryCount;
                if (error || entryCount > kMaximumPackageEntries)
                    return Result<void>::Failure(
                        MakeError(ExtensionErrors::LoadFailed, "Extension package traversal failed or exceeded entry limits."));
                const fs::file_status status = it->symlink_status(error);
                if (error || fs::is_symlink(status))
                    return Result<void>::Failure(MakeError(ExtensionErrors::LoadFailed, "Extension packages may not contain symlinks."));
                const fs::path relative = fs::relative(it->path(), source, error);
                if (error || relative.empty() || relative.is_absolute() || std::ranges::any_of(relative, [](const fs::path &component) {
                    return component == "..";
                })) {
                    return Result<void>::Failure(MakeError(ExtensionErrors::LoadFailed, "Extension package path escaped its source root."));
                }
                const fs::path target = destination / relative;
                if (fs::is_directory(status)) {
                    fs::create_directories(target, error);
                } else if (fs::is_regular_file(status)) {
                    const std::uintmax_t size = fs::file_size(it->path(), error);
                    if (error || size > kMaximumPackageBytes - totalBytes)
                        return Result<void>::Failure(
                            MakeError(ExtensionErrors::LoadFailed, "Extension package exceeds the bounded byte size."));
                    totalBytes += size;
                    fs::create_directories(target.parent_path(), error);
                    if (!error)
                        fs::copy_file(it->path(), target, fs::copy_options::none, error);
                } else {
                    return Result<void>::Failure(
                        MakeError(ExtensionErrors::LoadFailed, "Extension package contains an unsupported filesystem entry."));
                }
                if (error)
                    return Result<void>::Failure(MakeError(ExtensionErrors::LoadFailed, "Unable to stage extension package contents."));
            }
            return Result<void>::Success();
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
            home = fs::temp_directory_path() / "horo-user";
        return fs::absolute(home / ".horo" / "extensions").lexically_normal();
    }

    /** @copydoc ExtensionInventory::Refresh */
    Result<void> ExtensionInventory::Refresh() {
        struct RuntimeState {
            bool active{};
            std::string loadError;
            std::string compositionVersion;
        };

        std::unordered_map<std::string, RuntimeState> runtimeStates;
        runtimeStates.reserve(entries_.size());
        for (auto &entry : entries_) {
            runtimeStates.try_emplace(entry.packageId, RuntimeState{.active = entry.runtimeActive,
                                                                    .loadError = std::move(entry.loadError),
                                                                    .compositionVersion = std::move(entry.runtimeCompositionVersion)});
        }
        entries_.clear();
        if (auto loaded = LoadState(); loaded.HasError())
            return loaded;
        AddBuiltInPackages();

        std::error_code error;
        fs::create_directories(installRoot_, error);
        if (error)
            return Result<void>::Failure(MakeError(ExtensionErrors::LoadFailed, "Unable to create the user extension directory."));
        for (const auto &directory : fs::directory_iterator(installRoot_, error)) {
            if (error)
                return Result<void>::Failure(MakeError(ExtensionErrors::LoadFailed, "Unable to enumerate installed extensions."));
            if (const fs::file_status status = directory.symlink_status(error);
                error || fs::is_symlink(status) || !fs::is_directory(status) ||
                directory.path().filename().string().starts_with(".install-"))
                continue;
            auto parsed = ReadManifest(fs::absolute(directory.path() / "extension.json"));
            if (parsed.HasError())
                continue;
            ExtensionManifest manifest = std::move(parsed).Value();
            if (!IsSafePackageId(manifest.id) || directory.path().filename() != fs::path{manifest.id} ||
                std::ranges::any_of(entries_, [&manifest](const ExtensionInventoryEntry &entry) {
                return entry.packageId == manifest.id;
            }))
                continue;
            const std::string compositionVersion = BuildCompositionVersion(manifest.version, manifest.modules);
            entries_.push_back(ExtensionInventoryEntry{
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
                .enabled = enabled_.contains(manifest.id),
                .locallyTrusted = trusted_.contains(manifest.id),
                .compositionVersion = compositionVersion,
            });
        }
        for (auto &entry : entries_) {
            if (auto runtime = runtimeStates.find(entry.packageId); runtime != runtimeStates.end()) {
                entry.runtimeActive = runtime->second.active;
                entry.loadError = std::move(runtime->second.loadError);
                entry.runtimeCompositionVersion = std::move(runtime->second.compositionVersion);
            }
        }
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
        if (!absoluteSourceDirectory.is_absolute())
            return Result<std::string>::Failure(
                MakeError(ExtensionErrors::InvalidManifest, "Extension source directory must be absolute."));
        std::error_code error;
        const fs::path source = fs::weakly_canonical(absoluteSourceDirectory, error);
        if (error || !fs::is_directory(source, error) || fs::is_symlink(fs::symlink_status(source, error))) {
            return Result<std::string>::Failure(
                MakeError(ExtensionErrors::InvalidManifest, "Extension source must be a regular non-symlink directory."));
        }
        if (const fs::path canonicalInstallRoot = fs::weakly_canonical(installRoot_, error);
            !error && IsPathContainedBy(canonicalInstallRoot, source)) {
            return Result<std::string>::Failure(
                MakeError(ExtensionErrors::InvalidManifest, "Extension source must be outside the managed installation directory."));
        }
        error.clear();
        auto parsed = ReadManifest(source / "extension.json");
        if (parsed.HasError())
            return Result<std::string>::Failure(parsed.ErrorValue());
        const ExtensionManifest &manifest = parsed.Value();
        if (!IsSafePackageId(manifest.id))
            return Result<std::string>::Failure(
                MakeError(ExtensionErrors::InvalidManifest, "Extension package ID is unsafe for installation."));

        fs::create_directories(installRoot_, error);
        const fs::path destination = installRoot_ / manifest.id;
        if (error || fs::exists(destination, error))
            return Result<std::string>::Failure(
                MakeError(ExtensionErrors::LoadFailed, "An extension with this package ID is already installed."));
        const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
        const fs::path staging = installRoot_ / std::format(".install-{}-{}", manifest.id, nonce);
        const auto cleanup = [&staging]() {
            std::error_code ignored;
            fs::remove_all(staging, ignored);
        };
        if (auto copied = CopyPackageTree(source, staging); copied.HasError()) {
            cleanup();
            return Result<std::string>::Failure(copied.ErrorValue());
        }
        fs::rename(staging, destination, error);
        if (error) {
            cleanup();
            return Result<std::string>::Failure(MakeError(ExtensionErrors::LoadFailed, "Unable to publish the staged extension package."));
        }
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
            for (const auto &id : state.value("enabled", Json::array()))
                if (id.is_string())
                    enabled_.insert(id.get<std::string>());
            for (const auto &id : state.value("trusted", Json::array()))
                if (id.is_string())
                    trusted_.insert(id.get<std::string>());
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
                    {.type = "asset.importer", .id = "horo.asset-importer.obj-mesh", .module = "horo.builtin.assets.importer.obj"},
                    {.type = "asset.importer", .id = "horo.asset-importer.fbx-mesh", .module = "horo.builtin.assets.importer.fbx"},
                },
            .enabled = enabled_.contains("horo.builtin.assets"),
            .locallyTrusted = true,
        };
        builtIn.compositionVersion = BuildCompositionVersion(builtIn.version, builtIn.modules);
        entries_.push_back(std::move(builtIn));
    }
}  // namespace Horo::Extensions
