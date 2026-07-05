/** @file ReleaseCommand.cpp
 *  @brief Build command construction: shell commands, process plans, output
 *         path resolution, and code-signing commands.
 *
 *  Extraction from ReleasePipeline.cpp — HORO-32 P7.1.
 */
#include "core/pipeline/ReleaseCommand.h"
#include "core/pipeline/ReleaseDraft.h"
#include "core/pipeline/ReleaseTypes.h"
#include "core/pipeline/ToolchainSettings.h"
#include "core/pipeline/TargetCapability.h"
#include "core/ArgRedactor.h"

#include <algorithm>
#include <cctype>
#include <format>
#include <stdexcept>
#include <string>

#include "core/ProjectPath.h"

namespace Horo::Build {
namespace {

/** @brief Canonicalizes a path without requiring the final path to exist. */
std::filesystem::path NormalizePathForLookup(const std::filesystem::path &rawPath) {
    if (rawPath.empty())
        return {};

    std::error_code ec;
    std::filesystem::path normalized = std::filesystem::weakly_canonical(rawPath, ec);
    if (ec)
        normalized = std::filesystem::absolute(rawPath, ec);
    if (ec)
        normalized = rawPath;
    return normalized.lexically_normal();
}

/** @brief Returns true when a directory exposes a build-tree HoroEngine CMake package. */
bool IsBuildTreeEnginePrefix(const std::filesystem::path &candidate) {
    if (candidate.empty())
        return false;

    std::error_code ec;
    const bool hasConfig = std::filesystem::is_regular_file(
        candidate / "HoroEngineConfig.cmake", ec) && !ec;
    ec.clear();
    const bool hasTargets = std::filesystem::is_regular_file(
        candidate / "HoroEngineTargets.cmake", ec) && !ec;
    return hasConfig && hasTargets;
}

/** @brief Returns true when a directory looks like an installed HoroEngine prefix. */
bool IsInstalledEnginePrefix(const std::filesystem::path &candidate) {
    if (candidate.empty())
        return false;

    std::error_code ec;
    return std::filesystem::is_regular_file(
        candidate / "lib" / "cmake" / "HoroEngine" / "HoroEngineConfig.cmake",
        ec) && !ec;
}

/** @brief Quotes one argument for the host shell. */
std::string QuoteShellArg(const std::filesystem::path &arg) {
    const std::string value = arg.generic_string();
#if defined(_WIN32)
    std::string quoted = "\"";
    for (const char c : value) {
        if (c == '"')
            quoted += "\\\"";
        else
            quoted += c;
    }
    quoted += '"';
    return quoted;
#else
    std::string quoted = "'";
    for (const char c : value) {
        if (c == '\'')
            quoted += "'\\''";
        else
            quoted += c;
    }
    quoted += '\'';
    return quoted;
#endif
}

/** @brief Converts build config enum to CMake configuration name. */
const char *GetCMakeBuildConfigName(BuildConfig config) {
    switch (config) {
    case BuildConfig::Debug:      return "Debug";
    case BuildConfig::Release:    return "Release";
    case BuildConfig::MinSizeRel: return "MinSizeRel";
    }
    return "Release";
}

/** @brief Builds a shell fragment that packs and removes copied raw assets. */
std::string BuildAssetArchiveShellCommand(const std::filesystem::path &sdkPrefix,
                                          const std::filesystem::path &assetsDir,
                                          const std::filesystem::path &archivePath) {
    const std::filesystem::path horopak = sdkPrefix / "bin" / "horopak";

#if defined(_WIN32)
    return std::format(
        "if exist {} ("
        "if defined HORO_RELEASE_ARCHIVE_PASSWORD ("
        "{} pack --project-root {} --output {} --compression 9 --password \"%HORO_RELEASE_ARCHIVE_PASSWORD%\""
        ") else ("
        "{} pack --project-root {} --output {} --compression 9"
        ") && cmake -E rm -rf {}"
        ")",
        QuoteShellArg(assetsDir),
        QuoteShellArg(horopak), QuoteShellArg(assetsDir), QuoteShellArg(archivePath),
        QuoteShellArg(horopak), QuoteShellArg(assetsDir), QuoteShellArg(archivePath),
        QuoteShellArg(assetsDir));
#else
    return std::format(
        "if [ -d {} ]; then "
        "if [ -n \"${{HORO_RELEASE_ARCHIVE_PASSWORD:-}}\" ]; then "
        "{} pack --project-root {} --output {} --compression 9 --password \"$HORO_RELEASE_ARCHIVE_PASSWORD\"; "
        "else "
        "{} pack --project-root {} --output {} --compression 9; "
        "fi && cmake -E rm -rf {}; "
        "fi",
        QuoteShellArg(assetsDir),
        QuoteShellArg(horopak), QuoteShellArg(assetsDir), QuoteShellArg(archivePath),
        QuoteShellArg(horopak), QuoteShellArg(assetsDir), QuoteShellArg(archivePath),
        QuoteShellArg(assetsDir));
#endif
}

/** @brief Builds a shell fragment that packs <output>/assets into <output>/assets.horo. */
std::string BuildOutputAssetArchiveShellCommand(const std::filesystem::path &sdkPrefix,
                                                const std::filesystem::path &outputPath) {
    return BuildAssetArchiveShellCommand(sdkPrefix, outputPath / "assets",
                                         outputPath / "assets.horo");
}

/** @brief Builds the Info.plist heredoc for a generated macOS game bundle. */
std::string BuildMacosInfoPlistShellCommand(const std::filesystem::path &plistPath,
                                            const std::string &appName) {
    return std::format(
        "cat > {} << 'HORO_INFO_PLIST_END'\n"
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
        "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
        "<plist version=\"1.0\">\n"
        "<dict>\n"
        "  <key>CFBundleExecutable</key>\n"
        "  <string>{}</string>\n"
        "  <key>CFBundleIdentifier</key>\n"
        "  <string>com.horoengine.{}</string>\n"
        "  <key>CFBundleName</key>\n"
        "  <string>{}</string>\n"
        "  <key>CFBundlePackageType</key>\n"
        "  <string>APPL</string>\n"
        "  <key>CFBundleShortVersionString</key>\n"
        "  <string>1.0</string>\n"
        "  <key>CFBundleVersion</key>\n"
        "  <string>1</string>\n"
        "</dict>\n"
        "</plist>\n"
        "HORO_INFO_PLIST_END\n",
        QuoteShellArg(plistPath), appName, appName, appName);
}

} // namespace

/** @copydoc ResolveBuildSdkPrefix */
std::filesystem::path ResolveBuildSdkPrefix() {
    const std::filesystem::path sdkRoot = NormalizePathForLookup(ProjectPath::SdkRoot());
    if (IsBuildTreeEnginePrefix(sdkRoot) || IsInstalledEnginePrefix(sdkRoot))
        return sdkRoot;

    if (sdkRoot.filename() == "sdk") {
        const std::filesystem::path parent = sdkRoot.parent_path();
        if (IsBuildTreeEnginePrefix(parent) || IsInstalledEnginePrefix(parent))
            return parent;
    }

    return sdkRoot;
}

/** @copydoc BuildProjectShellCommand */
std::string BuildProjectShellCommand(const std::filesystem::path &projectRoot,
                                     const std::filesystem::path &sdkPrefix,
                                     BuildConfig config,
                                     const ToolchainConfig* toolchain) {
    const std::filesystem::path buildDir = projectRoot / "build";
    const char *configName = GetCMakeBuildConfigName(config);

    std::string cmakeExe = "cmake";
    std::string extraArgs;

    if (toolchain) {
        if (!toolchain->cmakePath.empty()) {
            cmakeExe = QuoteShellArg(toolchain->cmakePath);
        }
        if (!toolchain->cmakeGenerator.empty()) {
            extraArgs += std::format(" -G {}", QuoteShellArg(toolchain->cmakeGenerator));
        }
        if (!toolchain->cmakeToolchainFilePath.empty()) {
            extraArgs += std::format(" -DCMAKE_TOOLCHAIN_FILE={}", QuoteShellArg(toolchain->cmakeToolchainFilePath));
        }
        if (!toolchain->sysrootPath.empty()) {
            extraArgs += std::format(" -DCMAKE_SYSROOT={}", QuoteShellArg(toolchain->sysrootPath));
        }
        if (!toolchain->compilerPath.empty()) {
            extraArgs += std::format(" -DCMAKE_C_COMPILER={} -DCMAKE_CXX_COMPILER={}",
                                     QuoteShellArg(toolchain->compilerPath),
                                     QuoteShellArg(toolchain->compilerPath));
        }
    }

    return std::format(
        "{} --fresh -S {} -B {} -DCMAKE_PREFIX_PATH={} -DCMAKE_BUILD_TYPE={}{} && "
        "{} --build {} --config {}",
        cmakeExe, QuoteShellArg(projectRoot), QuoteShellArg(buildDir),
        QuoteShellArg(sdkPrefix), configName, extraArgs,
        cmakeExe, QuoteShellArg(buildDir), configName);
}

/** @copydoc BuildPackageShellCommand */
std::string BuildPackageShellCommand(const std::filesystem::path &projectRoot,
                                     const std::filesystem::path &sdkPrefix,
                                     BuildConfig config,
                                     const std::filesystem::path &outputPath,
                                     const std::filesystem::path &archivePath,
                                     const ToolchainConfig* toolchain) {
    const std::filesystem::path buildDir = projectRoot / "build";
    const std::filesystem::path binDir = buildDir / "bin";
    const std::string buildCommand =
        BuildProjectShellCommand(projectRoot, sdkPrefix, config, toolchain);

    if (outputPath.empty())
        return buildCommand;

    std::string cmakeExe = toolchain && !toolchain->cmakePath.empty() ? QuoteShellArg(toolchain->cmakePath) : "cmake";

    // We only use this for Windows now.
    return std::format(
        "{} && {} -E rm -rf {} && {} -E make_directory {} && "
        "{} -E copy_directory {} {} && {}",
        buildCommand, cmakeExe, QuoteShellArg(outputPath), cmakeExe, QuoteShellArg(outputPath),
        cmakeExe, QuoteShellArg(binDir), QuoteShellArg(outputPath),
        BuildOutputAssetArchiveShellCommand(sdkPrefix, archivePath.parent_path()));
}

/** @copydoc CreateBuildCommandPlan */
BuildCommandPlan CreateBuildCommandPlan(const std::filesystem::path &projectRoot,
                                        BuildConfig config,
                                        const ToolchainConfig* toolchain) {
    BuildCommandPlan plan;
#if defined(_WIN32)
    plan.executable = "cmd";
#else
    plan.executable = "/bin/sh";
#endif
    plan.workingDirectory = projectRoot.empty() ? std::filesystem::current_path()
                                                : projectRoot;
    const std::string shellCmd =
        BuildProjectShellCommand(plan.workingDirectory, ResolveBuildSdkPrefix(), config, toolchain);
#if defined(_WIN32)
    plan.args = {"/C", shellCmd};
#else
    plan.args = {"-c", shellCmd};
#endif
    plan.debugString = RedactForDisplay(shellCmd);
    return plan;
}

/** @copydoc CreateBuildCommandPlan */
BuildCommandPlan CreateBuildCommandPlan(const BuildPipelineDraft &draft,
                                        const BuildJob &job,
                                        const std::filesystem::path &projectRoot,
                                        const ToolchainSettingsStore* store) {
    const ToolchainConfig* tc = nullptr;
    if (store) {
        TargetCapability cap = EvaluateTargetCapability(job.os, job.arch, *store);
        if (!cap.IsEnabled()) {
            throw std::runtime_error(FormatTargetCapabilityBlockReason(cap));
        }
        if (cap.state == TargetCapabilityState::ReadyCrossCompile) {
            tc = store->FindValidEnabledToolchain(ReleaseTargetTriple{job.os, job.arch});
        }
    }

    BuildCommandPlan plan = CreateBuildCommandPlan(projectRoot, job.config, tc);

    std::string shellCmd;
    const std::string outputPath = ResolveJobOutputPath(draft, job, plan.workingDirectory);
    const std::filesystem::path buildDir = projectRoot / "build";
    const std::string appName = draft.buildName.empty() ? std::string("HoroEngine") : draft.buildName;
    const std::string buildCmd = BuildProjectShellCommand(plan.workingDirectory, ResolveBuildSdkPrefix(), job.config, tc);

    if (job.os == BuildTargetOS::Linux) {
        shellCmd =
            BuildPackageShellCommand(plan.workingDirectory, ResolveBuildSdkPrefix(),
                                     job.config, outputPath,
                                     ResolveJobArchivePath(draft, job, plan.workingDirectory), tc);
    } else if (job.os == BuildTargetOS::MacOS) {
        const std::filesystem::path appBundle =
            ResolveJobAppBundlePath(draft, job, plan.workingDirectory);
        const std::filesystem::path appResources =
            appBundle / "Contents" / "Resources";
        const std::filesystem::path appMacOS =
            appBundle / "Contents" / "MacOS";
        const std::filesystem::path plistPath = appBundle / "Contents" / "Info.plist";
        const std::filesystem::path binaryPath = buildDir / "bin" / appName;
        shellCmd = std::format(
            "{} && cmake -E rm -rf {} && cmake -E make_directory {} {} && "
            "cmake -E copy_if_different {} {} && {}\n"
            "cmake -E copy_directory {} {} && cmake -E copy_directory {} {} && "
            "cmake -E copy_directory {} {} && {}",
            buildCmd,
            QuoteShellArg(appBundle),
            QuoteShellArg(appMacOS),
            QuoteShellArg(appResources),
            QuoteShellArg(binaryPath),
            QuoteShellArg(appMacOS / appName),
            BuildMacosInfoPlistShellCommand(plistPath, appName),
            QuoteShellArg(buildDir / "bin" / "assets"),
            QuoteShellArg(appResources / "assets"),
            QuoteShellArg(buildDir / "bin" / "shaders"),
            QuoteShellArg(appMacOS / "shaders"),
            QuoteShellArg(buildDir / "bin" / "shaders"),
            QuoteShellArg(appResources / "shaders"),
            BuildAssetArchiveShellCommand(ResolveBuildSdkPrefix(),
                                          appResources / "assets",
                                          appResources / "assets.horo"));
    } else {
        shellCmd =
            BuildPackageShellCommand(plan.workingDirectory, ResolveBuildSdkPrefix(),
                                     job.config, outputPath,
                                     ResolveJobArchivePath(draft, job, plan.workingDirectory), tc);
    }

#if defined(_WIN32)
    plan.args = {"/C", shellCmd};
#else
    plan.args = {"-c", shellCmd};
#endif
    plan.debugString = RedactForDisplay(shellCmd);
    return plan;
}

/** @copydoc BuildCommandForJob */
std::string BuildCommandForJob(const BuildPipelineDraft &draft,
                               const BuildJob &job,
                               const std::filesystem::path &projectRoot,
                               const ToolchainSettingsStore* store) {
    try {
        return CreateBuildCommandPlan(draft, job, projectRoot, store).debugString;
    } catch (const std::exception& e) {
        return std::format("[Error] Cannot generate build command: {}", e.what());
    }
}

/** @copydoc SignCommandForJob */
std::string SignCommandForJob(const BuildPipelineDraft &draft, const BuildJob &job) {
    if (!draft.signing.enabled)
        return {};

    if (job.os == BuildTargetOS::MacOS && draft.signing.notarize) {
        return std::format(
            R"(codesign --deep --force --verify --verbose --sign "Developer ID Application: {}" "{}")",
            draft.signing.teamId, job.outputPath);
    }
    if (job.os == BuildTargetOS::Windows) {
        return std::format(
            R"(signtool sign /fd SHA256 /f "{}" /p "{}" "{}")",
            draft.signing.certificatePath,
            draft.signing.certificatePassword.Data(), job.outputPath);
    }
    return {};
}

/** @copydoc ResolveJobOutputPath */
std::string ResolveJobOutputPath(const BuildPipelineDraft &draft,
                                 const BuildJob &job,
                                 const std::filesystem::path &projectRoot) {
    const std::string osLabel = GetBuildTargetOSLabel(job.os);
    std::string osLower = osLabel;
    std::ranges::transform(osLower, osLower.begin(),
                           [](char c) { return static_cast<char>(std::tolower(c)); });
    const std::string outputRoot =
        draft.outputRoot.empty() ? ResolveDefaultOutputRoot(projectRoot) : draft.outputRoot;
    const std::string versionTag =
        draft.versionTag.empty() ? DefaultVersionTag() : draft.versionTag;
    return std::format("{}/{}_{}_{}",
                       outputRoot, versionTag, osLower,
                       GetBuildArchLabel(job.arch));
}

/** @copydoc ResolveJobOutputPath */
std::string ResolveJobOutputPath(const BuildPipelineDraft &draft, const BuildJob &job) {
    return ResolveJobOutputPath(draft, job, std::filesystem::current_path());
}

/** @copydoc ResolveJobAppBundlePath */
std::string ResolveJobAppBundlePath(const BuildPipelineDraft &draft,
                                    const BuildJob &job,
                                    const std::filesystem::path &projectRoot) {
    if (job.os != BuildTargetOS::MacOS)
        return {};
    const std::string outputPath = ResolveJobOutputPath(draft, job, projectRoot);
    const std::string appName = draft.buildName.empty() ? std::string("HoroEngine") : draft.buildName;
    return std::format("{}/{}.app", outputPath, appName);
}

/** @copydoc ResolveJobManifestPath */
std::string ResolveJobManifestPath(const BuildPipelineDraft &draft,
                                   const BuildJob &job,
                                   const std::filesystem::path &projectRoot) {
    if (job.os == BuildTargetOS::MacOS) {
        return std::format("{}/Contents/Resources/.manifest.json",
                           ResolveJobAppBundlePath(draft, job, projectRoot));
    }
    return std::format("{}/.manifest.json", ResolveJobOutputPath(draft, job, projectRoot));
}

/** @copydoc ResolveJobArchivePath */
std::string ResolveJobArchivePath(const BuildPipelineDraft &draft,
                                  const BuildJob &job,
                                  const std::filesystem::path &projectRoot) {
    if (job.os == BuildTargetOS::MacOS) {
        return std::format("{}/Contents/Resources/assets.horo",
                           ResolveJobAppBundlePath(draft, job, projectRoot));
    }
    return std::format("{}/assets.horo", ResolveJobOutputPath(draft, job, projectRoot));
}

} // namespace Horo::Build
