/** @file horo_engine_main.cpp
 *  @brief Headless Horo Engine project and release command-line interface. */
#include "core/ArgRedactor.h"
#include "core/ProjectPath.h"
#include "core/BuildVersion.h"
#include "core/pipeline/ReleasePipeline.h"
#include "core/pipeline/ToolchainSettings.h"
#include "ui/launcher/LauncherProjectTemplate.h"

#include <cstdlib>
#include <filesystem>
#include <format>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#include <sys/wait.h>
#elif defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace {

namespace fs = std::filesystem;

struct CliOptions {
    std::vector<std::string_view> args;
};

/** @brief Temporarily sets an environment variable for a spawned command. */
class ScopedEnvironmentVariable {
public:
    ScopedEnvironmentVariable(std::string name, std::string value)
        : m_name(std::move(name)), m_oldValue(Read(m_name)) {
        Set(m_name, value);
    }

    ~ScopedEnvironmentVariable() {
        if (m_oldValue)
            Set(m_name, *m_oldValue);
        else
            Unset(m_name);
    }

    ScopedEnvironmentVariable(const ScopedEnvironmentVariable&) = delete;
    ScopedEnvironmentVariable& operator=(const ScopedEnvironmentVariable&) = delete;

private:
    static std::optional<std::string> Read(const std::string& name) {
#if defined(_WIN32)
        char* value = nullptr;
        std::size_t size = 0;
        if (_dupenv_s(&value, &size, name.c_str()) != 0 || value == nullptr)
            return std::nullopt;
        std::string result(value, size > 0 ? size - 1 : 0);
        std::free(value);
        return result;
#else
        const char* value = std::getenv(name.c_str());
        if (!value)
            return std::nullopt;
        return std::string(value);
#endif
    }

    static void Set(const std::string& name, const std::string& value) {
#if defined(_WIN32)
        _putenv_s(name.c_str(), value.c_str());
#else
        setenv(name.c_str(), value.c_str(), 1);
#endif
    }

    static void Unset(const std::string& name) {
#if defined(_WIN32)
        _putenv_s(name.c_str(), "");
#else
        unsetenv(name.c_str());
#endif
    }

    std::string m_name;
    std::optional<std::string> m_oldValue;
};

/** @brief Returns the current executable path when the platform exposes it. */
fs::path GetExecutablePath() {
#if defined(__APPLE__)
    std::vector<char> buffer(4096);
    uint32_t size = static_cast<uint32_t>(buffer.size());
    if (_NSGetExecutablePath(buffer.data(), &size) == 0) {
        std::error_code ec;
        fs::path path = fs::canonical(buffer.data(), ec);
        return ec ? fs::path(buffer.data()) : path;
    }
    return {};
#elif defined(_WIN32)
    std::vector<wchar_t> buffer(MAX_PATH);
    const DWORD len =
        GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    return len == 0 ? fs::path{} : fs::path(buffer.data());
#else
    std::string path(4096, '\0');
    const ssize_t len = readlink("/proc/self/exe", path.data(), path.size() - 1);
    if (len <= 0)
        return {};
    path.resize(static_cast<size_t>(len));
    return fs::path(path);
#endif
}

/** @brief Resolves the default SDK/prefix root from the CLI executable path. */
fs::path DefaultSdkRoot() {
    const fs::path exePath = GetExecutablePath();
    if (!exePath.empty() && exePath.has_parent_path()) {
        const fs::path binDir = exePath.parent_path();
        if (binDir.filename() == "bin" && binDir.has_parent_path())
            return binDir.parent_path();
        return binDir;
    }
    return fs::current_path();
}

/** @brief Quotes one argument for the host shell. */
std::string ShellQuote(const fs::path& value) {
    const std::string text = value.generic_string();
#if defined(_WIN32)
    std::string quoted = "\"";
    for (const char c : text) {
        if (c == '"')
            quoted += "\\\"";
        else
            quoted += c;
    }
    quoted += '"';
    return quoted;
#else
    std::string quoted = "'";
    for (const char c : text) {
        if (c == '\'')
            quoted += "'\\''";
        else
            quoted += c;
    }
    quoted += '\'';
    return quoted;
#endif
}

