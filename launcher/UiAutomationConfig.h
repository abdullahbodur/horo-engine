#pragma once

#include <filesystem>
#include <string_view>

namespace Monolith {

// Parses UI automation boolean env-like values.
// Empty value => fallback.
// "0" => false, anything else => true.
bool ParseUiAutomationBoolValue(std::string_view value, bool fallback);

// Parses non-negative integer env-like values.
// Returns fallback for invalid/empty values.
int ParseUiAutomationNonNegativeIntValue(std::string_view value, int fallback);

// Chooses capture output directory for UI automation.
std::filesystem::path ResolveUiCaptureOutputDir(bool captureEnabled,
                                                std::string_view outputDirEnv,
                                                const std::filesystem::path& currentPath);

// Chooses base directory for temporary UI automation data.
std::filesystem::path SelectUiAutomationBaseDir(std::string_view homePath,
                                                std::string_view userProfilePath,
                                                const std::filesystem::path& currentPath,
                                                bool isWindows);

}  // namespace Monolith
