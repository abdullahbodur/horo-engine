/** @file ReleasePipeline.cpp
 *  @brief Remaining implementations in the ReleasePipeline umbrella:
 *         label getters, SemVer helpers, and artifact manifest generation.
 *
 *  After HORO-32 P7.1 extraction:
 *  - types        → ReleaseTypes.h
 *  - draft mgmt   → ReleaseDraft.cpp
 *  - commands     → ReleaseCommand.cpp
 *  - history      → ReleaseHistory.cpp
 */
#include "core/pipeline/ReleasePipeline.h"

#include <format>

#include "core/Version.h"
#include "core/BuildVersion.h"
#include "core/pipeline/ArtifactManifest.h"
#include "core/pipeline/ReleaseCommand.h"

// --------------------------------------------------------------------------
//  Label getters (declared in ReleaseTypes.h, re-exported via this header)
// --------------------------------------------------------------------------

namespace Horo::Build {

/** @copydoc GetBuildTargetOSLabel */
const char *GetBuildTargetOSLabel(BuildTargetOS os) {
    using enum BuildTargetOS;
    switch (os) {
    case Windows: return "Windows";
    case MacOS:   return "macOS";
    case Linux:   return "Linux";
    }
    return "Unknown";
}

/** @copydoc GetBuildConfigLabel */
const char *GetBuildConfigLabel(BuildConfig config) {
    using enum BuildConfig;
    switch (config) {
    case Debug:      return "Debug";
    case Release:    return "Release";
    case MinSizeRel: return "MinSizeRel";
    }
    return "Unknown";
}

/** @copydoc GetBuildArchLabel */
const char *GetBuildArchLabel(BuildArch arch) {
    switch (arch) {
    case BuildArch::x86_64: return "x86_64";
    case BuildArch::Arm64:  return "arm64";
    }
    return "Unknown";
}

/** @copydoc GetBuildJobStatusLabel */
const char *GetBuildJobStatusLabel(BuildJobStatus status) {
    using enum BuildJobStatus;
    switch (status) {
    case Pending:   return "Pending";
    case Building:  return "Building";
    case Success:   return "Success";
    case Failed:    return "Failed";
    case Cancelled: return "Cancelled";
    }
    return "Unknown";
}

// --------------------------------------------------------------------------
//  Pipeline state machine helpers (BuildPipelineState)
// --------------------------------------------------------------------------

/** @copydoc GetBuildPipelineStateLabel */
const char *GetBuildPipelineStateLabel(BuildPipelineState state) {
    using enum BuildPipelineState;
    switch (state) {
    case Idle:        return "Idle";
    case Configuring: return "Configuring";
    case Building:    return "Building";
    case Packaging:   return "Packaging";
    case Downloading: return "Downloading";
    case Done:        return "Done";
    case Error:       return "Error";
    }
    return "Unknown";
}

/** @copydoc IsTerminalBuildPipelineState */
bool IsTerminalBuildPipelineState(BuildPipelineState state) {
    using enum BuildPipelineState;
    return state == Done || state == Error;
}

/** @copydoc CanTransitionBuildPipelineState */
bool CanTransitionBuildPipelineState(BuildPipelineState from,
                                     BuildPipelineState to) {
    using enum BuildPipelineState;
    // Allow reset from anywhere to Idle or Configuring.
    if (to == Idle || to == Configuring)
        return true;

    switch (from) {
    case Idle:        return to == Configuring;
    case Configuring: return to == Building || to == Idle || to == Error;
    // Building: local builds go straight to Done (packaging inline),
    // CI builds go to Downloading (fetch remote artifacts).
    case Building:    return to == Packaging   || to == Error || to == Idle ||
                              to == Downloading || to == Done;
    case Packaging:   return to == Downloading || to == Error;
    case Downloading: return to == Done;
    case Done:        return to == Idle || to == Configuring;
    case Error:       return to == Idle || to == Configuring;
    }
    return false;
}

// --------------------------------------------------------------------------
//  SemVer helpers
// --------------------------------------------------------------------------

/** @copydoc ParseSemVer */
SemVer ParseSemVer(std::string_view tag) {
    const auto parsed = Horo::ParseVersion(tag);
    if (!parsed)
        return {};  // Zeroed sentinel.
    return {parsed->major, parsed->minor, parsed->patch};
}

/** @copydoc IsValidSemVer */
bool IsValidSemVer(std::string_view tag) {
    return Horo::ParseVersion(tag).has_value();
}

/** @copydoc BumpPatch */
void BumpPatch(SemVer &ver) {
    ++ver.patch;
}

/** @copydoc SemVerToString */
std::string SemVerToString(const SemVer &ver) {
    return std::format("{}.{}.{}", ver.major, ver.minor, ver.patch);
}

/** @copydoc CurrentEngineVersion */
const char *CurrentEngineVersion() {
    return EngineVersion();
}

// --------------------------------------------------------------------------
//  Artifact manifest generation
// --------------------------------------------------------------------------

/** @copydoc BuildArtifactManifest */
ArtifactManifest BuildArtifactManifest(const BuildPipelineDraft &draft,
                                       const BuildJob &job,
                                       const std::filesystem::path &projectRoot) {
    ManifestArtifactInfo artifactInfo;
    artifactInfo.name = draft.buildName;
    artifactInfo.version = draft.gameVersion;
    artifactInfo.buildNumber = draft.buildNumber;
    artifactInfo.releaseChannel = std::to_string(draft.releaseChannel);

    ManifestContentsInfo contentsInfo;
    contentsInfo.archivePath = "assets.horo";

    const std::filesystem::path archivePath = ResolveJobArchivePath(draft, job, projectRoot);
    contentsInfo.archiveSha256 = ComputeSha256Hex(archivePath);

    if (std::error_code ec; std::filesystem::exists(archivePath, ec)) {
        contentsInfo.archiveSizeBytes =
            static_cast<uint64_t>(std::filesystem::file_size(archivePath, ec));
        if (ec)
            contentsInfo.archiveSizeBytes = 0;
    }

    return GenerateManifest(artifactInfo,
                            GetBuildConfigLabel(job.config),
                            contentsInfo,
                            GetBuildTargetOSLabel(job.os),
                            GetBuildArchLabel(job.arch));
}

/** @copydoc WriteArtifactManifest */
bool WriteArtifactManifest(const BuildPipelineDraft &draft,
                           const BuildJob &job,
                           const std::filesystem::path &projectRoot) {
    const ArtifactManifest manifest =
        BuildArtifactManifest(draft, job, projectRoot);

    const auto manifestPath = ResolveJobManifestPath(draft, job, projectRoot);
    return WriteManifestFile(manifestPath, manifest);
}

/** @copydoc BuildManifestShellCommand */
std::string BuildManifestShellCommand(const BuildPipelineDraft &draft,
                                      const BuildJob &job,
                                      const std::filesystem::path &projectRoot) {
    const ArtifactManifest manifest =
        BuildArtifactManifest(draft, job, projectRoot);

    const std::string jsonContent = SerializeManifest(manifest);

    const auto manifestPath = ResolveJobManifestPath(draft, job, projectRoot);

#if defined(_WIN32)
    return std::format(
        "powershell -Command \"Set-Content -Path '{}' -Value @'\\n{}\\n'@\"",
        manifestPath, jsonContent);
#else
    return std::format(
        "cat > '{}' << 'HORO_MANIFEST_JSON_END'\\n{}\\nHORO_MANIFEST_JSON_END",
        manifestPath, jsonContent);
#endif
}


} // namespace Horo::Build
