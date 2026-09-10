#include "Horo/Extensions/ExtensionManager.h"

#include "../capabilities/asset_pipeline_points/ExternalAssetImporter.h"
#include "ExtensionAbiValidation.h"
#include "ExtensionActivationTransaction.h"
#include "Horo/Assets/AssetImporter.h"
#include "Horo/Extensions/ExtensionAbi.h"
#include "Horo/Extensions/ExtensionErrors.h"
#include "Horo/Extensions/ExtensionModuleResolution.h"
#include "Horo/Foundation/Logging/Logger.h"
#include "Horo/Platform/DynamicLibrary.h"
#include "Horo/Security/SecurityErrors.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <ranges>
#include <sstream>
#include <string_view>

namespace Horo::Extensions {
    namespace {
        namespace fs = std::filesystem;

        constexpr std::uint32_t kMaximumModuleIdentityBytes = 256;
        constexpr ExtensionManifestLimits kManifestLimits{};
        constexpr std::size_t kMaximumHostCapabilities = kManifestLimits.maximumContributions;
        constexpr std::uintmax_t kMaximumNativeArtifactBytes = 1024ULL * 1024ULL * 1024ULL;

        [[nodiscard]] std::string_view View(const HoroExtensionStringView value) noexcept {
            return value.data != nullptr ? std::string_view{value.data, value.length} : std::string_view{};
        }

        [[nodiscard]] bool IsValidBoundedText(const HoroExtensionStringView value) noexcept {
            return (value.data != nullptr || value.length == 0) && value.length <= kMaximumModuleIdentityBytes;
        }

