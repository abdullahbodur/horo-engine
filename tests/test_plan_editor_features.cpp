#include <catch2/catch_test_macros.hpp>

#include <string>
#include <unordered_set>
#include <vector>

#include "core/EngineLaunchArgs.h"
#include "core/LogBuffer.h"
#include "editor/ProjectEntryFilter.h"

using namespace Monolith;
using namespace Monolith::Editor;

TEST_CASE("EngineLaunchArgs: default when no flags", "[engine][cli]") {
  std::vector<std::string> args = {"MonolithApp"};
  std::vector<char *> argv;
  argv.reserve(args.size());
  for (std::string &arg : args)
    argv.push_back(arg.data());
  REQUIRE(ParseEditorStartupCli(static_cast<int>(argv.size()), argv.data()) ==
          EditorStartupCli::Default);
}

TEST_CASE("EngineLaunchArgs: --editor and --play", "[engine][cli]") {
  std::vector<std::string> argsEditor = {"app", "--editor"};
  std::vector<char *> avEd;
  avEd.reserve(argsEditor.size());
  for (std::string &arg : argsEditor)
    avEd.push_back(arg.data());
  REQUIRE(ParseEditorStartupCli(static_cast<int>(avEd.size()), avEd.data()) ==
          EditorStartupCli::ForceEditor);
  std::vector<std::string> argsPlay = {"app", "--play"};
  std::vector<char *> avPl;
  avPl.reserve(argsPlay.size());
  for (std::string &arg : argsPlay)
    avPl.push_back(arg.data());
  REQUIRE(ParseEditorStartupCli(static_cast<int>(avPl.size()), avPl.data()) ==
          EditorStartupCli::ForcePlay);
}

TEST_CASE("EngineLaunchArgs: last flag wins", "[engine][cli]") {
  std::vector<std::string> args = {"app", "--editor", "--play"};
  std::vector<char *> argv;
  argv.reserve(args.size());
  for (std::string &arg : args)
    argv.push_back(arg.data());
  REQUIRE(ParseEditorStartupCli(static_cast<int>(argv.size()), argv.data()) ==
          EditorStartupCli::ForcePlay);
  std::vector<std::string> args2 = {"app", "--play", "--editor"};
  std::vector<char *> argv2;
  argv2.reserve(args2.size());
  for (std::string &arg : args2)
    argv2.push_back(arg.data());
  REQUIRE(ParseEditorStartupCli(static_cast<int>(argv2.size()), argv2.data()) ==
          EditorStartupCli::ForceEditor);
}

TEST_CASE("ShouldStartWithEditor: force and release/debug defaults",
          "[engine][cli]") {
  REQUIRE(ShouldStartWithEditor(EditorStartupCli::ForceEditor, true));
  REQUIRE(ShouldStartWithEditor(EditorStartupCli::ForceEditor, false));
  REQUIRE_FALSE(ShouldStartWithEditor(EditorStartupCli::ForcePlay, true));
  REQUIRE_FALSE(ShouldStartWithEditor(EditorStartupCli::ForcePlay, false));
  REQUIRE_FALSE(ShouldStartWithEditor(EditorStartupCli::Default, true));
  REQUIRE(ShouldStartWithEditor(EditorStartupCli::Default, false));
}

TEST_CASE("LogBuffer: ring limit and clear", "[engine][log]") {
  LogBuffer &lb = LogBuffer::Instance();
  lb.Clear();
  const uint64_t rev0 = lb.Revision();
  lb.SetMaxLines(3);
  lb.Push(LogLevel::Info, "a.cpp", 1, "one");
  REQUIRE(lb.Revision() > rev0);
  lb.Push(LogLevel::Warn, "b.cpp", 2, "two");
  lb.Push(LogLevel::Error, "c.cpp", 3, "three");
  lb.Push(LogLevel::Info, "d.cpp", 4, "four");

  int ci = 0;
  int cw = 0;
  int ce = 0;
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

  std::unordered_set<std::string, Monolith::StringHash, std::equal_to<>> extra{
      {"vendor"}};
  REQUIRE(IsBlockedProjectDirName("vendor", &extra));
  REQUIRE_FALSE(IsBlockedProjectDirName("vendor", nullptr));
}
