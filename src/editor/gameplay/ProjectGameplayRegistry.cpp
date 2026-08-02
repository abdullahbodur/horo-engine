#include "editor/gameplay/ProjectGameplayRegistry.h"

#include "Horo/Gameplay/GameplayErrors.h"

#include <algorithm>
#include <fstream>
#include <nlohmann/json.hpp>
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
            const std::uintmax_t size = std::filesystem::file_size(path, error);
            if (error || size == 0 || size > MaximumGameplayManifestBytes)
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
            for (const std::filesystem::path root : {projectRoot / "source" / "gameplay", projectRoot / "src" / "gameplay"}) {
                std::error_code error;
                if (!std::filesystem::is_directory(root, error))
                    continue;
                for (std::filesystem::recursive_directory_iterator
                         iterator(root, std::filesystem::directory_options::skip_permission_denied, error),
                     end;
                     iterator != end && !error; iterator.increment(error)) {
                    if (iterator->is_regular_file(error) &&
                        (iterator->path().extension() == ".cpp" || iterator->path().extension() == ".cc" ||
                         iterator->path().extension() == ".cxx") &&
                        // GameModule.cpp is the generated bootstrap created by the editor;
                        // it is not evidence that the project contains a native behavior.
                        iterator->path().filename() != "GameModule.cpp")
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
        auto result = std::unique_ptr<ProjectGameplayRegistry>(new ProjectGameplayRegistry());
        const std::filesystem::path nativeManifestPath = projectRoot / ".horo" / "local" / "gameplay_module.json";
        result->nativeManifestPath_ = nativeManifestPath;
        std::error_code nativeFilesystemError;
        const bool nativeManifestExists = std::filesystem::exists(nativeManifestPath, nativeFilesystemError);
        if (!nativeFilesystemError && nativeManifestExists)
            result->nativeManifestWriteTime_ = std::filesystem::last_write_time(nativeManifestPath, nativeFilesystemError);
        if (!nativeFilesystemError && nativeManifestExists && std::filesystem::is_regular_file(nativeManifestPath, nativeFilesystemError)) {
            Result<NativeArtifactManifest> manifest = ReadNativeArtifactManifest(nativeManifestPath);
            if (manifest.HasError()) {
                result->diagnostics_.push_back({nativeManifestPath, manifest.ErrorValue()});
            } else if (manifest.Value().buildFingerprint != Gameplay::CurrentGameplayBuildFingerprint()) {
                result->diagnostics_.push_back(
                    {nativeManifestPath, ManifestError("the artifact was built for a different Horo SDK generation.")});
            } else {
                Gameplay::GameModuleHost host;
                Result<std::unique_ptr<Gameplay::LoadedGameModule>> loaded =
                    host.LoadShadowCopy(manifest.Value().artifactPath, projectRoot / ".horo" / "local" / "gameplay_module_shadow",
                                        Gameplay::CurrentGameplayBuildFingerprint());
                if (loaded.HasError()) {
                    result->diagnostics_.push_back({manifest.Value().artifactPath, loaded.ErrorValue()});
                } else if (loaded.Value()->ModuleId() != manifest.Value().moduleId ||
                           loaded.Value()->DescriptorRevision() != manifest.Value().descriptorRevision) {
                    result->diagnostics_.push_back(
                        {nativeManifestPath, ManifestError("module identity or descriptor revision does not match the artifact.")});
                } else {
                    result->nativeModule_ = std::move(loaded).Value();
                    for (const Gameplay::BehaviorRegistration &registration : result->nativeModule_->Registry().Registrations()) {
                        if (Result<void> registered = result->registry_.Register(registration); registered.HasError()) {
                            result->diagnostics_.push_back({manifest.Value().artifactPath, registered.ErrorValue()});
                            break;
                        }
                    }
                }
            }
        } else if (!nativeFilesystemError && nativeManifestExists) {
            result->diagnostics_.push_back({nativeManifestPath, ManifestError("manifest path is not a regular file.")});
        } else if (!nativeFilesystemError && HasNativeGameplaySources(projectRoot)) {
            result->diagnostics_.push_back(
                {nativeManifestPath, ManifestError("native gameplay sources exist, but no successful module build has been published.")});
        } else if (nativeFilesystemError) {
            result->diagnostics_.push_back({nativeManifestPath, ManifestError(nativeFilesystemError.message())});
        }

        const std::filesystem::path scriptsRoot = projectRoot / "assets" / "scripts";
        std::error_code filesystemError;
        if (!std::filesystem::is_directory(scriptsRoot, filesystemError)) {
            const Result<void> frozen = result->registry_.Freeze();
            if (frozen.HasError())
                result->diagnostics_.push_back({scriptsRoot, frozen.ErrorValue()});
            return result;
        }

        std::vector<std::filesystem::path> sources;
        for (std::filesystem::recursive_directory_iterator
                 iterator(scriptsRoot, std::filesystem::directory_options::skip_permission_denied, filesystemError),
             end;
             iterator != end && !filesystemError; iterator.increment(filesystemError)) {
            if (!iterator->is_regular_file(filesystemError) || iterator->path().extension() != ".horo_script")
                continue;
            if (sources.size() == MaximumDiscoveredScripts) {
                result->diagnostics_.push_back({scriptsRoot, MakeError(Gameplay::GameplayErrors::InvalidBehaviorComponent,
                                                                       "Project script discovery exceeds the supported asset count.")});
                break;
            }
            sources.push_back(iterator->path());
        }
        if (filesystemError)
            result->diagnostics_.push_back(
                {scriptsRoot, MakeError(Gameplay::GameplayErrors::InvalidBehaviorComponent, filesystemError.message())});

        std::ranges::sort(sources);
        for (const std::filesystem::path &source : sources) {
            auto loaded = Gameplay::LuaBehaviorProgram::LoadFiles(source, source.string() + ".meta");
            if (loaded.HasError()) {
                result->diagnostics_.push_back({source, loaded.ErrorValue()});
                continue;
            }
            std::unique_ptr<Gameplay::LuaBehaviorProgram> program = std::move(loaded).Value();
            Result<void> registered = result->registry_.Register(program->Registration());
            if (registered.HasError()) {
                result->diagnostics_.push_back({source, registered.ErrorValue()});
                continue;
            }
            result->luaPrograms_.push_back(std::move(program));
            result->luaSources_.push_back(source);
            result->luaSourceStats_.push_back(ReadLuaSourceStat(source, filesystemError));
            filesystemError.clear();
        }

        Result<void> frozen = result->registry_.Freeze();
        if (frozen.HasError())
            result->diagnostics_.push_back({scriptsRoot, frozen.ErrorValue()});
        return result;
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
        for (std::size_t index = 0; index < luaPrograms_.size(); ++index) {
            std::error_code error;
            const LuaSourceStat sourceStat = ReadLuaSourceStat(luaSources_[index], error);
            if (error) {
                diagnostics.push_back({luaSources_[index], MakeError(Gameplay::GameplayErrors::InvalidBehaviorComponent, error.message())});
                continue;
            }
            if (sourceStat == luaSourceStats_[index])
                continue;
            luaSourceStats_[index] = sourceStat;
            auto candidate = Gameplay::LuaBehaviorProgram::LoadFiles(luaSources_[index], luaSources_[index].string() + ".meta");
            if (candidate.HasError()) {
                diagnostics.push_back({luaSources_[index], candidate.ErrorValue()});
                continue;
            }
            Result<void> replaced = luaPrograms_[index]->ReplaceCompatible(std::move(candidate).Value());
            if (replaced.HasError())
                diagnostics.push_back({luaSources_[index], replaced.ErrorValue()});
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
