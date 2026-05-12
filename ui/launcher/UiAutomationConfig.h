/** @file UiAutomationConfig.h
 *  @brief Constants and configuration helpers for the UI automation test runner. */
#pragma once

#include <filesystem>
#include <string_view>
#include <vector>

namespace Horo {

/** @brief Maximum number of frames the automation runner will execute before timing out. */
inline constexpr int kUiAutomationDefaultMaxFrames = 300000;

/** @brief Frame delta threshold in seconds above which a large-delta warning is emitted. */
inline constexpr double kUiAutomationLargeFrameDeltaWarningSec = 1.0;

/** @brief Returns true when a heartbeat log line should be emitted for the current automation frame.
 *  @param enabled           Whether heartbeat logging is enabled.
 *  @param frameCount        The current frame index.
 *  @param heartbeatInterval Number of frames between heartbeat log lines.
 *  @return True if a heartbeat line should be logged on this frame. */
bool ShouldLogUiAutomationHeartbeat(bool enabled, int frameCount,
                                    int heartbeatInterval);

/** @brief Returns true when a large-frame-delta warning should be emitted.
 *  @param frameDeltaSec Elapsed time in seconds since the previous frame.
 *  @return True if the delta exceeds kUiAutomationLargeFrameDeltaWarningSec. */
bool ShouldWarnUiAutomationLargeFrameDelta(double frameDeltaSec);

/** @brief Returns true when a heartbeat log line should be emitted for the current editor render frame.
 *  @param enabled    Whether editor render heartbeat logging is enabled.
 *  @param frameCount The current editor frame index.
 *  @return True if a heartbeat line should be logged on this frame. */
bool ShouldLogEditorRenderHeartbeat(bool enabled, int frameCount);

/** @brief Returns the registered names of all available UI automation test suites.
 *  @return View list of suite name string views. */
std::vector<std::string_view> UiAutomationTestSuiteNames();

/** @brief Selects the scenario filter to apply when running a named test suite.
 *  @param explicitFilter An explicit filter override; takes precedence when non-empty.
 *  @param suiteName      The test suite name used to derive a default filter.
 *  @return The resolved filter string to pass to the test engine. */
std::string_view ResolveUiAutomationScenarioFilter(
    std::string_view explicitFilter, std::string_view suiteName);

/** @brief Parses a boolean value from an environment-variable-style string.
 *
 *  Empty string returns fallback. "0" returns false. Any other non-empty value returns true.
 *
 *  @param value    The string value to parse.
 *  @param fallback Value returned when the string is empty or unrecognized.
 *  @return The parsed boolean, or fallback when the input is empty. */
bool ParseUiAutomationBoolValue(std::string_view value, bool fallback);

/** @brief Parses a non-negative integer from an environment-variable-style string.
 *
 *  Returns fallback for empty, negative, or non-numeric input.
 *
 *  @param value    The string value to parse.
 *  @param fallback Value returned when the string is invalid or empty.
 *  @return The parsed integer, or fallback on failure. */
int ParseUiAutomationNonNegativeIntValue(std::string_view value, int fallback);

/** @brief Determines the directory where UI automation captures will be written.
 *  @param captureEnabled Whether frame capture is enabled.
 *  @param outputDirEnv   Value of the output directory environment variable; may be empty.
 *  @param currentPath    Current working directory used as a fallback root.
 *  @return Absolute path to the capture output directory. */
std::filesystem::path
ResolveUiCaptureOutputDir(bool captureEnabled, std::string_view outputDirEnv,
                          const std::filesystem::path &currentPath);

/** @brief Determines the base directory used for temporary UI automation data.
 *  @param homePath        Value of the HOME environment variable; may be empty.
 *  @param userProfilePath Value of the USERPROFILE environment variable; may be empty.
 *  @param currentPath     Current working directory used as a final fallback.
 *  @param isWindows       True when running on Windows (selects USERPROFILE over HOME).
 *  @return Absolute path to the selected base directory. */
std::filesystem::path SelectUiAutomationBaseDir(
    std::string_view homePath, std::string_view userProfilePath,
    const std::filesystem::path &currentPath, bool isWindows);

} // namespace Horo
