#include "Horo/Extensions/ExtensionManager.h"

#include "../capabilities/asset_pipeline_points/ExternalAssetImporter.h"
#include "Horo/Assets/AssetImporter.h"
#include "Horo/Extensions/ExtensionAbi.h"
#include "Horo/Extensions/ExtensionErrors.h"
#include "Horo/Foundation/Logging/Logger.h"
#include "Horo/Platform/DynamicLibrary.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <ranges>
#include <sstream>
#include <string_view>

namespace Horo::Extensions {
    namespace {
        namespace fs = std::filesystem;

        constexpr std::uint32_t kMaximumModuleIdentityBytes = 256;

        [[nodiscard]] std::string_view View(const HoroExtensionStringView value) noexcept {
            return value.data != nullptr ? std::string_view{value.data, value.length} : std::string_view{};
        }

        [[nodiscard]] bool IsValidBoundedText(const HoroExtensionStringView value) noexcept {
            return (value.data != nullptr || value.length == 0) && value.length <= kMaximumModuleIdentityBytes;
        }

        [[nodiscard]] fs::path NativeLibraryPath(const ExtensionManifest &manifest, const ExtensionModuleManifest &manifestModule) {
#if defined(_WIN32)
            constexpr std::string_view extension = ".dll";
#elif defined(__APPLE__)
            constexpr std::string_view extension = ".dylib";
#else
            constexpr std::string_view extension = ".so";
#endif
            fs::path entry = manifestModule.entry.empty() ? fs::path{manifest.id} : fs::path{manifestModule.entry};
            if (!entry.has_extension())
                entry += extension;
            return fs::path{manifest.rootPath} / entry;
        }

        [[nodiscard]] bool IsContainedLibraryPath(const fs::path &packageRoot, const fs::path &libraryPath) {
            std::error_code ec;
            const fs::path root = fs::weakly_canonical(packageRoot, ec);
            if (ec)
                return false;
            const fs::path library = fs::weakly_canonical(libraryPath, ec);
            if (ec)
                return false;
            auto rootIt = root.begin();
            auto libraryIt = library.begin();
            for (; rootIt != root.end(); ++rootIt, ++libraryIt) {
                if (libraryIt == library.end() || *rootIt != *libraryIt)
                    return false;
            }
            return true;
        }

        [[nodiscard]] bool HasSafeModuleEntry(const fs::path &packageRoot, const fs::path &entry) {
            if (entry.is_absolute())
                return false;
            fs::path current = packageRoot;
            std::error_code ec;
            for (const auto &component : entry) {
                if (component == "..")
                    return false;
                current /= component;
                if (fs::is_symlink(fs::symlink_status(current, ec)) || ec)
                    return false;
            }
            return true;
        }

        [[nodiscard]] Result<fs::path> ResolveModuleLibraryPath(const ExtensionManifest &manifest,
                                                                const ExtensionModuleManifest &manifestModule) {
            const fs::path libraryPath = NativeLibraryPath(manifest, manifestModule);
            fs::path moduleEntry = manifestModule.entry.empty() ? fs::path{manifest.id} : fs::path{manifestModule.entry};
            if (!moduleEntry.has_extension()) {
#if defined(_WIN32)
                moduleEntry += ".dll";
#elif defined(__APPLE__)
                moduleEntry += ".dylib";
#else
                moduleEntry += ".so";
#endif
            }
            if (!HasSafeModuleEntry(manifest.rootPath, moduleEntry) || !IsContainedLibraryPath(manifest.rootPath, libraryPath))
                return Result<fs::path>::Failure(
                    MakeError(ExtensionErrors::InvalidManifest, "Native module entry must resolve inside its absolute package root."));
            return Result<fs::path>::Success(libraryPath);
        }

        void SafeUnload(HoroExtensionUnloadFunc unload, HoroExtensionModuleApi &moduleApi,  // NOSONAR(cpp:S5205)
                        const char *context) {
            if (unload != nullptr && moduleApi.moduleContext != nullptr) {
                try {
                    unload(&moduleApi);
                } catch (const std::runtime_error &exception) {  // NOSONAR(cpp:S1181)
                    LOG_WARN("extensions", "Runtime error during %s unload: %s", context, exception.what());
                } catch (const std::logic_error &exception) {  // NOSONAR(cpp:S1181)
                    LOG_WARN("extensions", "Logic error during %s unload: %s", context, exception.what());
                } catch (const std::bad_alloc &exception) {  // NOSONAR(cpp:S1181)
                    LOG_WARN("extensions", "Bad alloc during %s unload: %s", context, exception.what());
                } catch (const std::exception &exception) {  // NOSONAR(cpp:S1181) External code is an exception containment boundary.
                    LOG_WARN("extensions", "Exception during %s unload: %s", context, exception.what());
                } catch (...) {  // NOSONAR(cpp:S1181)
                    LOG_WARN("extensions", "Unknown exception during %s unload.", context);
                }
            }
        }

