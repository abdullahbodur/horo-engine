#include "editor/gameplay/ProjectGameplayRegistry.h"

#include "Horo/Gameplay/GameplayErrors.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <nlohmann/json.hpp>
#include <ranges>
#include <system_error>

namespace Horo::Editor {
    namespace {
        constexpr std::size_t MaximumDiscoveredScripts = 1024;
        constexpr std::uintmax_t MaximumGameplayManifestBytes = 64U * 1024U;

        struct NativeArtifactManifest {
            std::string moduleId;
            std::string buildFingerprint;
            std::uint64_t descriptorRevision{};
            std::filesystem::path artifactPath;
        };

        [[nodiscard]] Error ManifestError(const std::string &message) {
            return MakeError(Gameplay::GameplayErrors::InvalidBehaviorComponent,
                             "Gameplay module artifact manifest is invalid: " + message);
        }

        [[nodiscard]] Result<NativeArtifactManifest> ReadNativeArtifactManifest(const std::filesystem::path &path) {
            std::error_code error;
            if (const std::uintmax_t size = std::filesystem::file_size(path, error);
                error || size == 0 || size > MaximumGameplayManifestBytes)
                return Result<NativeArtifactManifest>::Failure(ManifestError("file size is unavailable or outside the supported limit."));
            std::ifstream input(path, std::ios::binary);
            const nlohmann::json document = nlohmann::json::parse(input, nullptr, false);
            if (document.is_discarded() || !document.is_object() || document.value("schemaVersion", 0) != 1 ||
                !document.contains("moduleId") || !document["moduleId"].is_string() || !document.contains("buildFingerprint") ||
                !document["buildFingerprint"].is_string() || !document.contains("descriptorRevision") ||
                !document["descriptorRevision"].is_number_unsigned() || !document.contains("artifactPath") ||
                !document["artifactPath"].is_string())
                return Result<NativeArtifactManifest>::Failure(ManifestError("required typed fields are missing."));

            NativeArtifactManifest manifest{document["moduleId"].get<std::string>(), document["buildFingerprint"].get<std::string>(),
                                            document["descriptorRevision"].get<std::uint64_t>(),
                                            std::filesystem::path{document["artifactPath"].get<std::string>()}};
            if (manifest.moduleId.empty() || manifest.buildFingerprint.empty() || manifest.descriptorRevision == 0 ||
                !manifest.artifactPath.is_absolute() || !std::filesystem::is_regular_file(manifest.artifactPath, error) || error)
                return Result<NativeArtifactManifest>::Failure(ManifestError("metadata or artifact path is invalid."));
            return Result<NativeArtifactManifest>::Success(std::move(manifest));
        }

        [[nodiscard]] bool HasNativeGameplaySources(const std::filesystem::path &projectRoot) {
            const std::array roots{projectRoot / "source" / "gameplay", projectRoot / "src" / "gameplay"};
            for (const std::filesystem::path &root : roots) {
                std::error_code error;
                if (!std::filesystem::is_directory(root, error))
                    continue;
                for (const std::filesystem::directory_entry &entry :
                     std::filesystem::recursive_directory_iterator(root, std::filesystem::directory_options::skip_permission_denied,
                                                                   error)) {
                    if (entry.is_regular_file(error) &&
                        (entry.path().extension() == ".cpp" || entry.path().extension() == ".cc" || entry.path().extension() == ".cxx") &&
                        // GameModule.cpp is the generated bootstrap created by the editor;
                        // it is not evidence that the project contains a native behavior.
                        entry.path().filename() != "GameModule.cpp")
                        return true;
                }
            }
            return false;
        }

    }  // namespace

    ProjectGameplayRegistry::LuaSourceStat ProjectGameplayRegistry::ReadLuaSourceStat(const std::filesystem::path &source,
                                                                                      std::error_code &error) {
        const std::filesystem::path metadata{source.string() + ".meta"};
        ProjectGameplayRegistry::LuaSourceStat stat;
        stat.sourceWriteTime = std::filesystem::last_write_time(source, error);
        if (error)
            return {};
        stat.metadataWriteTime = std::filesystem::last_write_time(metadata, error);
        if (error)
            return {};
        stat.sourceSize = std::filesystem::file_size(source, error);
        if (error)
            return {};
        stat.metadataSize = std::filesystem::file_size(metadata, error);
        return error ? ProjectGameplayRegistry::LuaSourceStat{} : stat;
    }

