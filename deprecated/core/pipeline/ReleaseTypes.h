/** @file ReleaseTypes.h
 *  @brief Core build and release pipeline data model: enums, structs, and
 *         label getter declarations.
 *
 *  Extraction from ReleasePipeline.h — HORO-32 P7.1.
 */
#pragma once

#include "core/SecureString.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "core/pipeline/BuildDiagnostics.h"

namespace Horo::Build {

/** @brief Identifies a target operating system for a build job. */
enum class BuildTargetOS {
    Windows, /**< Windows (MSVC, x86_64). */
    MacOS,   /**< macOS (Clang, arm64 / x86_64). */
    Linux,   /**< Linux (GCC, x86_64). */
};

/** @brief Returns the display label for a build target OS. */
const char *GetBuildTargetOSLabel(BuildTargetOS os);

/** @brief Identifies a build configuration (optimisation level). */
enum class BuildConfig {
    Debug,      /**< Debug symbols, no optimisations. */
    Release,    /**< Full optimisations, stripped. */
    MinSizeRel, /**< Optimised for minimal binary size. */
};

/** @brief Returns the display label for a build configuration. */
const char *GetBuildConfigLabel(BuildConfig config);

/** @brief Architecture target for a build job. */
enum class BuildArch {
    x86_64, /**< 64-bit x86 (Intel / AMD). */
    Arm64,  /**< 64-bit ARM (Apple Silicon, etc.). */
};

/** @brief Returns the display label for a build architecture. */
const char *GetBuildArchLabel(BuildArch arch);

/** @brief Current lifecycle status of a single build job. */
enum class BuildJobStatus {
    Pending,   /**< Queued, not yet started. */
    Building,  /**< Currently executing. */
    Success,   /**< Completed successfully. */
    Failed,    /**< Completed with errors. */
    Cancelled, /**< Cancelled by the user. */
};

/** @brief Returns the display label for a build job status. */
const char *GetBuildJobStatusLabel(BuildJobStatus status);

/** @brief State machine for the overall build pipeline workflow. */
enum class BuildPipelineState {
    Idle,         /**< No build in progress, modal closed or idle. */
    Configuring,  /**< User is editing build settings. */
    Building,     /**< Build is executing locally or dispatched to CI. */
    Packaging,    /**< Post-build artifact packaging in progress. */
    Downloading,  /**< Fetching remote build artifacts. */
    Done,         /**< Build completed successfully. */
    Error,        /**< Build failed or was cancelled. */
};

/** @brief Returns the display label for a pipeline state. */
const char *GetBuildPipelineStateLabel(BuildPipelineState state);

/** @brief Returns true if the state is terminal (Done or Error). */
bool IsTerminalBuildPipelineState(BuildPipelineState state);

/** @brief Returns true if the transition from -> to is valid. */
bool CanTransitionBuildPipelineState(BuildPipelineState from, BuildPipelineState to);


/** @brief Describes a single build target (one OS x one arch x one config). */
struct BuildJob {
    BuildTargetOS os = BuildTargetOS::Windows;
    BuildArch arch = BuildArch::x86_64;
    BuildConfig config = BuildConfig::Release;
    BuildJobStatus status = BuildJobStatus::Pending;
    int exitCode = 0;
    std::string outputPath; /**< Filesystem path where the artifact was written. */
    std::string log;        /**< Captured stdout/stderr from the build process. */
    std::string logPath;    /**< Filesystem path to persisted build log file. */
    std::string error;      /**< Human-readable error message when status is Failed. */
    Horo::Build::BuildFailureDiagnostic diagnostic;
    std::string timestamp;  /**< ISO-8601 timestamp of job completion. */

};

/** @brief Code-signing configuration persisted across sessions.
 *
 *  @note  SigningConfig is move-only: the certificate password is held in a
 *         SecureString, which deletes copy operations. Use explicit Clone()
 *         when a deep copy is needed (e.g. dirty-tracking snapshots). */
struct SigningConfig {
    SigningConfig() = default;

    // ── Move-only ─────────────────────────────────────────────────────
    SigningConfig(SigningConfig&&) = default;
    SigningConfig& operator=(SigningConfig&&) = default;
    SigningConfig(const SigningConfig&) = delete;
    SigningConfig& operator=(const SigningConfig&) = delete;

    bool enabled = false;
    std::string certificatePath;          /**< Path to signing certificate / .pfx file. */
    Horo::Core::SecureString certificatePassword; /**< Password for the certificate (zeroed on destruction). */
    bool notarize = false;                /**< Submit for Apple notarization (macOS only). */
    bool hardenedRuntime = true;          /**< Enable hardened runtime protections. */
    bool verifySignature = true;          /**< Validate the signature after signing. */
    std::string appleId;                  /**< Apple ID for notarization. */
    std::string teamId;                   /**< Apple Team ID for notarization. */
    std::string keychainProfile;          /**< Keychain profile name for notarytool. */