/** @brief Returns true if the current process can spawn the given shell command. */
int RunShellCommand(const std::string& command, bool dryRun) {
    std::cout << Horo::RedactCommandLine(command) << '\n';
    std::cout.flush();
    if (dryRun)
        return 0;
    const int status = std::system(command.c_str());
#if defined(_WIN32)
    return status;
#else
    if (status == -1)
        return 1;
    if (WIFEXITED(status))
        return WEXITSTATUS(status);
    if (WIFSIGNALED(status))
        return 128 + WTERMSIG(status);
    return 1;
#endif
}

/** @brief Returns the argument following a named option. */
std::optional<std::string_view> OptionValue(const CliOptions& options,
                                            std::string_view name) {
    const std::string prefix = std::string(name) + "=";
    for (size_t i = 0; i < options.args.size(); ++i) {
        const std::string_view arg = options.args[i];
        if (arg == name && i + 1 < options.args.size())
            return options.args[i + 1];
        if (arg.starts_with(prefix))
            return arg.substr(prefix.size());
    }
    return std::nullopt;
}

/** @brief Returns true when an option is present. */
bool HasOption(const CliOptions& options, std::string_view name) {
    for (const std::string_view arg : options.args) {
        if (arg == name)
            return true;
    }
    return false;
}

/** @brief Returns the first positional argument after options are ignored. */
std::optional<std::string_view> FirstPositional(const CliOptions& options) {
    for (size_t i = 0; i < options.args.size(); ++i) {
        const std::string_view arg = options.args[i];
        if (arg.starts_with("--")) {
            if (arg.find('=') == std::string_view::npos && i + 1 < options.args.size())
                ++i;
            continue;
        }
        return arg;
    }
    return std::nullopt;
}

/** @brief Returns --path when present, otherwise the first positional argument. */
std::optional<std::string_view> ProjectPathArgument(const CliOptions& options) {
    if (const auto path = OptionValue(options, "--path"))
        return path;
    return FirstPositional(options);
}

/** @brief Parses a build config token. */
Horo::Build::BuildConfig ParseBuildConfig(std::string_view value) {
    using enum Horo::Build::BuildConfig;
    if (value == "Debug" || value == "debug")
        return Debug;
    if (value == "MinSizeRel" || value == "minsizerel")
        return MinSizeRel;
    return Release;
}

/** @brief Returns the host build target. */
Horo::Build::BuildTargetOS HostTargetOS() {
#if defined(_WIN32)
    return Horo::Build::BuildTargetOS::Windows;
#elif defined(__APPLE__)
    return Horo::Build::BuildTargetOS::MacOS;
#else
    return Horo::Build::BuildTargetOS::Linux;
#endif
}

/** @brief Returns the host architecture label. */
Horo::Build::BuildArch HostArch() {
#if defined(__aarch64__) || defined(_M_ARM64)
    return Horo::Build::BuildArch::Arm64;
#else
    return Horo::Build::BuildArch::x86_64;
#endif
}

/** @brief Parses a target OS token, returning nullopt for unknown values. */
std::optional<Horo::Build::BuildTargetOS> ParseTargetOS(std::string_view value) {
    using enum Horo::Build::BuildTargetOS;
    if (value == "windows" || value == "Windows" || value == "win")
        return Windows;
    if (value == "macos" || value == "macOS" || value == "darwin" ||
        value == "MacOS")
        return MacOS;
    if (value == "linux" || value == "Linux")
        return Linux;
    return std::nullopt;
}

/** @brief Parses a target architecture token, returning nullopt for unknown values. */
std::optional<Horo::Build::BuildArch> ParseArch(std::string_view value) {
    using enum Horo::Build::BuildArch;
    if (value == "x86_64" || value == "amd64")
        return x86_64;
    if (value == "arm64" || value == "aarch64")
        return Arm64;
    return std::nullopt;
}

/** @brief Normalizes version tags for release output paths. */
std::string NormalizeVersionTag(std::string_view value) {
    if (value.empty())
        return Horo::Build::DefaultVersionTag();
    if (value.front() == 'v' || value.front() == 'V')
        return std::string(value);
    return "v" + std::string(value);
}

