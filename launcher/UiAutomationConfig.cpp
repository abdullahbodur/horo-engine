#include "launcher/UiAutomationConfig.h"

#include <charconv>

namespace Monolith {

bool ParseUiAutomationBoolValue(std::string_view value, bool fallback) {
  if (value.empty())
    return fallback;
  return value != "0";
}

int ParseUiAutomationNonNegativeIntValue(std::string_view value, int fallback) {
  if (value.empty())
    return fallback;

  int parsed = 0;
  const char* first = value.data();
  const char* last = first + value.size();
  const auto [ptr, ec] = std::from_chars(first, last, parsed);
  if (ec != std::errc() || ptr != last || parsed < 0)
    return fallback;
  return parsed;
}

std::filesystem::path ResolveUiCaptureOutputDir(const bool captureEnabled,
                                                const std::string_view outputDirEnv,
                                                const std::filesystem::path& currentPath) {
  if (!captureEnabled)
    return {};
  if (!outputDirEnv.empty())
    return std::filesystem::path(outputDirEnv);
  return currentPath / "ui_test_output";
}

std::filesystem::path SelectUiAutomationBaseDir(const std::string_view homePath,
                                                const std::string_view userProfilePath,
                                                const std::filesystem::path& currentPath,
                                                const bool isWindows) {
  if (isWindows) {
    if (!userProfilePath.empty())
      return std::filesystem::path(userProfilePath);
    if (!homePath.empty())
      return std::filesystem::path(homePath);
    return currentPath;
  }

  if (!homePath.empty())
    return std::filesystem::path(homePath);
  return currentPath;
}

}  // namespace Monolith