    /** @copydoc ProjectGameplayRegistry::Discover */
    std::unique_ptr<ProjectGameplayRegistry> ProjectGameplayRegistry::Discover(const std::filesystem::path &projectRoot) {
        auto result = std::make_unique<ProjectGameplayRegistry>(ConstructionToken{});
        result->DiscoverNativeModule(projectRoot);
        result->DiscoverLuaPrograms(projectRoot);
        return result;
    }

    void ProjectGameplayRegistry::DiscoverNativeModule(const std::filesystem::path &projectRoot) {
        const std::filesystem::path nativeManifestPath = projectRoot / ".horo" / "local" / "gameplay_module.json";
        nativeManifestPath_ = nativeManifestPath;
        if (!PrepareNativeManifestPath(projectRoot, nativeManifestPath))
            return;

        Result<NativeArtifactManifest> manifest = ReadNativeArtifactManifest(nativeManifestPath);
        if (manifest.HasError()) {
            diagnostics_.emplace_back(nativeManifestPath, manifest.ErrorValue());
            return;
        }
        if (manifest.Value().buildFingerprint != Gameplay::CurrentGameplayBuildFingerprint()) {
            diagnostics_.emplace_back(nativeManifestPath, ManifestError("the artifact was built for a different Horo SDK generation."));
            return;
        }
        LoadNativeModule(projectRoot, nativeManifestPath, manifest.Value().artifactPath, manifest.Value().moduleId,
                         manifest.Value().descriptorRevision);
    }

    bool ProjectGameplayRegistry::PrepareNativeManifestPath(const std::filesystem::path &projectRoot,
                                                            const std::filesystem::path &manifestPath) {
        std::error_code nativeFilesystemError;
        const bool nativeManifestExists = std::filesystem::exists(manifestPath, nativeFilesystemError);
        if (nativeFilesystemError) {
            diagnostics_.emplace_back(manifestPath, ManifestError(nativeFilesystemError.message()));
            return false;
        }
        if (!nativeManifestExists) {
            if (HasNativeGameplaySources(projectRoot))
                diagnostics_.emplace_back(manifestPath,
                                          ManifestError(
                                              "native gameplay sources exist, but no successful module build has been published."));
            return false;
        }

        nativeManifestWriteTime_ = std::filesystem::last_write_time(manifestPath, nativeFilesystemError);
        if (nativeFilesystemError) {
            diagnostics_.emplace_back(manifestPath, ManifestError(nativeFilesystemError.message()));
            return false;
        }
        const bool regularManifest = std::filesystem::is_regular_file(manifestPath, nativeFilesystemError);
        if (nativeFilesystemError) {
            diagnostics_.emplace_back(manifestPath, ManifestError(nativeFilesystemError.message()));
            return false;
        }
        if (!regularManifest) {
            diagnostics_.emplace_back(manifestPath, ManifestError("manifest path is not a regular file."));
            return false;
        }
        return true;
    }

    void ProjectGameplayRegistry::LoadNativeModule(const std::filesystem::path &projectRoot, const std::filesystem::path &manifestPath,
                                                   const std::filesystem::path &artifactPath, const std::string_view moduleId,
                                                   const std::uint64_t descriptorRevision) {
        Gameplay::GameModuleHost host;
        Result<std::unique_ptr<Gameplay::LoadedGameModule>> loaded =
            host.LoadShadowCopy(artifactPath, projectRoot / ".horo" / "local" / "gameplay_module_shadow",
                                Gameplay::CurrentGameplayBuildFingerprint());
        if (loaded.HasError()) {
            diagnostics_.emplace_back(artifactPath, loaded.ErrorValue());
            return;
        }
        if (loaded.Value()->ModuleId() != moduleId || loaded.Value()->DescriptorRevision() != descriptorRevision) {
            diagnostics_.emplace_back(manifestPath, ManifestError("module identity or descriptor revision does not match the artifact."));
            return;
        }

        nativeModule_ = std::move(loaded).Value();
        for (const Gameplay::BehaviorRegistration &registration : nativeModule_->Registry().Registrations()) {
            if (Result<void> registered = registry_.Register(registration); registered.HasError()) {
                diagnostics_.emplace_back(artifactPath, registered.ErrorValue());
                return;
            }
        }
    }

