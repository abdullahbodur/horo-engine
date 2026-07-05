/**
 * @file ToolchainSettings.h
 * @brief Toolchain configuration and validation for cross-platform release builds.
 *
 * Provides a host-agnostic model for configuring and validating cross-compilation
 * toolchains. Users can configure named toolchains for any target (Windows, Linux, macOS)
 * from any host OS, and the validation contract proves toolchain readiness.
 */

#ifndef HORO_TOOLCHAIN_SETTINGS_H
#define HORO_TOOLCHAIN_SETTINGS_H

#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "core/pipeline/ReleaseTypes.h"

namespace Horo::Build {

/** @brief Release target platform triple: OS x architecture. */
struct ReleaseTargetTriple {
    BuildTargetOS os;      ///< Windows, Linux, macOS
    BuildArch arch;        ///< x86_64, arm64

    /// @brief Stable comparison for key-value lookups.
    bool operator==(const ReleaseTargetTriple& other) const = default;
    bool operator<(const ReleaseTargetTriple& other) const;

    /// @brief User-friendly string representation (e.g., "linux-x86_64").
    std::string ToString() const;

    /// @brief Parse from string (e.g., "linux-x86_64" -> ReleaseTargetTriple).
    /// Returns true if successfully parsed, false if format is invalid.
    static bool TryParse(std::string_view str, ReleaseTargetTriple& outTriple);
};

/** @brief Structured validation result for a single check within toolchain validation. */
struct ToolchainCheckResult {
    enum class Severity : uint8_t {
        Unknown = 0,
        Pass = 1,
        Warning = 2,
        Error = 3,
    };

    std::string checkName;     ///< e.g., "compiler_exists", "sysroot_valid", "cmake_works"
    Severity severity = Severity::Unknown;
    std::string message;       ///< e.g., "g++ found at /usr/bin/g++" or "sysroot /path missing"
};

/** @brief Overall validation state and result for a single toolchain. */
struct ToolchainValidationResult {
    enum class Status : uint8_t {
        NotValidated = 0,   ///< Never run validation
        Valid = 1,          ///< All checks passed
        PartiallyValid = 2, ///< Some checks passed, some warnings
        Invalid = 3,        ///< Checks failed; toolchain cannot be used
    };

    Status status = Status::NotValidated;
    std::string statusReason; ///< Human-readable summary

    std::vector<ToolchainCheckResult> checks; ///< Detailed per-check results

    /// @brief Metadata for audit trail.
    /// @{
    std::string hostOSWhenValidated;         ///< "macOS" / "Linux" / "Windows"
    std::string editorVersionWhenValidated;  ///< For future re-validation hints
    /// @}
};

/** @brief Named toolchain configuration for a single release target. */
struct ToolchainConfig {
    std::string name;                       ///< User-friendly name ("My Linux GCC", "MinGW-w64")
    ReleaseTargetTriple targetTriple;       ///< What OS/arch this toolchain targets

    /// @brief Toolchain file paths — user configures these.
    /// @{
    std::string cmakeToolchainFilePath;     ///< Path to CMake toolchain file
    std::string cmakeGenerator;             ///< CMake generator name (e.g. "Ninja")
    std::string compilerPath;               ///< Path to C++ compiler
    std::string linkerPath;                 ///< Path to linker
    std::string sysrootPath;                ///< Path to sysroot/SDK
    std::string cmakePath;                  ///< Path to cmake executable (default: system cmake)
    /// @}

    /// @brief Validation state (computed at startup, after save, or on-demand).
    /// @{
    bool enabled = true;                              ///< User toggle: this toolchain is available
    uint64_t lastValidationTime = 0;                  ///< Timestamp of last validation (0 = never)
    ToolchainValidationResult lastValidationResult;   ///< Latest validation state
    /// @}

    /// @brief Reserved for future packaging/runtime paths.
    /// @{
    std::string packagingScriptPath;        ///< Post-build packaging script (reserved)
    std::string runtimeDependenciesPath;    ///< Directory of runtime .so/.dll files (reserved)
    /// @}

    /// @brief Check if this toolchain has any configured paths.
    bool IsConfigured() const;

    /// @brief Check if this toolchain passed the most recent validation.
    bool IsValid() const;
};

/** @brief Collection of all configured toolchains with validation and persistence. */
class ToolchainSettingsStore {
public:
    ToolchainSettingsStore();
    ~ToolchainSettingsStore() = default;

    // Register/unregister toolchains
    /// @brief Add a new toolchain configuration.
    void AddToolchain(const ToolchainConfig& config);

    /// @brief Remove a toolchain by target and name.
    void RemoveToolchain(const ReleaseTargetTriple& targetTriple, const std::string& name);

    // Query toolchains
    /// @brief Get all toolchains configured for a specific target.
    std::vector<ToolchainConfig> GetToolchainsForTarget(const ReleaseTargetTriple& target) const;

    /// @brief Get all configured toolchains.
    std::vector<ToolchainConfig> GetAllToolchains() const;

    /// @brief Find a specific toolchain by target and name. Returns nullptr if not found.
    ToolchainConfig* FindToolchain(const ReleaseTargetTriple& target, const std::string& name);
    const ToolchainConfig* FindToolchain(const ReleaseTargetTriple& target, const std::string& name) const;

    // Validation
    /// @brief Run validation checks on a single toolchain.
    /// Updates toolchain.lastValidationResult in-place.
    void ValidateToolchain(ToolchainConfig& config);

    /// @brief Validate all configured toolchains.
    std::vector<ToolchainValidationResult> ValidateAll();

    /// @brief Force re-validation of all toolchains even if recently validated.
    void InvalidateAll();

    // Persistence
    /// @brief Resolves default persistence path (~/.horo/toolchains.json).
    static std::filesystem::path GetDefaultSettingsPath();

    /// @brief Save all toolchains to a JSON file. Returns true on success.
    bool SaveToFile(const std::filesystem::path& settingsPath) const;

    /// @brief Load toolchains from a JSON file. Returns true on success.
    /// Existing toolchains are cleared before loading.
    bool LoadFromFile(const std::filesystem::path& settingsPath);

    // Metadata
    /// @brief Count of enabled toolchains for a specific target.
    std::size_t GetEnabledToolchainCountForTarget(
        const ReleaseTargetTriple& target) const;

    /// @brief Check if at least one valid enabled toolchain exists for a target.
    bool HasAnyValidToolchainForTarget(const ReleaseTargetTriple& target) const;

    /// @brief Find the first valid enabled toolchain for a target, or nullptr.
    ToolchainConfig* FindValidEnabledToolchain(const ReleaseTargetTriple& target);
    const ToolchainConfig* FindValidEnabledToolchain(const ReleaseTargetTriple& target) const;

    /// @brief Clear all toolchains.
    void Clear();

    /// @brief Check if store is empty.
    bool IsEmpty() const;

private:
    // Key: ReleaseTargetTriple; Value: map of (name -> ToolchainConfig)
    std::map<ReleaseTargetTriple, std::map<std::string, ToolchainConfig>> m_toolchains;

    /// @brief Run all validation checks for a toolchain and update result.
    void RunValidationChecks(ToolchainConfig& config) const;
};

} // namespace Horo::Build

#endif // HORO_TOOLCHAIN_SETTINGS_H
