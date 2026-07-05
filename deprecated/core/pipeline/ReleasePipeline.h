/** @file ReleasePipeline.h
 *  @brief Umbrella header for the Horo build and release pipeline.
 *
 *  After HORO-32 P7.1 extraction, this header re-exports the split modules:
 *  - core/pipeline/ReleaseTypes.h   (enums, structs)
 *  - core/pipeline/ReleaseDraft.h   (draft management)
 *  - core/pipeline/ReleaseCommand.h (command building)
 *  - core/pipeline/ReleaseHistory.h (history persistence)
 *
 *  Remaining surface: label getters, artifact manifest and SemVer helpers.
 */
#pragma once

// Re-export extracted pipeline modules for backward compatibility.
#include <string>
#include <string_view>

#include "ReleaseCommand.h"
#include "ReleaseDraft.h"
#include "ReleaseHistory.h"
#include "ReleaseTypes.h"

namespace Horo::Build {
 /** @brief Lightweight semver wrapper for editor UX.
 *
 *  Simplifies the Horo::Version type to just major.minor.patch for
 *  pipeline display and bump workflows. */
struct SemVer {
    int major = 0;
    int minor = 0;
    int patch = 0;

    /** @brief True when all numeric components are zero (parse-failure sentinel). */
    bool IsZero() const { return major == 0 && minor == 0 && patch == 0; }
};

/** @brief Parses a version tag into a SemVer struct.
 *
 *  Accepts "v"-prefixed and bare semver strings.  Returns a
 *  zeroed sentinel on parse failure. */
SemVer ParseSemVer(std::string_view tag);

/** @brief Returns true when the tag represents a valid semver. */
bool IsValidSemVer(std::string_view tag);

/** @brief Increments the patch component by one. */
void BumpPatch(SemVer &ver);

/** @brief Returns the canonical string form "major.minor.patch". */
std::string SemVerToString(const SemVer &ver);

/** @brief Returns the Horo Engine version string from project() VERSION.
 *
 *  Guaranteed null-terminated and stable for the lifetime of the
 *  process.  Wraps EngineVersion() from core/Version.h.in. */
const char *CurrentEngineVersion();

// -- Artifact manifest ---------------------------------------------------

/** @brief Forward declaration — see core/pipeline/ArtifactManifest.h. */
struct ArtifactManifest;

/** @brief Generates a manifest describing artifact identity and build provenance.
 *
 *  Captures engine version, build environment (git SHA, platform, arch,
 *  compiler, config), artifact identity from the draft, and content
 *  summary.  The result is a JSON-serialisable ArtifactManifest ready
 *  for writing alongside the build artifact.
 *
 *  @param draft       Build pipeline draft state.
 *  @param job         The completed build job.
 *  @param projectRoot Root of the game project being built.
 *  @return Populated manifest. */
ArtifactManifest BuildArtifactManifest(const BuildPipelineDraft &draft,
                                       const BuildJob &job,
                                       const std::filesystem::path &projectRoot);

/** @brief Writes an artifact manifest JSON file to the job output directory.
 *
 *  The manifest is written as `.manifest.json` inside the directory
 *  returned by ResolveJobOutputPath().
 *
 *  @param draft       Build pipeline draft state.
 *  @param job         The completed build job.
 *  @param projectRoot Root of the game project being built.
 *  @return True if the manifest was written successfully. */
bool WriteArtifactManifest(const BuildPipelineDraft &draft,
                           const BuildJob &job,
                           const std::filesystem::path &projectRoot);

/** @brief Builds a shell command fragment that writes the manifest JSON.
 *
 *  Constructs the full manifest JSON in C++ and produces a shell heredoc
 *  command that writes it to `.manifest.json` in the output directory.
 *
 *  @param draft       Build pipeline draft state.
 *  @param job         The build job.
 *  @param projectRoot Root of the game project being built.
 *  @return Shell command string, or empty on failure. */
std::string BuildManifestShellCommand(const BuildPipelineDraft &draft,
                                      const BuildJob &job,
                                      const std::filesystem::path &projectRoot);


} // namespace Horo::Build
