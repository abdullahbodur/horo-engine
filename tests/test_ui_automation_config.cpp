#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

#include "ui/launcher/UiAutomationConfig.h"

using namespace Horo;

TEST_CASE("ParseUiAutomationBoolValue handles empty and numeric values", "[launcher][ui-automation]") {
  REQUIRE(ParseUiAutomationBoolValue({}, false) == false);
  REQUIRE(ParseUiAutomationBoolValue({}, true) == true);
  REQUIRE(ParseUiAutomationBoolValue("0", true) == false);
  REQUIRE(ParseUiAutomationBoolValue("1", false) == true);
  REQUIRE(ParseUiAutomationBoolValue("true", false) == true);
}

TEST_CASE("ParseUiAutomationNonNegativeIntValue validates input", "[launcher][ui-automation]") {
  REQUIRE(ParseUiAutomationNonNegativeIntValue("", 7) == 7);
  REQUIRE(ParseUiAutomationNonNegativeIntValue("0", 7) == 0);
  REQUIRE(ParseUiAutomationNonNegativeIntValue("42", 7) == 42);
  REQUIRE(ParseUiAutomationNonNegativeIntValue("-1", 7) == 7);
  REQUIRE(ParseUiAutomationNonNegativeIntValue("abc", 7) == 7);
  REQUIRE(ParseUiAutomationNonNegativeIntValue("12x", 7) == 7);
}

TEST_CASE("ResolveUiCaptureOutputDir obeys capture flag and env", "[launcher][ui-automation]") {
  const auto cwd = std::filesystem::path("workspace/work");

  REQUIRE(ResolveUiCaptureOutputDir(false, "custom", cwd).empty());
  REQUIRE(ResolveUiCaptureOutputDir(true, "custom", cwd) ==
          std::filesystem::path("custom"));
  REQUIRE(ResolveUiCaptureOutputDir(true, "", cwd) == cwd / "ui_test_output");
}

TEST_CASE("UI automation defaults avoid heartbeat spam and allow long runs", "[launcher][ui-automation]") {
  REQUIRE(kUiAutomationDefaultMaxFrames == 300000);
  REQUIRE(kUiAutomationLargeFrameDeltaWarningSec == 1.0);
}

TEST_CASE("UI automation heartbeat and frame-delta logging decisions are independent", "[launcher][ui-automation]") {
  REQUIRE_FALSE(ShouldLogUiAutomationHeartbeat(false, 1, 30));
  REQUIRE_FALSE(ShouldLogUiAutomationHeartbeat(false, 30, 30));
  REQUIRE_FALSE(ShouldLogUiAutomationHeartbeat(true, 31, 30));
  REQUIRE(ShouldLogUiAutomationHeartbeat(true, 1, 30));
  REQUIRE(ShouldLogUiAutomationHeartbeat(true, 30, 30));

  REQUIRE_FALSE(ShouldWarnUiAutomationLargeFrameDelta(1.0));
  REQUIRE(ShouldWarnUiAutomationLargeFrameDelta(1.01));
}

TEST_CASE("Editor render heartbeat is opt-in and sampled", "[launcher][ui-automation]") {
  REQUIRE_FALSE(ShouldLogEditorRenderHeartbeat(false, 1));
  REQUIRE_FALSE(ShouldLogEditorRenderHeartbeat(false, 60));

  REQUIRE(ShouldLogEditorRenderHeartbeat(true, 1));
  REQUIRE(ShouldLogEditorRenderHeartbeat(true, 60));
  REQUIRE_FALSE(ShouldLogEditorRenderHeartbeat(true, 59));
}

TEST_CASE("UI test suites resolve to native scenario filters", "[launcher][ui-automation]") {
  const auto suiteNames = UiAutomationTestSuiteNames();
  REQUIRE(suiteNames.size() == 5);
  REQUIRE(std::ranges::find(suiteNames, "launcher-basic") != suiteNames.end());
  REQUIRE(std::ranges::find(suiteNames, "properties-workflows") != suiteNames.end());
  REQUIRE(std::ranges::find(suiteNames, "mcp-project") != suiteNames.end());
  REQUIRE(std::ranges::find(suiteNames, "modals-mcp") != suiteNames.end());
  REQUIRE(std::ranges::find(suiteNames, "properties-close") != suiteNames.end());

  REQUIRE(ResolveUiAutomationScenarioFilter("explicit/scenario", "mcp-project") ==
          "explicit/scenario");
  REQUIRE(ResolveUiAutomationScenarioFilter("", "").empty());
  REQUIRE(ResolveUiAutomationScenarioFilter("", "unknown-suite") ==
          "__invalid_ui_shard__");

  const std::string mcpFilter(ResolveUiAutomationScenarioFilter("", "modals-mcp"));
  REQUIRE(mcpFilter.find("editor/mcp_enable_and_verify_running") != std::string::npos);
  REQUIRE(mcpFilter.find("editor/mcp_send_request_and_verify_log") <
          mcpFilter.find("editor/mcp_live_request_visibility"));
  REQUIRE(mcpFilter.find("editor/mcp_live_request_visibility") <
          mcpFilter.find("editor/mcp_request_detail_fields_visible"));
  REQUIRE(mcpFilter.find("editor/mcp_clear_request_log") >
          mcpFilter.find("editor/mcp_open_settings_from_tab"));
}

TEST_CASE("SelectUiAutomationBaseDir prefers platform-specific home roots", "[launcher][ui-automation]") {
  const auto cwd = std::filesystem::path("/cwd");

  REQUIRE(SelectUiAutomationBaseDir("/home/user", "C:/Users/User", cwd, true) ==
          std::filesystem::path("C:/Users/User"));
  REQUIRE(SelectUiAutomationBaseDir("/home/user", "", cwd, true) ==
          std::filesystem::path("/home/user"));
  REQUIRE(SelectUiAutomationBaseDir("", "", cwd, true) == cwd);

  REQUIRE(SelectUiAutomationBaseDir("/home/user", "ignored", cwd, false) ==
          std::filesystem::path("/home/user"));
  REQUIRE(SelectUiAutomationBaseDir("", "ignored", cwd, false) == cwd);
}
