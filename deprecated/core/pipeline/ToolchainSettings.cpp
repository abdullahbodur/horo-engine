/**
 * @copydoc ToolchainSettings.h
 */

#include "core/pipeline/ToolchainSettings.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <string>

#include "core/Logger.h"
#include "core/ProjectPath.h"
#include "mcp/McpSettings.h"
#include <nlohmann/json.hpp>

namespace Horo::Build {

namespace {

const char* CurrentValidationHostOS() {
#ifdef _WIN32
    return "Windows";
#elif defined(__APPLE__)
    return "macOS";
#else
    return "Linux";
#endif
}

void MarkPartiallyValid(ToolchainValidationResult& result) {
    if (result.status == ToolchainValidationResult::Status::Valid)
        result.status = ToolchainValidationResult::Status::PartiallyValid;
}

void MarkInvalid(ToolchainValidationResult& result) {
    result.status = ToolchainValidationResult::Status::Invalid;
}

bool IsOwnerExecutable(const std::filesystem::path& path) {
    const auto status = std::filesystem::status(path);
    return (status.permissions() & std::filesystem::perms::owner_exec) !=
           std::filesystem::perms::none;
}

ToolchainCheckResult RequiredPathCheck(std::string checkName,
                                       std::string foundPrefix,
                                       std::string missingPrefix,
                                       const std::string& path,
                                       ToolchainValidationResult& result) {
    ToolchainCheckResult check;
    check.checkName = std::move(checkName);
    if (std::filesystem::exists(path)) {
        check.severity = ToolchainCheckResult::Severity::Pass;
        check.message = std::move(foundPrefix) + path;
        return check;
    }

    check.severity = ToolchainCheckResult::Severity::Error;
    check.message = std::move(missingPrefix) + path;
    MarkInvalid(result);
    return check;
}

ToolchainCheckResult RequiredExecutableCheck(std::string checkName,
                                             std::string foundPrefix,
                                             std::string missingPrefix,
                                             std::string notExecutablePrefix,
                                             const std::string& path,
                                             ToolchainValidationResult& result) {
    ToolchainCheckResult check;
    check.checkName = std::move(checkName);
    if (!std::filesystem::exists(path)) {
        check.severity = ToolchainCheckResult::Severity::Error;
        check.message = std::move(missingPrefix) + path;
        MarkInvalid(result);
        return check;
    }
    if (!IsOwnerExecutable(path)) {
        check.severity = ToolchainCheckResult::Severity::Error;
        check.message = std::move(notExecutablePrefix) + path;
        MarkInvalid(result);
        return check;
    }

    check.severity = ToolchainCheckResult::Severity::Pass;
    check.message = std::move(foundPrefix) + path;
    return check;
}

ToolchainCheckResult ValidateSysroot(const ToolchainConfig& config,
                                     ToolchainValidationResult& result) {
    ToolchainCheckResult check;
    check.checkName = "sysroot_valid";
    if (!std::filesystem::exists(config.sysrootPath)) {
        check.severity = ToolchainCheckResult::Severity::Error;
        check.message = "Sysroot not found: " + config.sysrootPath;
        MarkInvalid(result);
        return check;
    }

    const auto sysrootPath = std::filesystem::path(config.sysrootPath);
    const bool hasInclude = std::filesystem::exists(sysrootPath / "include");
    if (const bool hasLib = std::filesystem::exists(sysrootPath / "lib");
        hasInclude && hasLib) {
        check.severity = ToolchainCheckResult::Severity::Pass;
        check.message = "Sysroot found with include/ and lib/ at " + config.sysrootPath;
    } else {
        check.severity = ToolchainCheckResult::Severity::Warning;
        check.message = "Sysroot exists but missing expected subdirs: " + config.sysrootPath;
        MarkPartiallyValid(result);
    }
    return check;
}

ToolchainCheckResult ValidateCMakeGenerator(const ToolchainConfig& config,
                                            ToolchainValidationResult& result) {
    ToolchainCheckResult check;
    check.checkName = "cmake_generate";
    if (config.cmakeGenerator.empty()) {
        check.severity = ToolchainCheckResult::Severity::Error;
        check.message = "CMake generator is not configured";
        MarkInvalid(result);
        return check;
    }
    if (!config.cmakePath.empty() && !std::filesystem::exists(config.cmakePath)) {
        check.severity = ToolchainCheckResult::Severity::Error;
        check.message = "Custom CMake executable not found: " + config.cmakePath;
        MarkInvalid(result);
        return check;
    }

    check.severity = ToolchainCheckResult::Severity::Pass;
    check.message = "CMake generator configured: " + config.cmakeGenerator;
    return check;
}

void AddOptionalPathCheck(std::vector<ToolchainCheckResult>& checks,
                          ToolchainValidationResult& result,
                          std::string checkName,
                          std::string passMessage,
                          std::string warningMessage,
                          const std::filesystem::path& path,
                          bool requireDirectory) {
    if (path.empty())
        return;

    ToolchainCheckResult check;
    check.checkName = std::move(checkName);
    if (const bool exists =
            requireDirectory ? std::filesystem::is_directory(path)
                             : std::filesystem::exists(path);
        exists) {
        check.severity = ToolchainCheckResult::Severity::Pass;
        check.message = std::move(passMessage) + path.string();
    } else {
        check.severity = ToolchainCheckResult::Severity::Warning;
        check.message = std::move(warningMessage) + path.string();
        MarkPartiallyValid(result);
    }
    checks.push_back(std::move(check));
}

std::string BuildValidationStatusReason(const std::vector<ToolchainCheckResult>& checks) {
    std::vector<std::string> failureReasons;
    for (const auto& check : checks) {
        if (check.severity == ToolchainCheckResult::Severity::Error)
            failureReasons.push_back(check.message);
    }
    if (failureReasons.empty())
        return "All checks passed";

    std::string reason = "Errors: ";
    for (size_t i = 0; i < failureReasons.size(); ++i) {
        if (i > 0)
            reason += "; ";
        reason += failureReasons[i];
    }
    return reason;
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// ReleaseTargetTriple implementation
// ─────────────────────────────────────────────────────────────────────────────

bool ReleaseTargetTriple::operator<(const ReleaseTargetTriple& other) const {
    if (os != other.os)
        return static_cast<int>(os) < static_cast<int>(other.os);
    return static_cast<int>(arch) < static_cast<int>(other.arch);
}

std::string ReleaseTargetTriple::ToString() const {
    using enum BuildTargetOS;
    std::string osStr;
    switch (os) {
        case Windows:
            osStr = "windows";
            break;
        case Linux:
            osStr = "linux";
            break;
        case MacOS:
            osStr = "macos";
            break;
    }

    std::string archStr;
    switch (arch) {
        case BuildArch::x86_64:
            archStr = "x86_64";
            break;
        case BuildArch::Arm64:
            archStr = "arm64";
            break;
    }

    return osStr + "-" + archStr;
}

bool ReleaseTargetTriple::TryParse(std::string_view str,
                                   ReleaseTargetTriple& outTriple) {
    const auto dashPos = str.find('-');
    if (dashPos == std::string::npos)
        return false;

    const std::string_view osStr = str.substr(0, dashPos);
    const std::string_view archStr = str.substr(dashPos + 1);

    BuildTargetOS os;
    if (osStr == "windows")
        os = BuildTargetOS::Windows;
    else if (osStr == "linux")
        os = BuildTargetOS::Linux;
    else if (osStr == "macos")
        os = BuildTargetOS::MacOS;
    else
        return false;

    BuildArch arch;
    if (archStr == "x86_64")
        arch = BuildArch::x86_64;
    else if (archStr == "arm64")
        arch = BuildArch::Arm64;
    else
        return false;

    outTriple = {os, arch};
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// ToolchainConfig implementation
// ─────────────────────────────────────────────────────────────────────────────

bool ToolchainConfig::IsConfigured() const {
    return !cmakeToolchainFilePath.empty() && !compilerPath.empty() &&
           !linkerPath.empty() && !sysrootPath.empty() && !cmakeGenerator.empty();
}

bool ToolchainConfig::IsValid() const {
    return lastValidationResult.status == ToolchainValidationResult::Status::Valid;
}

// ─────────────────────────────────────────────────────────────────────────────
// ToolchainSettingsStore implementation
// ─────────────────────────────────────────────────────────────────────────────

ToolchainSettingsStore::ToolchainSettingsStore() = default;

void ToolchainSettingsStore::AddToolchain(const ToolchainConfig& config) {
    m_toolchains[config.targetTriple][config.name] = config;
}

void ToolchainSettingsStore::RemoveToolchain(const ReleaseTargetTriple& targetTriple,
                                              const std::string& name) {
    auto targetIt = m_toolchains.find(targetTriple);
    if (targetIt != m_toolchains.end()) {
        targetIt->second.erase(name);
        if (targetIt->second.empty())
            m_toolchains.erase(targetIt);
    }
}

std::vector<ToolchainConfig> ToolchainSettingsStore::GetToolchainsForTarget(
    const ReleaseTargetTriple& target) const {
    std::vector<ToolchainConfig> result;
    if (const auto it = m_toolchains.find(target); it != m_toolchains.end()) {
        for (const auto& [name, config] : it->second) {
            result.push_back(config);
        }
    }
    return result;
}

std::vector<ToolchainConfig> ToolchainSettingsStore::GetAllToolchains() const {
    std::vector<ToolchainConfig> result;
    for (const auto& [target, namedToolchains] : m_toolchains) {
        for (const auto& [name, config] : namedToolchains) {
            result.push_back(config);
        }
    }
    return result;
}

ToolchainConfig* ToolchainSettingsStore::FindToolchain(const ReleaseTargetTriple& target,
                                                        const std::string& name) {
    const auto targetIt = m_toolchains.find(target);
    if (targetIt == m_toolchains.end())
        return nullptr;
    const auto it = targetIt->second.find(name);
    if (it == targetIt->second.end())
        return nullptr;
    return &it->second;
}

const ToolchainConfig* ToolchainSettingsStore::FindToolchain(const ReleaseTargetTriple& target,
                                                              const std::string& name) const {
    const auto targetIt = m_toolchains.find(target);
    if (targetIt == m_toolchains.end())
        return nullptr;
    const auto it = targetIt->second.find(name);
    if (it == targetIt->second.end())
        return nullptr;
    return &it->second;
}

void ToolchainSettingsStore::ValidateToolchain(ToolchainConfig& config) {
    RunValidationChecks(config);
}

std::vector<ToolchainValidationResult> ToolchainSettingsStore::ValidateAll() {
    std::vector<ToolchainValidationResult> results;
    for (auto& [target, namedToolchains] : m_toolchains) {
        for (auto& [name, config] : namedToolchains) {
            RunValidationChecks(config);
            results.push_back(config.lastValidationResult);
        }
    }
    return results;
}

void ToolchainSettingsStore::InvalidateAll() {
    for (auto& [target, namedToolchains] : m_toolchains) {
        for (auto& [name, config] : namedToolchains) {
            config.lastValidationTime = 0;
            config.lastValidationResult = ToolchainValidationResult();
        }
    }
}

std::filesystem::path ToolchainSettingsStore::GetDefaultSettingsPath() {
    return Mcp::ResolveMcpSettingsDirectory() / "toolchains.json";
}

using json = nlohmann::json;

void to_json(json& j, const ToolchainConfig& config) {
    j = json{
        {"name", config.name},
        {"targetTriple", config.targetTriple.ToString()},
        {"cmakeToolchainFilePath", config.cmakeToolchainFilePath},
        {"cmakeGenerator", config.cmakeGenerator},
        {"compilerPath", config.compilerPath},
        {"linkerPath", config.linkerPath},
        {"sysrootPath", config.sysrootPath},
        {"cmakePath", config.cmakePath},
        {"enabled", config.enabled},
        {"packagingScriptPath", config.packagingScriptPath},
        {"runtimeDependenciesPath", config.runtimeDependenciesPath}
    };
}

void from_json(const json& j, ToolchainConfig& config) {
    if (j.contains("name")) j.at("name").get_to(config.name);

    if (j.contains("targetTriple")) {
        std::string tripleStr = j.at("targetTriple").get<std::string>();
        ReleaseTargetTriple::TryParse(tripleStr, config.targetTriple);
    }

    if (j.contains("cmakeToolchainFilePath")) j.at("cmakeToolchainFilePath").get_to(config.cmakeToolchainFilePath);
    if (j.contains("cmakeGenerator")) j.at("cmakeGenerator").get_to(config.cmakeGenerator);
    if (j.contains("compilerPath")) j.at("compilerPath").get_to(config.compilerPath);
    if (j.contains("linkerPath")) j.at("linkerPath").get_to(config.linkerPath);
    if (j.contains("sysrootPath")) j.at("sysrootPath").get_to(config.sysrootPath);
    if (j.contains("cmakePath")) j.at("cmakePath").get_to(config.cmakePath);
    if (j.contains("enabled")) j.at("enabled").get_to(config.enabled);
    if (j.contains("packagingScriptPath")) j.at("packagingScriptPath").get_to(config.packagingScriptPath);
    if (j.contains("runtimeDependenciesPath")) j.at("runtimeDependenciesPath").get_to(config.runtimeDependenciesPath);
}

bool ToolchainSettingsStore::SaveToFile(const std::filesystem::path& settingsPath) const {
    json root = json::array();
    for (const auto& config : GetAllToolchains()) {
        root.push_back(config);
    }

    std::error_code ec;
    std::filesystem::create_directories(settingsPath.parent_path(), ec);
    if (ec) {
        LogError("Failed to create toolchains directory: {}", ec.message());
        return false;
    }

    std::ofstream out(settingsPath);
    if (!out.is_open()) {
        LogError("Failed to open toolchains file for writing: {}", settingsPath.string());
        return false;
    }
    out << root.dump(4);
    return true;
}

bool ToolchainSettingsStore::LoadFromFile(const std::filesystem::path& settingsPath) {
    if (!std::filesystem::exists(settingsPath)) {
        return true;
    }

    std::ifstream in(settingsPath);
    if (!in.is_open()) {
        LogError("Failed to open toolchains file for reading: {}", settingsPath.string());
        return false;
    }

    try {
        json root;
        in >> root;
        Clear();
        if (root.is_array()) {
            for (const auto& j : root) {
                ToolchainConfig config = j.get<ToolchainConfig>();
                AddToolchain(config);
            }
        }
        return true;
    } catch (const json::exception& e) {
        LogError("Failed to parse toolchains file: {}", e.what());
        return false;
    }
}

std::size_t ToolchainSettingsStore::GetEnabledToolchainCountForTarget(
    const ReleaseTargetTriple& target) const {
    const auto toolchains = GetToolchainsForTarget(target);
    return static_cast<std::size_t>(
        std::ranges::count_if(toolchains,
                              [](const ToolchainConfig& cfg) {
                                  return cfg.enabled;
                              }));
}

bool ToolchainSettingsStore::HasAnyValidToolchainForTarget(const ReleaseTargetTriple& target) const {
    const auto toolchains = GetToolchainsForTarget(target);
    return std::ranges::any_of(toolchains, [](const ToolchainConfig& cfg) {
        return cfg.enabled && cfg.IsValid();
    });
}

ToolchainConfig* ToolchainSettingsStore::FindValidEnabledToolchain(
    const ReleaseTargetTriple& target) {
    const auto toolchains = GetToolchainsForTarget(target);
    for (auto& toolchain : toolchains) {
        if (toolchain.enabled && toolchain.IsValid()) {
            return FindToolchain(target, toolchain.name);
        }
    }
    return nullptr;
}

const ToolchainConfig* ToolchainSettingsStore::FindValidEnabledToolchain(
    const ReleaseTargetTriple& target) const {
    const auto toolchains = GetToolchainsForTarget(target);
    for (const auto& toolchain : toolchains) {
        if (toolchain.enabled && toolchain.IsValid()) {
            return FindToolchain(target, toolchain.name);
        }
    }
    return nullptr;
}

void ToolchainSettingsStore::Clear() {
    m_toolchains.clear();
}

bool ToolchainSettingsStore::IsEmpty() const {
    return m_toolchains.empty();
}

// ─────────────────────────────────────────────────────────────────────────────
// Validation checks
// ─────────────────────────────────────────────────────────────────────────────

void ToolchainSettingsStore::RunValidationChecks(ToolchainConfig& config) const {
    ToolchainValidationResult result;
    result.status = ToolchainValidationResult::Status::Valid;
    result.hostOSWhenValidated = CurrentValidationHostOS();
    result.checks.clear();

    if (!config.IsConfigured()) {
        result.status = ToolchainValidationResult::Status::NotValidated;
        result.statusReason = "Toolchain not configured (missing paths)";
        config.lastValidationResult = result;
        return;
    }

    result.checks.push_back(RequiredPathCheck(
        "cmake_toolchain_exists",
        "CMake toolchain file found at ",
        "CMake toolchain file not found: ",
        config.cmakeToolchainFilePath,
        result));
    result.checks.push_back(RequiredExecutableCheck(
        "compiler_exists",
        "Compiler found at ",
        "Compiler not found: ",
        "Compiler not executable: ",
        config.compilerPath,
        result));
    result.checks.push_back(RequiredExecutableCheck(
        "linker_exists",
        "Linker found at ",
        "Linker not found: ",
        "Linker not executable: ",
        config.linkerPath,
        result));
    result.checks.push_back(ValidateSysroot(config, result));
    result.checks.push_back(ValidateCMakeGenerator(config, result));

    AddOptionalPathCheck(result.checks, result,
                         "packaging_script",
                         "Packaging script found at ",
                         "Packaging script not found: ",
                         config.packagingScriptPath,
                         false);
    AddOptionalPathCheck(result.checks, result,
                         "runtime_dependencies",
                         "Runtime dependencies dir found at ",
                         "Runtime dependencies dir not found or not a directory: ",
                         config.runtimeDependenciesPath,
                         true);

    result.statusReason = BuildValidationStatusReason(result.checks);
    config.lastValidationTime = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
    config.lastValidationResult = result;
}

} // namespace Horo::Build