/** @brief Prints top-level usage. */
void PrintUsage() {
    std::cout
        << "horo-engine - headless Horo Engine CLI\n\n"
        << "Usage:\n"
        << "  horo-engine project create <path> --name <name> [--renderer opengl]\n"
        << "  horo-engine project build <path> [--config Debug|Release|MinSizeRel]\n"
        << "  horo-engine project release <path> --version <x.y.z> [--target windows|macos|linux]\n"
        << "                              [--arch x86_64|arm64] [--build-name <name>]\n"
        << "                              [--archive-password <pw>]\n"
        << "  horo-engine project open <path> [--editor-binary <path>]\n\n"
        << "Common options:\n"
        << "  --help, -h             Show this help message.\n"
        << "  --version, -V          Print version and exit.\n"
        << "  --sdk-root <path>       Engine SDK/prefix root. Defaults to the CLI prefix.\n"
        << "  --dry-run               Print the command without executing it.\n";
}

/** @brief Builds a CliOptions view from argv beginning at start. */
CliOptions MakeOptions(int argc, char** argv, int start) {
    CliOptions options;
    for (int i = start; i < argc; ++i) {
        if (argv[i])
            options.args.emplace_back(argv[i]);
    }
    return options;
}

/** @brief Implements `project create`. */
int ProjectCreate(const CliOptions& options) {
    const auto path = ProjectPathArgument(options);
    const auto name = OptionValue(options, "--name");
    if (!path || !name) {
        std::cerr << "project create requires <path> and --name.\n";
        return 2;
    }

    const fs::path sdkRoot =
        fs::path(std::string(OptionValue(options, "--sdk-root").value_or("")))
            .empty()
            ? DefaultSdkRoot()
            : fs::path(std::string(*OptionValue(options, "--sdk-root")));
    const std::string renderer =
        std::string(OptionValue(options, "--renderer").value_or("opengl"));

    Horo::Launcher::LauncherProjectDocument doc;
    std::string error;
    const Horo::Launcher::LauncherProjectTemplateRequest request{
        .projectRoot = fs::path(std::string(*path)),
        .projectName = std::string(*name),
        .sdkRoot = sdkRoot,
        .rendererBackend = renderer,
    };
    if (!Horo::Launcher::CreateLauncherProjectTemplate(request, &doc, &error)) {
        std::cerr << "Failed to create project: " << error << '\n';
        return 1;
    }

    std::cout << "Created project '" << doc.manifest.projectName << "' at "
              << fs::absolute(request.projectRoot).string() << '\n';
    return 0;
}

/** @brief Implements `project build`. */
int ProjectBuild(const CliOptions& options) {
    const auto path = ProjectPathArgument(options);
    if (!path) {
        std::cerr << "project build requires <path>.\n";
        return 2;
    }

    const fs::path sdkRoot =
        fs::path(std::string(OptionValue(options, "--sdk-root").value_or("")))
            .empty()
            ? DefaultSdkRoot()
            : fs::path(std::string(*OptionValue(options, "--sdk-root")));
    const Horo::Build::BuildConfig config =
        ParseBuildConfig(OptionValue(options, "--config").value_or("Debug"));
    const std::string command = Horo::Build::BuildProjectShellCommand(
        fs::path(std::string(*path)), sdkRoot, config);
    return RunShellCommand(command, HasOption(options, "--dry-run"));
}