        [[nodiscard]] fs::path NativeLibraryEntryPath(const ExtensionManifest &manifest, const std::string_view selectedEntry) {
#if defined(_WIN32)
            constexpr std::string_view extension = ".dll";
#elif defined(__APPLE__)
            constexpr std::string_view extension = ".dylib";
#else
            constexpr std::string_view extension = ".so";
#endif
            fs::path entry = selectedEntry.empty() ? fs::path{manifest.id} : fs::path{selectedEntry};
            if (!entry.has_extension())
                entry += extension;
            return entry;
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

        [[nodiscard]] Result<fs::path> ResolveModuleLibraryPath(const ExtensionManifest &manifest, const std::string_view selectedEntry) {
            const fs::path moduleEntry = NativeLibraryEntryPath(manifest, selectedEntry);
            const fs::path libraryPath = fs::path{manifest.rootPath} / moduleEntry;
            if (!HasSafeModuleEntry(manifest.rootPath, moduleEntry) || !IsContainedLibraryPath(manifest.rootPath, libraryPath))
                return Result<fs::path>::Failure(
                    MakeError(ExtensionErrors::InvalidManifest, "Native module entry must resolve inside its absolute package root."));
            return Result<fs::path>::Success(libraryPath);
        }

        [[nodiscard]] constexpr ExtensionHostPlatform CurrentHostPlatform() noexcept {
#if defined(_WIN32)
            return ExtensionHostPlatform::Windows;
#elif defined(__APPLE__)
            return ExtensionHostPlatform::MacOS;
#else
            return ExtensionHostPlatform::Linux;
#endif
        }

        [[nodiscard]] constexpr ExtensionHostArchitecture CurrentHostArchitecture() noexcept {
#if defined(__aarch64__) || defined(_M_ARM64)
            return ExtensionHostArchitecture::Arm64;
#else
            return ExtensionHostArchitecture::X86_64;
#endif
        }

        [[nodiscard]] constexpr ExtensionBuildProfile CurrentBuildProfile() noexcept {
#if defined(NDEBUG)
            return ExtensionBuildProfile::Release;
#else
            return ExtensionBuildProfile::Debug;
#endif
        }

        [[nodiscard]] ExtensionHostEnvironment CurrentHostEnvironment(const ExtensionHostProfile profile,
                                                                      const std::span<const std::string_view> capabilities) noexcept {
            return {
                .profile = profile,
                .platform = CurrentHostPlatform(),
                .architecture = CurrentHostArchitecture(),
                .buildProfile = CurrentBuildProfile(),
                .engineVersion = "0.1.0",
                .abiMajor = HORO_EXTENSION_ABI_VERSION,
                .abiMinor = HORO_EXTENSION_ABI_MINOR_VERSION,
                .capabilities = capabilities,
            };
        }

        void CanonicalizeHostCapabilities(std::vector<std::string> &capabilities) {
            std::erase_if(capabilities, [](const std::string_view capability) {
                return capability.empty() || capability.size() > kManifestLimits.maximumIdentifierBytes;
            });
            std::ranges::sort(capabilities);
            capabilities.erase(std::ranges::unique(capabilities).begin(), capabilities.end());
            if (capabilities.size() > kMaximumHostCapabilities)
                capabilities.resize(kMaximumHostCapabilities);
        }

        /** @brief Reads one bounded manifest file from an absolute package root. */
        [[nodiscard]] Result<std::string> ReadManifestContent(const fs::path &requestedRoot) {
            if (!requestedRoot.is_absolute())
                return Result<std::string>::Failure(
                    MakeError(ExtensionErrors::InvalidManifest, "Extension package path must be absolute."));

            const fs::path manifestPath = requestedRoot / "extension.json";
            std::error_code fileError;
            if (const std::uintmax_t manifestBytes = fs::file_size(manifestPath, fileError);
                fileError || manifestBytes > kManifestLimits.maximumDocumentBytes) {
                return Result<std::string>::Failure(
                    MakeError(ExtensionErrors::InvalidManifest, "Extension manifest is unavailable or exceeds the bounded size."));
            }
            std::ifstream fileStream(manifestPath, std::ios::binary);
            if (!fileStream.is_open())
                return Result<std::string>::Failure(MakeError(ExtensionErrors::InvalidManifest, "Could not open extension.json"));

            std::stringstream buffer;
            buffer << fileStream.rdbuf();
            return Result<std::string>::Success(std::move(buffer).str());
        }

        /** @brief Parses a manifest and checks constraints imposed by the native loader. */
        template <typename LoadedMap>
        [[nodiscard]] Result<ExtensionManifest> ValidateLoadableManifest(const std::string &content, const fs::path &requestedRoot,
                                                                         const LoadedMap &loaded) {
            auto parseResult = ParseExtensionManifest(content, kManifestLimits);
            if (parseResult.HasError())
                return Result<ExtensionManifest>::Failure(parseResult.ErrorValue());

            ExtensionManifest manifest = std::move(parseResult).Value();
            manifest.rootPath = fs::weakly_canonical(requestedRoot).string();
            if (loaded.contains(manifest.id))
                return Result<ExtensionManifest>::Failure(
                    MakeError(ExtensionErrors::LoadFailed, "An extension with this package ID is already loaded."));
            return Result<ExtensionManifest>::Success(std::move(manifest));
        }

        template <typename LoadedMap>
        [[nodiscard]] Result<ExtensionManifest> ReadAndValidateManifest(const fs::path &requestedRoot, const LoadedMap &loaded) {
            auto content = ReadManifestContent(requestedRoot);
            if (content.HasError())
                return Result<ExtensionManifest>::Failure(content.ErrorValue());
            return ValidateLoadableManifest(content.Value(), requestedRoot, loaded);
        }

        /** @brief Invokes an extension entry point while containing exceptions at the ABI boundary. */
        template <typename LoadFunction>
        [[nodiscard]] HoroExtensionStatus InvokeExtensionLoad(const LoadFunction loadFunc, const HoroExtensionHostApi &hostApi,
                                                              HoroExtensionModuleApi &moduleApi, const std::string &extensionId) {
            try {
                return loadFunc(&hostApi, &moduleApi);
            } catch (const std::runtime_error &exception) {  // NOSONAR(cpp:S1181)
                LOG_ERROR("extensions", "Extension %s threw runtime error during load: %s", extensionId.c_str(), exception.what());
            } catch (const std::logic_error &exception) {  // NOSONAR(cpp:S1181)
                LOG_ERROR("extensions", "Extension %s threw logic error during load: %s", extensionId.c_str(), exception.what());
            } catch (const std::bad_alloc &exception) {  // NOSONAR(cpp:S1181)
                LOG_ERROR("extensions", "Extension %s threw bad alloc during load: %s", extensionId.c_str(), exception.what());
            } catch (const std::exception &exception) {  // NOSONAR(cpp:S1181)
                LOG_ERROR("extensions", "Extension %s threw during load: %s", extensionId.c_str(), exception.what());
            } catch (...) {  // NOSONAR(cpp:S1181)
                LOG_ERROR("extensions", "Extension %s threw unknown exception during load.", extensionId.c_str());
            }
            return HORO_EXTENSION_ERROR_INIT_FAILED;
        }

        /** @brief Checks the loaded module identity against the validated manifest declaration. */
        [[nodiscard]] bool MatchesDeclaredModule(const HoroExtensionModuleApi &moduleApi, const std::string_view declaredId,
                                                 const std::string_view declaredVersion) noexcept {
            return IsValidBoundedText(moduleApi.moduleId) && IsValidBoundedText(moduleApi.moduleVersion) &&
                   (View(moduleApi.moduleId).empty() || View(moduleApi.moduleId) == declaredId) &&
                   (View(moduleApi.moduleVersion).empty() || View(moduleApi.moduleVersion) == declaredVersion);
        }

        /** @brief Owns a validated native module and its staged importer contributions. */
        struct ActivatedModule {
            std::shared_ptr<ExtensionModuleLifetime> lifetime;
            std::vector<Assets::AssetImporterContribution> contributions;
        };

        /** @brief Loads and validates one native module while retaining rollback ownership. */
        [[nodiscard]] Result<ActivatedModule> ActivateModule(const std::shared_ptr<Platform::DynamicLibrary> &library,
                                                             const ExtensionManifest &manifest,
                                                             const ExtensionModuleManifest &manifestModule) {
            const auto loadFunc = reinterpret_cast<HoroExtensionLoadFunc>(library->GetSymbol("horo_extension_load"));  // NOSONAR(cpp:S3630)
            if (loadFunc == nullptr)
                return Result<ActivatedModule>::Failure(
                    MakeError(ExtensionErrors::MissingEntryPoint, "Symbol horo_extension_load not found"));

            auto lifetime = std::make_shared<ExtensionModuleLifetime>();
            lifetime->library = library;
            lifetime->moduleId = manifestModule.id;
            lifetime->unload =
                reinterpret_cast<HoroExtensionUnloadFunc>(library->GetSymbol("horo_extension_unload"));  // NOSONAR(cpp:S3630)
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
                .abiMinorVersion = HORO_EXTENSION_ABI_MINOR_VERSION,
            };
            if (const auto query =
                    reinterpret_cast<HoroExtensionQueryFunc>(library->GetSymbol("horo_extension_query"));  // NOSONAR(cpp:S3630)
                NegotiateModuleAbi(query, hostApi) != HORO_EXTENSION_SUCCESS)
                return Result<ActivatedModule>::Failure(
                    MakeError(ExtensionErrors::LoadFailed, "Native module ABI requirements are incompatible with the host."));
            HoroExtensionModuleApi moduleApi{.structSize = sizeof(HoroExtensionModuleApi)};
            if (const HoroExtensionStatus status = InvokeExtensionLoad(loadFunc, hostApi, moduleApi, manifest.id);
                status != HORO_EXTENSION_SUCCESS || registration.failed) {
                lifetime->moduleApi = moduleApi;
                lifetime->loaded = true;
                ExtensionActivationTransaction rollback;
                rollback.Stage(lifetime, std::move(registration.contributions));
                Error error = registration.failed ? std::move(registration.error)
                                                  : MakeError(ExtensionErrors::LoadFailed, "Extension load function returned an error.");
                return Result<ActivatedModule>::Failure(rollback.Rollback(std::move(error)));
            }
            if (!NormalizeModuleApi(moduleApi) || !MatchesDeclaredModule(moduleApi, manifestModule.id, manifestModule.version)) {
                lifetime->moduleApi = moduleApi;
                lifetime->loaded = true;
                ExtensionActivationTransaction rollback;
                rollback.Stage(lifetime, std::move(registration.contributions));
                return Result<ActivatedModule>::Failure(
                    rollback.Rollback(MakeError(ExtensionErrors::InvalidManifest, "Loaded module table or identity/version is invalid.")));
            }
            lifetime->moduleApi = moduleApi;
            lifetime->loaded = true;
            return Result<ActivatedModule>::Success(
                {.lifetime = std::move(lifetime), .contributions = std::move(registration.contributions)});
        }

