/** @file ArtifactManifest.h
 *  @brief Artifact manifest schema and generation for build provenance.
 *
 *  Defines the JSON manifest that accompanies every build artifact produced
 *  by the Horo Engine release pipeline.  The manifest captures engine
 *  identity, build environment metadata, and content summary so that
 *  downstream tooling and inspection can verify artifact provenance.
 */

#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace Horo::Build {

// ============================================================================
// Manifest data model
// ============================================================================

/** @brief Artifact identity fields set by the author. */
struct ManifestArtifactInfo {
    std::string name;            /**< Human-readable artifact/application name. */
    std::string version;         /**< Semantic version (e.g. "1.0.0"). */
    std::string buildNumber;     /**< Incremental build counter. */
    std::string releaseChannel;  /**< Release channel label (e.g. "stable"). */
};

/** @brief Build environment provenance captured at packaging time. */
struct ManifestBuildInfo {
    std::string gitSha;      /**< Full Git commit SHA (40 hex chars). */
    std::string platform;    /**< Target OS label (Windows/macOS/Linux). */
    std::string arch;        /**< Target architecture (x86_64/arm64). */
    std::string compiler;    /**< Compiler identification string. */
    std::string config;      /**< Build configuration (Debug/Release/MinSizeRel). */
    std::string timestamp;   /**< ISO-8601 UTC timestamp of manifest generation. */
};

/** @brief Summary of the archived contents. */
struct ManifestContentsInfo {
    uint32_t assetCount = 0;        /**< Number of assets in the archive. */
    std::string archivePath;        /**< Relative path to the .horo archive. */
    uint64_t archiveSizeBytes = 0;  /**< Size of the archive in bytes. */
    std::string archiveSha256;      /**< Hex-encoded SHA-256 of the archive file. */
};

/** @brief Complete artifact manifest, serialisable to/from JSON.
 *
 *  The manifest is designed to be deterministic across identical builds
 *  except for the `build.timestamp` field, which captures wall-clock time. */
struct ArtifactManifest {
    /** Schema version of the manifest format.  Currently "1". */
    inline static constexpr const char *kManifestVersion = "1";

    std::string manifestVersion = std::string(kManifestVersion);
    std::string engineVersion;      /**< Horo Engine version (e.g. "0.0.1"). */

    ManifestArtifactInfo artifact;
    ManifestBuildInfo build;
    ManifestContentsInfo contents;
};

// ============================================================================
// Generation API
// ============================================================================

/** @brief Detects the current host platform string.
 *  @return "Windows", "macOS", or "Linux". */
std::string DetectPlatform();

/** @brief Detects the current host architecture string.
 *  @return "x86_64" or "arm64". */
std::string DetectArchitecture();

/** @brief Returns a compiler identification string for the current build.
 *
 *  Captures compiler name + version for build provenance.
 *  @return Compiler string (e.g. "Clang 15.0.0" or "MSVC 19.38"). */
std::string DetectCompiler();

/** @brief Attempts to resolve the current Git commit SHA.
 *
 *  Precedence:
 *  1. `HORO_GIT_SHA` environment variable.
 *  2. `git rev-parse HEAD` executed from the working directory.
 *  Returns "unknown" on failure.
 *
 *  @return 40-character hex SHA or "unknown". */
std::string ResolveGitSha();

/** @brief Resolves the Git commit SHA from a specific repository path.
 *  @param repoPath  Path to the git repository root.
 *  @return 40-character hex SHA or "unknown". */
std::string ResolveGitSha(const std::filesystem::path &repoPath);

/** @brief Computes the SHA-256 hex digest of a file.
 *  @param filePath  Path to the file to hash.
 *  @return 64-character lowercase hex string, or empty on failure. */
std::string ComputeSha256Hex(const std::filesystem::path &filePath);

// ============================================================================
// Serialisation
// ============================================================================

/** @brief Serialises an ArtifactManifest to a JSON string.
 *
 *  Produces compact, deterministic JSON with sorted keys.
 *  @param manifest  Manifest to serialise.
 *  @return JSON string. */
std::string SerializeManifest(const ArtifactManifest &manifest);

/** @brief Parses an ArtifactManifest from a JSON string.
 *  @param json  JSON string.
 *  @param outManifest  Populated on success.
 *  @return True if the JSON was valid and the manifest was parsed. */
bool DeserializeManifest(std::string_view json, ArtifactManifest &outManifest);

/** @brief Convenience: writes a manifest to a JSON file.
 *  @param path      Destination file path.
 *  @param manifest  Manifest to write.
 *  @return True on success. */
bool WriteManifestFile(const std::filesystem::path &path,
                       const ArtifactManifest &manifest);

/** @brief Convenience: reads and parses a manifest from a JSON file.
 *  @param path         Source file path.
 *  @param outManifest  Populated on success.
 *  @return True on success. */
bool ReadManifestFile(const std::filesystem::path &path,
                      ArtifactManifest &outManifest);

/** @brief Generates a complete manifest for a built artifact.
 *
 *  Fills in all provenance fields automatically:
 *  - engineVersion from HORO_ENGINE_VERSION compile-time define
 *  - build fields from host detection + git resolution
 *  - contents from the provided info struct
 *
 *  @param targetOS   Target operating system string.
 *  @param targetArch Target architecture string.
 *  @return Populated ArtifactManifest. */
ArtifactManifest GenerateManifest(const ManifestArtifactInfo &artifact,
                                  std::string_view config,
                                  const ManifestContentsInfo &contents,
                                  std::string_view targetOS,
                                  std::string_view targetArch);

} // namespace Horo::Build