/** @brief Implements `project release`. */
int ProjectRelease(const CliOptions& options) {
    const auto path = ProjectPathArgument(options);
    if (!path) {
        std::cerr << "project release requires <path>.\n";
        return 2;
    }

    const fs::path sdkRoot =
        fs::path(std::string(OptionValue(options, "--sdk-root").value_or("")))
            .empty()
            ? DefaultSdkRoot()
            : fs::path(std::string(*OptionValue(options, "--sdk-root")));
    Horo::ProjectPath::SetSdkRoot(sdkRoot);

    Horo::Build::BuildPipelineDraft draft;
    draft.versionTag =
        NormalizeVersionTag(OptionValue(options, "--version").value_or(
            Horo::Build::EngineVersion()));
    draft.buildName =
        std::string(OptionValue(options, "--build-name").value_or(
            fs::path(std::string(*path)).filename().string()));
    if (const auto outputRoot = OptionValue(options, "--output-root"))
        draft.outputRoot = std::string(*outputRoot);
    if (const auto password = OptionValue(options, "--archive-password"))
        draft.archivePassword = Horo::Core::SecureString(*password);

    Horo::Build::BuildTargetOS targetOS = HostTargetOS();
    if (const auto target = OptionValue(options, "--target")) {
        const auto parsed = ParseTargetOS(*target);
        if (!parsed) {
            std::cerr << "Unknown release target: " << *target << '\n';
            return 2;
        }
        targetOS = *parsed;
    }

    Horo::Build::BuildArch targetArch = HostArch();
    if (const auto arch = OptionValue(options, "--arch")) {
        const auto parsed = ParseArch(*arch);
        if (!parsed) {
            std::cerr << "Unknown release architecture: " << *arch << '\n';
            return 2;
        }
        targetArch = *parsed;
    }

    draft.jobs.push_back(Horo::Build::MakePendingJob(
        targetOS, targetArch,
        ParseBuildConfig(OptionValue(options, "--config").value_or("Release"))));

    Horo::Build::ToolchainSettingsStore emptyToolchains;
    Horo::Build::BuildCommandPlan plan;
    try {
        plan = Horo::Build::CreateBuildCommandPlan(
            draft, draft.jobs.front(), fs::path(std::string(*path)),
            &emptyToolchains);
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
    std::optional<ScopedEnvironmentVariable> archivePasswordEnv;
    if (!draft.archivePassword.Empty()) {
        archivePasswordEnv.emplace("HORO_RELEASE_ARCHIVE_PASSWORD",
                                   std::string(draft.archivePassword.View()));
    }
    return RunShellCommand(plan.args.size() >= 2 ? plan.args[1] : std::string{},
                           HasOption(options, "--dry-run"));
}

/** @brief Implements `project open`. */
int ProjectOpen(const CliOptions& options) {
    const auto path = ProjectPathArgument(options);
    if (!path) {
        std::cerr << "project open requires <path>.\n";
        return 2;
    }

    const fs::path sdkRoot =
        fs::path(std::string(OptionValue(options, "--sdk-root").value_or("")))
            .empty()
            ? DefaultSdkRoot()
            : fs::path(std::string(*OptionValue(options, "--sdk-root")));
    const fs::path editorBinary =
        fs::path(std::string(OptionValue(options, "--editor-binary").value_or("")))
            .empty()
            ? sdkRoot / "bin" / "HoroEditor"
            : fs::path(std::string(*OptionValue(options, "--editor-binary")));
    const std::string command =
        std::format("{} --editor --project {}", ShellQuote(editorBinary),
                    ShellQuote(fs::path(std::string(*path))));
    return RunShellCommand(command, HasOption(options, "--dry-run"));
}

/** @brief Implements `project ...` subcommands. */
int ProjectCommand(int argc, char** argv) {
    if (argc < 3 || std::string_view(argv[2]) == "--help") {
        PrintUsage();
        return argc < 3 ? 2 : 0;
    }

    const std::string_view subcommand(argv[2]);
    const CliOptions options = MakeOptions(argc, argv, 3);
    if (subcommand == "create")
        return ProjectCreate(options);
    if (subcommand == "build")
        return ProjectBuild(options);
    if (subcommand == "release")
        return ProjectRelease(options);
    if (subcommand == "open")
        return ProjectOpen(options);

    std::cerr << "Unknown project command: " << subcommand << '\n';
    return 2;
}

} // namespace

/** @brief Main entry point for the headless Horo Engine CLI. */
int main(int argc, char** argv) {
    if (argc < 2 || std::string_view(argv[1]) == "--help" ||
        std::string_view(argv[1]) == "-h") {
        PrintUsage();
        return argc < 2 ? 2 : 0;
    }

    if (std::string_view(argv[1]) == "--version" ||
        std::string_view(argv[1]) == "-V") {
        std::cout << Horo::Build::EngineVersion() << "\n";
        return 0;
    }

    const std::string_view command(argv[1]);
    if (command == "project")
        return ProjectCommand(argc, argv);

    std::cerr << "Unknown command: " << command << '\n';
    PrintUsage();
    return 2;
}
