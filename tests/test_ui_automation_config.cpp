#include <catch2/catch_test_macros.hpp>

#include <filesystem>

#include "studio/UiAutomationConfig.h"

using namespace Monolith;

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
  const std::filesystem::path cwd = std::filesystem::path("/tmp/work");

  REQUIRE(ResolveUiCaptureOutputDir(false, "custom", cwd).empty());
  REQUIRE(ResolveUiCaptureOutputDir(true, "custom", cwd) == std::filesystem::path("custom"));
  REQUIRE(ResolveUiCaptureOutputDir(true, "", cwd) == cwd / "ui_test_output");
}

TEST_CASE("SelectUiAutomationBaseDir prefers platform-specific home roots", "[launcher][ui-automation]") {
  const std::filesystem::path cwd = std::filesystem::path("/cwd");

  REQUIRE(SelectUiAutomationBaseDir("/home/user", "C:/Users/User", cwd, true) ==
          std::filesystem::path("C:/Users/User"));
  REQUIRE(SelectUiAutomationBaseDir("/home/user", "", cwd, true) == std::filesystem::path("/home/user"));
  REQUIRE(SelectUiAutomationBaseDir("", "", cwd, true) == cwd);

  REQUIRE(SelectUiAutomationBaseDir("/home/user", "ignored", cwd, false) ==
          std::filesystem::path("/home/user"));
  REQUIRE(SelectUiAutomationBaseDir("", "ignored", cwd, false) == cwd);
}
