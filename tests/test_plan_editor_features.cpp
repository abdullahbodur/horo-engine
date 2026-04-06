#include <catch2/catch_test_macros.hpp>

#include <unordered_set>
#include <vector>

#include "core/EngineLaunchArgs.h"
#include "core/LogBuffer.h"
#include "editor/ProjectEntryFilter.h"

using namespace Monolith;
using namespace Monolith::Editor;

TEST_CASE("EngineLaunchArgs: default when no flags", "[engine][cli]") {
  char arg0[] = "MonolithApp";
  char* argv[] = {arg0};
  REQUIRE(ParseEditorStartupCli(1, argv) == EditorStartupCli::Default);
}

TEST_CASE("EngineLaunchArgs: --editor and --play", "[engine][cli]") {
  char arg0[] = "app";
  char ed[] = "--editor";
  char pl[] = "--play";
  char* avEd[] = {arg0, ed};
  REQUIRE(ParseEditorStartupCli(2, avEd) == EditorStartupCli::ForceEditor);
  char* avPl[] = {arg0, pl};
  REQUIRE(ParseEditorStartupCli(2, avPl) == EditorStartupCli::ForcePlay);
}

TEST_CASE("EngineLaunchArgs: last flag wins", "[engine][cli]") {
  char arg0[] = "app";
  char ed[] = "--editor";
  char pl[] = "--play";
  char* argv[] = {arg0, ed, pl};
  REQUIRE(ParseEditorStartupCli(3, argv) == EditorStartupCli::ForcePlay);
  char* argv2[] = {arg0, pl, ed};
  REQUIRE(ParseEditorStartupCli(3, argv2) == EditorStartupCli::ForceEditor);
}

TEST_CASE("ShouldStartWithEditor: force and release/debug defaults", "[engine][cli]") {
  REQUIRE(ShouldStartWithEditor(EditorStartupCli::ForceEditor, true));
  REQUIRE(ShouldStartWithEditor(EditorStartupCli::ForceEditor, false));
  REQUIRE_FALSE(ShouldStartWithEditor(EditorStartupCli::ForcePlay, true));
  REQUIRE_FALSE(ShouldStartWithEditor(EditorStartupCli::ForcePlay, false));
  REQUIRE_FALSE(ShouldStartWithEditor(EditorStartupCli::Default, true));
  REQUIRE(ShouldStartWithEditor(EditorStartupCli::Default, false));
}

TEST_CASE("LogBuffer: ring limit and clear", "[engine][log]") {
  LogBuffer& lb = LogBuffer::Instance();
  lb.Clear();
  const uint64_t rev0 = lb.Revision();
  lb.SetMaxLines(3);
  lb.Push(LogLevel::Info, "a.cpp", 1, "one");
  REQUIRE(lb.Revision() > rev0);
  lb.Push(LogLevel::Warn, "b.cpp", 2, "two");
  lb.Push(LogLevel::Error, "c.cpp", 3, "three");
  lb.Push(LogLevel::Info, "d.cpp", 4, "four");

  int ci = 0, cw = 0, ce = 0;
  lb.GetCounts(&ci, &cw, &ce);
  REQUIRE(ci == 2);
  REQUIRE(cw == 1);
  REQUIRE(ce == 1);

  std::vector<LogLine> lines;
  lb.CopyLinesTo(&lines);
  REQUIRE(lines.size() == 3);
  REQUIRE(lines.back().message == "four");

  const uint64_t revBeforeClear = lb.Revision();
  lb.Clear();
  REQUIRE(lb.Revision() > revBeforeClear);
  REQUIRE(lb.CountInfo() == 0);
  REQUIRE(lb.CountWarn() == 0);
  REQUIRE(lb.CountError() == 0);
  lb.CopyLinesTo(&lines);
  REQUIRE(lines.empty());
}

TEST_CASE("ProjectEntryFilter: dot files and blocklist", "[editor][project]") {
  REQUIRE(IsHiddenDotEntry(".git"));
  REQUIRE(IsHiddenDotEntry(".clangd"));
  REQUIRE_FALSE(IsHiddenDotEntry("src"));
  REQUIRE_FALSE(IsHiddenDotEntry(""));

  REQUIRE(IsBlockedProjectDirName("build", nullptr));
  REQUIRE(IsBlockedProjectDirName("engine", nullptr));
  REQUIRE_FALSE(IsBlockedProjectDirName("assets", nullptr));
  REQUIRE(IsBlockedProjectDirName("cmake-build-debug", nullptr));

  std::unordered_set<std::string> extra{{"vendor"}};
  REQUIRE(IsBlockedProjectDirName("vendor", &extra));
  REQUIRE_FALSE(IsBlockedProjectDirName("vendor", nullptr));
}
