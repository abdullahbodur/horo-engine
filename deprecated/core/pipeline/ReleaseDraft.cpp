/** @file ReleaseDraft.cpp
 *  @brief Build pipeline draft management and general utility implementations.
 *
 *  Extraction from ReleasePipeline.cpp — HORO-32 P7.1.
 */
#include "core/pipeline/ReleaseDraft.h"

#include "core/BuildVersion.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <format>
#include <iomanip>
#include <sstream>

namespace Horo::Build {

/** @copydoc IsAnyBuildRunning */
bool IsAnyBuildRunning(const BuildPipelineDraft &draft) {
    using enum BuildJobStatus;
    return std::ranges::any_of(draft.jobs, [](const BuildJob &j) {
        return j.status == Building;
    });
}

/** @copydoc ComputeTotalProgress */
int ComputeTotalProgress(const BuildPipelineDraft &draft) {
    using enum BuildJobStatus;
    if (draft.jobs.empty())
        return 0;
    int completed = 0;
    for (const auto &j : draft.jobs) {
        if (j.status == Success || j.status == Failed || j.status == Cancelled)
            ++completed;
    }
    return static_cast<int>((completed * 100) / draft.jobs.size());
}

/** @copydoc MakePendingJob */
BuildJob MakePendingJob(BuildTargetOS os, BuildArch arch, BuildConfig config) {
    BuildJob job;
    job.os = os;
    job.arch = arch;
    job.config = config;
    job.status = BuildJobStatus::Pending;
    return job;
}

/** @copydoc GetPlatformSelection */
PlatformSelection GetPlatformSelection(const BuildPipelineDraft &draft) {
    using enum BuildTargetOS;
    PlatformSelection selection;
    for (const auto &job : draft.jobs) {
        selection.windowsSelected =
            selection.windowsSelected || job.os == Windows;
        selection.macOSSelected =
            selection.macOSSelected || job.os == MacOS;
        selection.linuxSelected =
            selection.linuxSelected || job.os == Linux;
    }
    return selection;
}

/** @copydoc CurrentBuildConfig */
BuildConfig CurrentBuildConfig(const BuildPipelineDraft &draft) {
    return draft.jobs.empty() ? BuildConfig::Release : draft.jobs.front().config;
}

/** @copydoc RebuildJobsForSelection */
void RebuildJobsForSelection(BuildPipelineDraft &draft, PlatformSelection selection) {
    using enum BuildTargetOS;
    const BuildConfig config = CurrentBuildConfig(draft);
    constexpr BuildArch kDefaultArch = BuildArch::x86_64;
    draft.jobs.clear();
    if (selection.windowsSelected)
        draft.jobs.push_back(MakePendingJob(Windows, kDefaultArch, config));
    if (selection.macOSSelected)
        draft.jobs.push_back(MakePendingJob(MacOS, BuildArch::Arm64, config));
    if (selection.linuxSelected)
        draft.jobs.push_back(MakePendingJob(Linux, kDefaultArch, config));

    if (!draft.jobs.empty())
        return;

#if defined(_WIN32)
    draft.jobs.push_back(MakePendingJob(Windows, kDefaultArch, config));
#elif defined(__APPLE__)
    draft.jobs.push_back(MakePendingJob(MacOS, BuildArch::Arm64, config));
#else
    draft.jobs.push_back(MakePendingJob(Linux, kDefaultArch, config));
#endif
}

/** @copydoc ResolveDefaultOutputRoot */
std::string ResolveDefaultOutputRoot(const std::filesystem::path &projectRoot) {
    namespace fs = std::filesystem;
    if (!projectRoot.empty())
        return (projectRoot / "build" / "release").generic_string();
    return (fs::current_path() / "build" / "release").generic_string();
}

/** @copydoc DefaultVersionTag */
std::string DefaultVersionTag() {
    return std::string("v") + EngineVersion();
}

/** @copydoc CurrentTimestamp */
std::string CurrentTimestamp() {
    const auto now = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &time);
#else
    gmtime_r(&time, &tm);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}

/** @copydoc FormatRecentRunTimestamp */
std::string FormatRecentRunTimestamp(std::string_view timestamp) {
    if (timestamp.size() < 16)
        return std::string(timestamp);

    std::tm runTm{};
    std::istringstream input(std::string(timestamp.substr(0, 19)));
    input >> std::get_time(&runTm, "%Y-%m-%dT%H:%M:%S");
    if (input.fail())
        return std::string(timestamp);

    const auto now = std::chrono::system_clock::now();
    const auto nowTime = std::chrono::system_clock::to_time_t(now);
    std::tm nowTm{};
#if defined(_WIN32)
    gmtime_s(&nowTm, &nowTime);
#else
    gmtime_r(&nowTime, &nowTm);
#endif
    std::tm yesterdayTm = nowTm;
    yesterdayTm.tm_mday -= 1;
    std::mktime(&yesterdayTm);

    const auto sameDay = [](const std::tm &a, const std::tm &b) {
        return a.tm_year == b.tm_year && a.tm_mon == b.tm_mon && a.tm_mday == b.tm_mday;
    };

    std::ostringstream out;
    if (sameDay(runTm, nowTm)) {
        out << "Today, ";
    } else if (sameDay(runTm, yesterdayTm)) {
        out << "Yesterday, ";
    } else {
        out << std::put_time(&runTm, "%Y-%m-%d ");
    }
    out << std::put_time(&runTm, "%H:%M");
    return out.str();
}



/** @copydoc ResolveJobLogPath */
std::string ResolveJobLogPath(const BuildPipelineDraft &draft,
                               const BuildJob &job,
                               const std::filesystem::path &projectRoot) {
    const std::string outputRoot = draft.outputRoot.empty()
        ? ResolveDefaultOutputRoot(projectRoot) : draft.outputRoot;
    const std::string logDir = outputRoot + "/logs";
    std::filesystem::create_directories(logDir);

    // Sanitise timestamp for cross-platform filename compatibility.
    std::string safeTimestamp = job.timestamp;
    for (char &c : safeTimestamp) {
        if (c == ':') c = '-';
    }

    return std::format("{}/{}_{}_{}_{}.log",
                       logDir, safeTimestamp,
                       GetBuildTargetOSLabel(job.os),
                       GetBuildArchLabel(job.arch),
                       GetBuildConfigLabel(job.config));
}

} // namespace Horo::Build