    void ProjectGameplayRegistry::DiscoverLuaPrograms(const std::filesystem::path &projectRoot) {
        const std::filesystem::path scriptsRoot = projectRoot / "assets" / "scripts";
        std::error_code filesystemError;
        if (!std::filesystem::is_directory(scriptsRoot, filesystemError)) {
            if (const Result<void> frozen = registry_.Freeze(); frozen.HasError())
                diagnostics_.emplace_back(scriptsRoot, frozen.ErrorValue());
            return;
        }

        std::vector<std::filesystem::path> sources;
        for (const std::filesystem::directory_entry &entry :
             std::filesystem::recursive_directory_iterator(scriptsRoot, std::filesystem::directory_options::skip_permission_denied,
                                                           filesystemError)) {
            if (!entry.is_regular_file(filesystemError) || entry.path().extension() != ".horo_script")
                continue;
            if (sources.size() == MaximumDiscoveredScripts) {
                diagnostics_.emplace_back(scriptsRoot, MakeError(Gameplay::GameplayErrors::InvalidBehaviorComponent,
                                                                 "Project script discovery exceeds the supported asset count."));
                break;
            }
            sources.emplace_back(entry.path());
        }
        if (filesystemError)
            diagnostics_.emplace_back(scriptsRoot,
                                      MakeError(Gameplay::GameplayErrors::InvalidBehaviorComponent, filesystemError.message()));

        std::ranges::sort(sources);
        for (const std::filesystem::path &source : sources) {
            auto loaded = Gameplay::LuaBehaviorProgram::LoadFiles(source, source.string() + ".meta");
            if (loaded.HasError()) {
                diagnostics_.emplace_back(source, loaded.ErrorValue());
                continue;
            }
            std::unique_ptr<Gameplay::LuaBehaviorProgram> program = std::move(loaded).Value();
            if (Result<void> registered = registry_.Register(program->Registration()); registered.HasError()) {
                diagnostics_.emplace_back(source, registered.ErrorValue());
                continue;
            }
            luaPrograms_.emplace_back(std::move(program));
            luaSources_.emplace_back(source);
            luaSourceStats_.emplace_back(ReadLuaSourceStat(source, filesystemError));
            filesystemError.clear();
        }

        if (Result<void> frozen = registry_.Freeze(); frozen.HasError())
            diagnostics_.emplace_back(scriptsRoot, frozen.ErrorValue());
    }

    /** @copydoc ProjectGameplayRegistry::Registry */
    const Gameplay::BehaviorRegistry &ProjectGameplayRegistry::Registry() const noexcept {
        return registry_;
    }

    /** @copydoc ProjectGameplayRegistry::Diagnostics */
    const std::vector<ProjectGameplayDiagnostic> &ProjectGameplayRegistry::Diagnostics() const noexcept {
        return diagnostics_;
    }

    /** @copydoc ProjectGameplayRegistry::HasBlockingDiagnostics */
    bool ProjectGameplayRegistry::HasBlockingDiagnostics() const noexcept {
        return !diagnostics_.empty();
    }

    /** @copydoc ProjectGameplayRegistry::ReloadChangedLuaSources */
    std::vector<ProjectGameplayDiagnostic> ProjectGameplayRegistry::ReloadChangedLuaSources() {
        std::vector<ProjectGameplayDiagnostic> diagnostics;
        for (const std::size_t index : std::views::iota(std::size_t{0}, luaPrograms_.size())) {
            std::error_code error;
            const LuaSourceStat sourceStat = ReadLuaSourceStat(luaSources_[index], error);
            if (error) {
                diagnostics.emplace_back(luaSources_[index],
                                         MakeError(Gameplay::GameplayErrors::InvalidBehaviorComponent, error.message()));
                continue;
            }
            if (sourceStat == luaSourceStats_[index])
                continue;
            luaSourceStats_[index] = sourceStat;
            auto candidate = Gameplay::LuaBehaviorProgram::LoadFiles(luaSources_[index], luaSources_[index].string() + ".meta");
            if (candidate.HasError()) {
                diagnostics.emplace_back(luaSources_[index], candidate.ErrorValue());
                continue;
            }
            Result<void> replaced = luaPrograms_[index]->ReplaceCompatible(std::move(candidate).Value());
            if (replaced.HasError())
                diagnostics.emplace_back(luaSources_[index], replaced.ErrorValue());
        }
        return diagnostics;
    }

    /** @copydoc ProjectGameplayRegistry::ConsumeNativeArtifactChange */
    bool ProjectGameplayRegistry::ConsumeNativeArtifactChange() {
        std::error_code error;
        const bool exists = std::filesystem::exists(nativeManifestPath_, error);
        if (error)
            return false;
        if (!exists) {
            const bool changed = nativeManifestWriteTime_.has_value();
            nativeManifestWriteTime_.reset();
            return changed;
        }
        const auto writeTime = std::filesystem::last_write_time(nativeManifestPath_, error);
        if (error || (nativeManifestWriteTime_.has_value() && *nativeManifestWriteTime_ == writeTime))
            return false;
        nativeManifestWriteTime_ = writeTime;
        return true;
    }
}  // namespace Horo::Editor