        /** @brief Atomically commits staged importer contributions to the host catalog. */
        [[nodiscard]] Result<void> CommitContributions(std::vector<Assets::AssetImporterContribution> &contributions,
                                                       Assets::AssetImporterCatalog *importerCatalog) {
            if (contributions.empty())
                return Result<void>::Success();
            if (importerCatalog == nullptr)
                return Result<void>::Failure(
                    MakeError(ExtensionErrors::ContributionRejected, "The host did not provide an asset importer catalog."));
            if (auto validation = importerCatalog->ValidateBatch(contributions); validation.HasError())
                return Result<void>::Failure(WrapError(ExtensionErrors::ContributionRejected, validation.ErrorValue(),
                                                       "The complete extension contribution batch conflicted with the host catalog."));
            if (auto registered = importerCatalog->RegisterBatch(std::move(contributions)); registered.HasError()) {
                return Result<void>::Failure(WrapError(ExtensionErrors::ContributionRejected, registered.ErrorValue(),
                                                       "The complete extension contribution batch could not be committed."));
            }
            return Result<void>::Success();
        }

    }  // namespace

    /** @copydoc ExtensionManager::ExtensionManager */
    ExtensionManager::ExtensionManager(Assets::AssetImporterCatalog *importerCatalog, const ExtensionHostProfile hostProfile,
                                       std::vector<std::string> hostCapabilities,
                                       std::shared_ptr<const Security::NativeArtifactGate> artifactGate, NativeLibraryLoader libraryLoader)
        : m_importerCatalog(importerCatalog), m_hostProfile(hostProfile), m_hostCapabilities(std::move(hostCapabilities)),
          m_artifactGate(std::move(artifactGate)), m_libraryLoader(std::move(libraryLoader)) {
        CanonicalizeHostCapabilities(m_hostCapabilities);
        if (!m_libraryLoader)
            m_libraryLoader = [](const std::string &path) {
                return Platform::LoadDynamicLibrary(path);
            };
    }