    /** @brief Creates a deep copy of the signing configuration.
     *
     *  The certificatePassword is explicitly cloned via SecureString's
     *  constructor from string_view — this is deliberate and intentional
     *  (no accidental copies). */
    SigningConfig Clone() const {
        SigningConfig copy;
        copy.enabled = enabled;
        copy.certificatePath = certificatePath;
        copy.certificatePassword = Horo::Core::SecureString(certificatePassword.View());
        copy.notarize = notarize;
        copy.hardenedRuntime = hardenedRuntime;
        copy.verifySignature = verifySignature;
        copy.appleId = appleId;
        copy.teamId = teamId;
        copy.keychainProfile = keychainProfile;
        return copy;
    }
};

/** @brief Complete in-progress draft state for a build pipeline session.
 *
 *  @note  BuildPipelineDraft is move-only because archivePassword is a
 *         SecureString. Use explicit Clone() when a deep copy is needed. */
struct BuildPipelineDraft {
    BuildPipelineDraft() = default;

    // ── Move-only ─────────────────────────────────────────────────────
    BuildPipelineDraft(BuildPipelineDraft&&) = default;
    BuildPipelineDraft& operator=(BuildPipelineDraft&&) = default;
    BuildPipelineDraft(const BuildPipelineDraft&) = delete;
    BuildPipelineDraft& operator=(const BuildPipelineDraft&) = delete;

    std::vector<BuildJob> jobs;         /**< Configured build jobs. */
    std::string buildName;              /**< Human-readable build artifact/application name. */
    std::string versionTag;             /**< Release version tag (e.g. \"v0.3.0\"). */
    std::string outputRoot;             /**< Root directory for build artifacts. */
    int contentSelection = 0;            /**< Content/scenes preset index for packaging. */
    std::string gameVersion;            /**< Semantic game version, e.g. \"1.0.0\". */
    std::string buildNumber;            /**< Incremental build number. */
    int releaseChannel = 0;              /**< Release channel preset index. */
    std::string packageIdentifier;       /**< Bundle/package identifier. */

    Horo::Core::SecureString archivePassword; /**< Optional password for encrypting release archives. */
    SigningConfig signing;              /**< Code-signing configuration. */
    bool allJobsComplete = false;       /**< True when every job reached a terminal status. */
    bool anyJobFailed = false;          /**< True when at least one job reached Failed status. */
    int totalProgress = 0;              /**< Aggregate progress 0-100 across all jobs. */

    /** @brief Creates a deep copy of the entire draft.
     *
     *  SecureString fields (archivePassword, signing.certificatePassword)
     *  are explicitly cloned — no accidental secret duplication. */
    BuildPipelineDraft Clone() const {
        BuildPipelineDraft copy;
        copy.jobs = jobs;
        copy.buildName = buildName;
        copy.versionTag = versionTag;
        copy.outputRoot = outputRoot;
        copy.contentSelection = contentSelection;
        copy.gameVersion = gameVersion;
        copy.buildNumber = buildNumber;
        copy.releaseChannel = releaseChannel;
        copy.packageIdentifier = packageIdentifier;

        copy.archivePassword = Horo::Core::SecureString(archivePassword.View());
        copy.signing = signing.Clone();
        copy.allJobsComplete = allJobsComplete;
        copy.anyJobFailed = anyJobFailed;
        copy.totalProgress = totalProgress;
        return copy;
    }
};

/** @brief Snapshot of a completed build session, persisted to history. */
struct BuildHistoryEntry {
    std::string versionTag;       /**< Release version tag. */
    std::string timestamp;        /**< ISO-8601 timestamp when the session finished. */
    std::vector<BuildJob> jobs;   /**< Jobs from the session. */
    bool allSucceeded = false;    /**< True when every job succeeded. */
};

/** @brief Mutable target-platform checkbox state derived from draft jobs. */
struct PlatformSelection {
    bool windowsSelected = false; /**< True when Windows is selected. */
    bool macOSSelected = false;   /**< True when macOS is selected. */
    bool linuxSelected = false;   /**< True when Linux is selected. */
};

/** @brief Process-launch-neutral command plan for one build step. */
struct BuildCommandPlan {
    std::string executable;                    /**< Program to launch. */
    std::vector<std::string> args;             /**< Program arguments. */
    std::filesystem::path workingDirectory;    /**< Working directory for the process. */
    std::string debugString;                   /**< Human-readable shell command. */
};



} // namespace Horo::Build