        template <typename LoadedMap>
        [[nodiscard]] Result<ExtensionManifest> ReadAndValidateManifest(const fs::path &requestedRoot, const LoadedMap &loaded) {
            if (!requestedRoot.is_absolute())
                return Result<ExtensionManifest>::Failure(
                    MakeError(ExtensionErrors::InvalidManifest, "Extension package path must be absolute."));

            const fs::path manifestPath = requestedRoot / "extension.json";
            std::ifstream fileStream(manifestPath);
            if (!fileStream.is_open())
                return Result<ExtensionManifest>::Failure(MakeError(ExtensionErrors::InvalidManifest, "Could not open extension.json"));

            std::stringstream buffer;
            buffer << fileStream.rdbuf();
            auto parseResult = ParseExtensionManifest(buffer.str());
            if (parseResult.HasError())
                return Result<ExtensionManifest>::Failure(parseResult.ErrorValue());

            ExtensionManifest manifest = std::move(parseResult).Value();
            manifest.rootPath = fs::weakly_canonical(requestedRoot).string();
            if (loaded.contains(manifest.id))
                return Result<ExtensionManifest>::Failure(
                    MakeError(ExtensionErrors::LoadFailed, "An extension with this package ID is already loaded."));
            if (manifest.modules.size() != 1)
                return Result<ExtensionManifest>::Failure(
                    MakeError(ExtensionErrors::InvalidManifest, "The current native loader requires exactly one module per package."));
            return Result<ExtensionManifest>::Success(std::move(manifest));
        }

    }  // namespace

    /** @copydoc ExtensionManager::ExtensionManager */
    ExtensionManager::ExtensionManager(Assets::AssetImporterCatalog *importerCatalog) : m_importerCatalog(importerCatalog) {}

    ExtensionManager::~ExtensionManager() {
        UnloadAll();
    }

    std::vector<std::string> ExtensionManager::DiscoverExtensions(const std::string &directoryPath) const {
        std::vector<std::string> discovered;
        std::error_code ec;

        const fs::path dir{directoryPath};
        if (!fs::exists(dir, ec) || !fs::is_directory(dir, ec)) {
            LOG_WARN("extensions", "Discovery path does not exist or is not a directory: %s", directoryPath.c_str());
            return discovered;
        }

        for (const auto &entry : fs::directory_iterator(dir, ec)) {
            if (entry.is_directory()) {
                const fs::path manifestPath = entry.path() / "extension.json";
                if (fs::exists(manifestPath, ec))
                    discovered.push_back(entry.path().string());
            }
        }
        std::ranges::sort(discovered);
        return discovered;
    }