    ExtensionManager::~ExtensionManager() {
        UnloadAll();
    }

    ExtensionManager::ExtensionManager(ExtensionManager &&) noexcept = default;
    ExtensionManager &ExtensionManager::operator=(ExtensionManager &&) noexcept = default;

    Result<std::string> ExtensionManager::LoadExtension(const std::string &extensionDir) {
        auto manifestResult = ReadAndValidateManifest(fs::path{extensionDir}, m_loadedExtensions);
        if (manifestResult.HasError())
            return Result<std::string>::Failure(manifestResult.ErrorValue());

        ExtensionManifest manifest = std::move(manifestResult).Value();
        std::vector<std::string_view> capabilityViews;
        capabilityViews.reserve(m_hostCapabilities.size());
        std::ranges::transform(m_hostCapabilities, std::back_inserter(capabilityViews), [](const std::string &capability) {
            return std::string_view{capability};
        });
        const ExtensionHostEnvironment host = CurrentHostEnvironment(m_hostProfile, capabilityViews);
        auto planResult = ResolveExtensionModules(manifest, host);
        if (planResult.HasError())
            return Result<std::string>::Failure(planResult.ErrorValue());
        ExtensionModulePlan plan = std::move(planResult).Value();

        ExtensionActivationTransaction transaction;
        for (std::size_t moduleIndex = 0; moduleIndex < plan.moduleIds.size(); ++moduleIndex) {
            const std::string &moduleId = plan.moduleIds[moduleIndex];
            const auto manifestModule = std::ranges::find(manifest.modules, moduleId, &ExtensionModuleManifest::id);
            if (manifestModule == manifest.modules.end())
                return Result<std::string>::Failure(transaction.Rollback(
                    MakeError(ExtensionErrors::ModuleResolutionFailed, "Resolved module is absent from the package manifest.")));

            auto libraryPathResult = ResolveModuleLibraryPath(manifest, plan.selectedEntries[moduleIndex]);
            if (libraryPathResult.HasError())
                return Result<std::string>::Failure(transaction.Rollback(libraryPathResult.ErrorValue()));
            if (!m_artifactGate)
                return Result<std::string>::Failure(
                    transaction.Rollback(MakeError(SecurityErrors::MissingEvidence, "Native extension activation has no security gate.")));
            auto evidence = m_artifactGate->Verify(libraryPathResult.Value());
            if (evidence.HasError())
                return Result<std::string>::Failure(transaction.Rollback(evidence.ErrorValue()));
            std::error_code artifactSizeError;
            if (const std::uintmax_t artifactSize = fs::file_size(libraryPathResult.Value(), artifactSizeError);
                artifactSizeError || artifactSize > kMaximumNativeArtifactBytes)
                return Result<std::string>::Failure(transaction.Rollback(MakeError(SecurityErrors::StaleEvidence)));
            std::ifstream verifiedFile{libraryPathResult.Value(), std::ios::binary};
            const std::string verifiedBytes{std::istreambuf_iterator<char>{verifiedFile}, std::istreambuf_iterator<char>{}};
            const std::span<const std::byte> verifiedSpan{reinterpret_cast<const std::byte *>(verifiedBytes.data()), verifiedBytes.size()};
            if (!verifiedFile || ComputeSha256(verifiedSpan) != evidence.Value().ArtifactDigest())
                return Result<std::string>::Failure(transaction.Rollback(MakeError(SecurityErrors::StaleEvidence)));
            auto loadResult = m_libraryLoader(libraryPathResult.Value().string());
            if (loadResult.HasError())
                return Result<std::string>::Failure(transaction.Rollback(loadResult.ErrorValue()));
            std::shared_ptr<Platform::DynamicLibrary> library{std::move(loadResult).Value()};
            auto activatedResult = ActivateModule(library, manifest, *manifestModule);
            if (activatedResult.HasError())
                return Result<std::string>::Failure(transaction.Rollback(activatedResult.ErrorValue()));

            ActivatedModule activated = std::move(activatedResult).Value();
            transaction.Stage(std::move(activated.lifetime), std::move(activated.contributions));
        }
        if (auto committed = CommitContributions(transaction.Contributions(), m_importerCatalog); committed.HasError())
            return Result<std::string>::Failure(transaction.Rollback(committed.ErrorValue()));

        auto loadedExtension = std::make_unique<LoadedExtension>();
        loadedExtension->manifest = std::move(manifest);
        loadedExtension->lifetimes = transaction.ReleaseLifetimes();
        loadedExtension->moduleIds = std::move(plan.moduleIds);
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
