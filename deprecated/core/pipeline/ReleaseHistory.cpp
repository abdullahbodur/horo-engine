/** @file ReleaseHistory.cpp
 *  @brief Build history persistence: JSON serialization/deserialization
 *         of completed build sessions.
 *
 *  Extraction from ReleasePipeline.cpp — HORO-32 P7.1.
 */
#include "core/pipeline/ReleaseHistory.h"

#include <cstdlib>
#include <fstream>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

namespace Horo::Build {
namespace {

/** @brief Reads an environment variable without tripping MSVC secure CRT warnings. */
std::string ReadEnvironmentVariable(const char *name) {
#if defined(_WIN32)
    char *value = nullptr;
    std::size_t size = 0;
    if (_dupenv_s(&value, &size, name) != 0 || value == nullptr)
        return {};

    std::string result(value, size > 0 ? size - 1 : 0);
    std::free(value);
    return result;
#else
    if (const char *value = std::getenv(name))
        return value;
    return {};
#endif
}

/** @brief Serializes one build job to the persisted history JSON schema. */
nlohmann::json JobToJson(const BuildJob &job) {
    return nlohmann::json{
        {"os", GetBuildTargetOSLabel(job.os)},
        {"arch", GetBuildArchLabel(job.arch)},
        {"config", GetBuildConfigLabel(job.config)},
        {"status", GetBuildJobStatusLabel(job.status)},
        {"exitCode", job.exitCode},
        {"outputPath", job.outputPath},
        {"logPath", job.logPath},
        {"error", job.error},
        {"timestamp", job.timestamp},

    };
}

/** @brief Parses a target OS label from persisted history. */
BuildTargetOS ParseBuildTargetOS(std::string_view value) {
    using enum BuildTargetOS;
    if (value == "macOS")
        return MacOS;
    if (value == "Linux")
        return Linux;
    return Windows;
}

/** @brief Parses an architecture label from persisted history. */
BuildArch ParseBuildArch(std::string_view value) {
    return value == "arm64" ? BuildArch::Arm64 : BuildArch::x86_64;
}

/** @brief Parses a build config label from persisted history. */
BuildConfig ParseBuildConfig(std::string_view value) {
    using enum BuildConfig;
    if (value == "Debug")
        return Debug;
    if (value == "MinSizeRel")
        return MinSizeRel;
    return Release;
}

/** @brief Parses a build status label from persisted history. */
BuildJobStatus ParseBuildJobStatus(std::string_view value) {
    using enum BuildJobStatus;
    if (value == "Building")
        return Building;
    if (value == "Success")
        return Success;
    if (value == "Failed")
        return Failed;
    if (value == "Cancelled")
        return Cancelled;
    return Pending;
}

/** @brief Deserializes one build job from persisted history JSON. */
BuildJob JobFromJson(const nlohmann::json &json) {
    BuildJob job;
    job.os = ParseBuildTargetOS(json.value("os", "Windows"));
    job.arch = ParseBuildArch(json.value("arch", "x86_64"));
    job.config = ParseBuildConfig(json.value("config", "Release"));
    job.status = ParseBuildJobStatus(json.value("status", "Pending"));
    job.exitCode = json.value("exitCode", 0);
    job.outputPath = json.value("outputPath", std::string{});
    job.logPath = json.value("logPath", std::string{});
    job.error = json.value("error", std::string{});
    job.timestamp = json.value("timestamp", std::string{});

    return job;
}

} // namespace

/** @copydoc BuildHistoryPath */
std::filesystem::path BuildHistoryPath() {
    namespace fs = std::filesystem;
    fs::path base;
#if defined(_WIN32)
    base = ReadEnvironmentVariable("APPDATA");
#else
    base = ReadEnvironmentVariable("HOME");
#endif
    if (base.empty())
        base = fs::current_path();
    base /= ".horo";
    fs::create_directories(base);
    return base / "build_history.json";
}

/** @copydoc WriteHistoryJson */
void WriteHistoryJson(const std::filesystem::path &path,
                      const std::vector<BuildHistoryEntry> &entries) {
    nlohmann::json root = nlohmann::json::array();
    for (const auto &entry : entries) {
        nlohmann::json jobs = nlohmann::json::array();
        for (const BuildJob &job : entry.jobs)
            jobs.push_back(JobToJson(job));
        root.push_back({
            {"version", entry.versionTag},
            {"timestamp", entry.timestamp},
            {"allSucceeded", entry.allSucceeded},
            {"jobs", std::move(jobs)},
        });
    }

    std::ofstream out(path, std::ios::out | std::ios::trunc);
    if (out.good())
        out << root.dump(2) << '\n';
}

/** @copydoc ReadHistoryJson */
std::vector<BuildHistoryEntry> ReadHistoryJson(const std::filesystem::path &path) {
    std::ifstream in(path);
    if (!in.good())
        return {};

    try {
        const nlohmann::json root = nlohmann::json::parse(in);
        if (!root.is_array())
            return {};

        std::vector<BuildHistoryEntry> entries;
        entries.reserve(root.size());
        for (const nlohmann::json &entryJson : root) {
            BuildHistoryEntry entry;
            entry.versionTag = entryJson.value("version", std::string{});
            entry.timestamp = entryJson.value("timestamp", std::string{});
            entry.allSucceeded = entryJson.value("allSucceeded", false);
            if (const auto jobsIt = entryJson.find("jobs");
                jobsIt != entryJson.end() && jobsIt->is_array()) {
                entry.jobs.reserve(jobsIt->size());
                for (const nlohmann::json &jobJson : *jobsIt)
                    entry.jobs.push_back(JobFromJson(jobJson));
            }
            entries.push_back(std::move(entry));
        }
        return entries;
    } catch (const nlohmann::json::exception &) {
        return {};
    }
}

} // namespace Horo::Build
