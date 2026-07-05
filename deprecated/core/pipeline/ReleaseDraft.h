/** @file ReleaseDraft.h
 *  @brief Build pipeline draft management: job state queries, progress
 *         computation, platform selection, job queue rebuild, and general
 *         build/release utility functions.
 *
 *  Extraction from ReleasePipeline.h — HORO-32 P7.1.
 */
#pragma once

#include "core/pipeline/ReleaseTypes.h"

#include <filesystem>
#include <string>
#include <string_view>

namespace Horo::Build {

/** @brief Returns true when any job in the draft has status Building.
 *
 *  @param draft  Pipeline draft to inspect.
 *  @return       True iff at least one job has BuildJobStatus::Building. */
bool IsAnyBuildRunning(const BuildPipelineDraft &draft);

/** @brief Computes the aggregate completion percentage across all jobs.
 *
 *  Counts jobs in terminal states (Success, Failed, Cancelled) and divides
 *  by the total job count.  Returns 0 when the job list is empty.
 *
 *  @param draft  Pipeline draft to evaluate.
 *  @return       Integer in [0, 100]. */
int ComputeTotalProgress(const BuildPipelineDraft &draft);

/** @brief Creates a BuildJob with status Pending and the given target triple.
 *
 *  All other fields (outputPath, log, error, timestamp) are left empty.
 *
 *  @param os      Target operating system.
 *  @param arch    Target architecture.
 *  @param config  Build configuration (Debug/Release/MinSizeRel).
 *  @return        Newly constructed job in Pending state. */
BuildJob MakePendingJob(BuildTargetOS os, BuildArch arch, BuildConfig config);

/** @brief Derives the set of targeted platforms from the draft job list.
 *
 *  Each flag in the returned PlatformSelection is true iff at least one job
 *  in the draft targets that OS.  An empty draft produces an all-false result.
 *
 *  @param draft  Pipeline draft to inspect.
 *  @return       PlatformSelection reflecting the union of job targets. */
PlatformSelection GetPlatformSelection(const BuildPipelineDraft &draft);

/** @brief Returns the build configuration of the first job in the draft.
 *
 *  All jobs are assumed to share one configuration.  Returns Release
 *  when the draft has no jobs.
 *
 *  @param draft  Pipeline draft to inspect.
 *  @return       Config of the first job, or BuildConfig::Release if empty. */
BuildConfig CurrentBuildConfig(const BuildPipelineDraft &draft);

/** @brief Replaces the draft job queue from a platform selection.
 *
 *  Clears draft.jobs and creates one Pending job per enabled platform using
 *  the current build configuration.  When @p selection has all flags false,
 *  falls back to adding one job for the host platform.
 *
 *  @param draft      Pipeline draft to modify (jobs are replaced in-place).
 *  @param selection  Desired target platforms. */
void RebuildJobsForSelection(BuildPipelineDraft &draft, PlatformSelection selection);

/** @brief Returns the default output root directory for build artifacts.
 *
 *  Returns "\<projectRoot\>/build/release" when @p projectRoot is non-empty;
 *  falls back to "\<cwd\>/build/release" otherwise.
 *
 *  @param projectRoot  Root directory of the game project.
 *  @return             Generic-format path string. */
std::string ResolveDefaultOutputRoot(const std::filesystem::path &projectRoot);

/** @brief Resolves the filesystem path for persisting a build job's output log.
 *
 *  Returns "<outputRoot>/logs/<timestamp>_<os>_<arch>_<config>.log".
 *  Creates the logs subdirectory automatically.  The timestamp in the
 *  filename is sanitised (':' replaced with '-') for cross-platform
 *  compatibility.
 *
 *  @param draft       Build pipeline draft (provides outputRoot).
 *  @param job         The build job (provides os, arch, config, timestamp).
 *  @param projectRoot Project root used when outputRoot is empty.
 *  @return            Absolute or relative path to the log file. */
std::string ResolveJobLogPath(const BuildPipelineDraft &draft,
                               const BuildJob &job,
                               const std::filesystem::path &projectRoot);

/** @brief Returns the default release version tag suggestion.
 *
 *  Returns "v" prefixed to the engine version (e.g. "v0.0.1").
 *  Used to pre-populate the version field in a new pipeline draft when the
 *  user has not yet entered a tag.
 *
 *  @return Engine version prefixed with "v". */
std::string DefaultVersionTag();

/** @brief Returns the current UTC wall-clock time as an ISO-8601 string.
 *
 *  Format: "YYYY-MM-DDTHH:MM:SSZ" (precision: whole seconds).
 *
 *  @return ISO-8601 UTC timestamp string. */
std::string CurrentTimestamp();

/** @brief Formats an ISO-8601 timestamp for compact recent-run UI display.
 *
 *  Returns "Today, HH:MM", "Yesterday, HH:MM", or "YYYY-MM-DD HH:MM"
 *  depending on how old the timestamp is relative to the current UTC time.
 *  Falls back to the raw input string when parsing fails or the input is
 *  shorter than 16 characters.
 *
 *  @param timestamp  ISO-8601 timestamp string (e.g. "2024-06-01T14:30:00Z").
 *  @return           Human-readable relative or absolute time string. */
std::string FormatRecentRunTimestamp(std::string_view timestamp);



} // namespace Horo::Build