    Result<std::string> ExtensionManager::LoadExtension(const std::string &extensionDir) {
        auto manifestResult = ReadAndValidateManifest(fs::path{extensionDir}, m_loadedExtensions);
        if (manifestResult.HasError())
            return Result<std::string>::Failure(manifestResult.ErrorValue());

        ExtensionManifest manifest = std::move(manifestResult).Value();
        const ExtensionModuleManifest &manifestModule = manifest.modules.front();
        const std::string declaredModuleId = manifestModule.id;
        const std::string declaredModuleVersion = manifestModule.version;
        auto libraryPathResult = ResolveModuleLibraryPath(manifest, manifestModule);
        if (libraryPathResult.HasError())
            return Result<std::string>::Failure(libraryPathResult.ErrorValue());

        auto loadResult = Platform::LoadDynamicLibrary(libraryPathResult.Value().string());
        if (loadResult.HasError())
            return Result<std::string>::Failure(loadResult.ErrorValue());
        std::shared_ptr<Platform::DynamicLibrary> library{std::move(loadResult).Value()};

        const auto loadFunc = reinterpret_cast<HoroExtensionLoadFunc>(library->GetSymbol("horo_extension_load"));  // NOSONAR(cpp:S3630)
        if (loadFunc == nullptr)
            return Result<std::string>::Failure(MakeError(ExtensionErrors::MissingEntryPoint, "Symbol horo_extension_load not found"));

        auto lifetime = std::make_shared<ExtensionModuleLifetime>();
        lifetime->library = library;
        lifetime->unload = reinterpret_cast<HoroExtensionUnloadFunc>(library->GetSymbol("horo_extension_unload"));  // NOSONAR(cpp:S3630)

        AssetImporterRegistrationSession registration{
            .manifest = &manifest,
            .extensionModule = &manifestModule,
            .lifetime = lifetime,
        };
        constexpr std::string_view engineVersion = "0.1.0";
        HoroExtensionHostApi hostApi{
            .structSize = sizeof(HoroExtensionHostApi),
            .abiVersion = HORO_EXTENSION_ABI_VERSION,
            .engineVersion = {engineVersion.data(), static_cast<std::uint32_t>(engineVersion.size())},
            .hostContext = &registration,
            .registerAssetImporter = RegisterExternalAssetImporter,
        };
        HoroExtensionModuleApi moduleApi{
            .structSize = sizeof(HoroExtensionModuleApi),
        };

        HoroExtensionStatus status = HORO_EXTENSION_ERROR_INIT_FAILED;
        try {
            status = loadFunc(&hostApi, &moduleApi);
        } catch (const std::runtime_error &exception) {  // NOSONAR(cpp:S1181)
            LOG_ERROR("extensions", "Extension %s threw runtime error during load: %s", manifest.id.c_str(), exception.what());
            status = HORO_EXTENSION_ERROR_INIT_FAILED;
        } catch (const std::logic_error &exception) {  // NOSONAR(cpp:S1181)
            LOG_ERROR("extensions", "Extension %s threw logic error during load: %s", manifest.id.c_str(), exception.what());
            status = HORO_EXTENSION_ERROR_INIT_FAILED;
        } catch (const std::bad_alloc &exception) {  // NOSONAR(cpp:S1181)
            LOG_ERROR("extensions", "Extension %s threw bad alloc during load: %s", manifest.id.c_str(), exception.what());
            status = HORO_EXTENSION_ERROR_INIT_FAILED;
        } catch (const std::exception &exception) {  // NOSONAR(cpp:S1181)
            LOG_ERROR("extensions", "Extension %s threw during load: %s", manifest.id.c_str(), exception.what());
            status = HORO_EXTENSION_ERROR_INIT_FAILED;
        } catch (...) {  // NOSONAR(cpp:S1181)
            LOG_ERROR("extensions", "Extension %s threw unknown exception during load.", manifest.id.c_str());
            status = HORO_EXTENSION_ERROR_INIT_FAILED;
        }

        if (status != HORO_EXTENSION_SUCCESS || registration.failed) {
            SafeUnload(lifetime->unload, moduleApi, "rollback");
            if (registration.failed)
                return Result<std::string>::Failure(std::move(registration.error));
            return Result<std::string>::Failure(MakeError(ExtensionErrors::LoadFailed, "Extension load function returned an error."));
        }

        if (!IsValidBoundedText(moduleApi.moduleId) || !IsValidBoundedText(moduleApi.moduleVersion) ||
            (!View(moduleApi.moduleId).empty() && View(moduleApi.moduleId) != declaredModuleId) ||
            (!View(moduleApi.moduleVersion).empty() && View(moduleApi.moduleVersion) != declaredModuleVersion)) {
            SafeUnload(lifetime->unload, moduleApi, "validation failure");
            return Result<std::string>::Failure(
                MakeError(ExtensionErrors::InvalidManifest, "Loaded module identity/version does not match extension.json."));
        }

        lifetime->moduleApi = moduleApi;
        lifetime->loaded = true;

        if (!registration.contributions.empty()) {
            if (m_importerCatalog == nullptr)
                return Result<std::string>::Failure(
                    MakeError(ExtensionErrors::ContributionRejected, "The host did not provide an asset importer catalog."));
            if (auto registered = m_importerCatalog->RegisterBatch(std::move(registration.contributions)); registered.HasError()) {
                return Result<std::string>::Failure(
                    MakeError(ExtensionErrors::ContributionRejected,
                              "The complete extension contribution batch conflicted with the host catalog."));
            }
        }

        auto loadedExtension = std::make_unique<LoadedExtension>();
        loadedExtension->manifest = std::move(manifest);
        loadedExtension->lifetime = std::move(lifetime);
        loadedExtension->moduleId = declaredModuleId;
        loadedExtension->moduleVersion = declaredModuleVersion;
        const std::string extensionId = loadedExtension->manifest.id;
        m_loadedExtensions.try_emplace(extensionId, std::move(loadedExtension));

        LOG_INFO("extensions", "Successfully loaded extension: %s", extensionId.c_str());
        return Result<std::string>::Success(extensionId);
    }

    void ExtensionManager::UnloadExtension(const std::string &extensionId) {
        if (const auto it = m_loadedExtensions.find(extensionId); it != m_loadedExtensions.end()) {
            m_loadedExtensions.erase(it);
            LOG_INFO("extensions", "Released extension manager lease: %s", extensionId.c_str());
        }
    }

    void ExtensionManager::UnloadAll() {
        m_loadedExtensions.clear();
    }

    std::vector<std::string> ExtensionManager::GetLoadedExtensionIds() const {
        std::vector<std::string> ids;
        ids.reserve(m_loadedExtensions.size());
        for (const auto &key : m_loadedExtensions | std::views::keys)
            ids.push_back(key);
        std::ranges::sort(ids);
        return ids;
    }
}  // namespace Horo::Extensions
