/** @file ReleaseCommand.h
 *  @brief Build command construction: shell command generation, process
 *         command plans, output path resolution, and code-signing commands.
 *
 *  Extraction from ReleasePipeline.h — HORO-32 P7.1.
 */
#pragma once

#include "core/pipeline/ReleaseTypes.h"
#include "core/pipeline/TargetCapability.h"

#include <filesystem>
#include <string>

namespace Horo::Build {

struct ToolchainConfig;
class ToolchainSettingsStore;

/** @brief Resolves the CMake package prefix used when configuring a game project.
 *
 *  Queries ProjectPath::SdkRoot() and walks upward to find a directory that
 *  contains HoroEngineConfig.cmake (build-tree or installed prefix).  Falls
 *  back to the raw SdkRoot if no valid prefix is found.
 *
 *  @return Resolved CMake prefix path. Never empty (falls back to SdkRoot). */
std::filesystem::path ResolveBuildSdkPrefix();

/** @brief Builds a configure + compile shell command for a game project.
 *
 *  Produces: cmake --fresh -S <project> -B <project>/build -DCMAKE_PREFIX_PATH=<sdk>
 *  -DCMAKE_BUILD_TYPE=<config> && cmake --build <project>/build --config <config>.
 *  Arguments with spaces are quoted for the host shell.
 *
 *  @param projectRoot  Root directory of the CMake game project.
 *  @param sdkPrefix    CMake package prefix for HoroEngine (-DCMAKE_PREFIX_PATH).
 *  @param config       Build configuration (Debug/Release/MinSizeRel).
 *  @param toolchain    Optional target toolchain configuration.
 *  @return             Shell command string (POSIX or cmd.exe compatible). */
std::string BuildProjectShellCommand(const std::filesystem::path &projectRoot,
                                     const std::filesystem::path &sdkPrefix,
                                     BuildConfig config,
                                     const ToolchainConfig* toolchain = nullptr);

/** @brief Builds a configure + compile + package shell command for a game project.
 *
 *  Extends BuildProjectShellCommand() with a copy of the built binaries into
 *  @p outputPath followed by invoking horopak to archive the assets directory.
 *  When @p outputPath is empty, returns the bare build command.
 *
 *  @param projectRoot  Root directory of the CMake game project.
 *  @param sdkPrefix    CMake package prefix for HoroEngine.
 *  @param config       Build configuration.
 *  @param outputPath   Destination directory for the packaged artifact.
 *  @param toolchain    Optional target toolchain configuration.
 *  @param toolchain    Optional target toolchain configuration.
 *  @return             Shell command string. */
std::string BuildPackageShellCommand(const std::filesystem::path &projectRoot,
                                     const std::filesystem::path &sdkPrefix,
                                     BuildConfig config,
                                     const std::filesystem::path &outputPath,
                                     const std::filesystem::path &archivePath,
                                     const ToolchainConfig* toolchain = nullptr);

/** @brief Builds a process command plan for a local game project build (no packaging).
 *
 *  Sets executable to "/bin/sh" (POSIX) or "cmd" (Windows) and builds the
 *  configure + compile command from ResolveBuildSdkPrefix().
 *  The working directory is @p projectRoot or the current directory if empty.
 *
 *  @param projectRoot  Root directory of the game project.
 *  @param config       Build configuration.
 *  @param toolchain    Optional target toolchain configuration.
 *  @return             Populated BuildCommandPlan. */
BuildCommandPlan CreateBuildCommandPlan(const std::filesystem::path &projectRoot,
                                        BuildConfig config,
                                        const ToolchainConfig* toolchain = nullptr);

/** @brief Builds a process command plan for one draft job including artifact packaging.
 *
 *  For macOS jobs this creates an .app bundle and packages assets into
 *  Contents/Resources. Other jobs stage the generated binary directory and
 *  package assets with horopak.
 *  The plan uses the same executable (/bin/sh or cmd.exe) as the no-packaging overload.
 *
 *  @param draft        Current pipeline draft (provides buildName, versionTag, gameVersion).
 *  @param job          The specific job to build a command for.
 *  @param projectRoot  Root directory of the game project.
 *  @param store        Optional toolchain settings store for cross-compile resolving.
 *  @return             Populated BuildCommandPlan. */
BuildCommandPlan CreateBuildCommandPlan(const BuildPipelineDraft &draft,
                                        const BuildJob &job,
                                        const std::filesystem::path &projectRoot,
                                        const ToolchainSettingsStore* store = nullptr);

/** @brief Returns the human-readable shell command string for one draft job.
 *
 *  Convenience wrapper that returns CreateBuildCommandPlan().debugString.
 *
 *  @param draft        Current pipeline draft.
 *  @param job          Target job.
 *  @param projectRoot  Root directory of the game project.
 *  @param store        Optional toolchain settings store.
 *  @return             Shell command string suitable for display in the UI. */
std::string BuildCommandForJob(const BuildPipelineDraft &draft,
                               const BuildJob &job,
                               const std::filesystem::path &projectRoot,
                               const ToolchainSettingsStore* store = nullptr);

/** @brief Returns the code-signing shell command for one draft job, or empty.
 *
 *  Returns an empty string when signing is disabled (draft.signing.enabled ==
 *  false) or when no signing command is applicable for the job's OS.
 *  - macOS + notarize: codesign with the team ID.
 *  - Windows: signtool sign with the certificate and password.
 *  - Linux: empty (no signing command).
 *
 *  @param draft  Current pipeline draft (provides signing configuration).
 *  @param job    Target job (determines platform-specific signing tool).
 *  @return       Shell command string, or empty string if signing is not applicable. */
std::string SignCommandForJob(const BuildPipelineDraft &draft, const BuildJob &job);

/** @brief Resolves the default output path for a completed job using std::filesystem::current_path().
 *
 *  Convenience overload of the three-argument form with the current working
 *  directory as projectRoot.
 *
 *  @param draft  Current pipeline draft (provides outputRoot and versionTag).
 *  @param job    Target job (provides OS and arch for path components).
 *  @return       Generic-format output path string. */
std::string ResolveJobOutputPath(const BuildPipelineDraft &draft, const BuildJob &job);

/** @brief Resolves the default output path for a completed job relative to a project root.
 *
 *  Path format: "\<outputRoot\>/\<versionTag\>_\<os\>_\<arch\>".
 *  Falls back to ResolveDefaultOutputRoot(projectRoot) when draft.outputRoot is empty,
 *  and to DefaultVersionTag() when draft.versionTag is empty.
 *
 *  @param draft        Current pipeline draft.
 *  @param job          Target job.
 *  @param projectRoot  Game project root used to compute the default output root.
 *  @return             Generic-format output path string. */
std::string ResolveJobOutputPath(const BuildPipelineDraft &draft,
                                 const BuildJob &job,
                                 const std::filesystem::path &projectRoot);

/** @brief Resolves the .app bundle root path for a macOS job.
 *  Returns `<outputPath>/<AppName>.app` for macOS jobs, or empty for others. */
std::string ResolveJobAppBundlePath(const BuildPipelineDraft &draft,
                                    const BuildJob &job,
                                    const std::filesystem::path &projectRoot);

/** @brief Resolves the path to the artifact manifest.
 *  For macOS: `<AppName>.app/Contents/Resources/.manifest.json`.
 *  Otherwise: `<outputPath>/.manifest.json`. */
std::string ResolveJobManifestPath(const BuildPipelineDraft &draft,
                                   const BuildJob &job,
                                   const std::filesystem::path &projectRoot);

/** @brief Resolves the path to the assets archive.
 *  For macOS: `<AppName>.app/Contents/Resources/assets.horo`.
 *  Otherwise: `<outputPath>/assets.horo`. */
std::string ResolveJobArchivePath(const BuildPipelineDraft &draft,
                                  const BuildJob &job,
                                  const std::filesystem::path &projectRoot);

} // namespace Horo::Build
