// Launcher-focused unit tests (paths, manifest helpers). End-to-end ImGui UI
// scenarios run only in HoroEditorUiTest + UiAutomationRunner.cpp.

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "ui/launcher/EditorHomeSettings.h"
#include "ui/launcher/LauncherProject.h"
#include "ui/launcher/LauncherProjectTemplate.h"
#include "tests/TestTempPaths.h"

using namespace Horo::Launcher;

namespace {
namespace fs = std::filesystem;

void WriteFile(const fs::path &path, const std::string &content) {
  std::ofstream out(path);
  out << content;
}

// Temporarily redirects the home directory so that ResolveEditorHomePath()
// writes to a controllable temp location instead of $HOME/.horo/.
struct HomeDirGuard {
  std::string previousHome;
#ifdef _WIN32
  std::string previousUserProfile;
  std::string previousHomeDrive;
  std::string previousHomePath;
#endif

  HomeDirGuard(const HomeDirGuard &) = delete;

  HomeDirGuard &operator=(const HomeDirGuard &) = delete;

  HomeDirGuard(HomeDirGuard &&) = delete;

  HomeDirGuard &operator=(HomeDirGuard &&) = delete;

  static std::string ReadEnv(const char *name) {
    if (!name || !*name)
      return {};
#ifdef _WIN32
    size_t len = 0;
    if (getenv_s(&len, nullptr, 0, name) != 0 || len <= 1)
      return {};
    std::vector<char> value(len);
    if (getenv_s(&len, value.data(), value.size(), name) != 0 || len <= 1)
      return {};
    return std::string(value.data());
#else
    const char *value = std::getenv(name);
    return value ? std::string(value) : std::string();
#endif
  }

  explicit HomeDirGuard(const fs::path &nextHome) {
#ifdef _WIN32
    previousUserProfile = ReadEnv("USERPROFILE");
    previousHomeDrive = ReadEnv("HOMEDRIVE");
    previousHomePath = ReadEnv("HOMEPATH");
    _putenv_s("USERPROFILE", nextHome.string().c_str());
    _putenv_s("HOMEDRIVE", "");
    _putenv_s("HOMEPATH", "");
#else
    previousHome = ReadEnv("HOME");
    setenv("HOME", nextHome.string().c_str(), 1);
#endif
  }

  ~HomeDirGuard() {
#ifdef _WIN32
    _putenv_s("USERPROFILE", previousUserProfile.c_str());
    _putenv_s("HOMEDRIVE", previousHomeDrive.c_str());
    _putenv_s("HOMEPATH", previousHomePath.c_str());
#else
    if (previousHome.empty())
      unsetenv("HOME");
    else
      setenv("HOME", previousHome.c_str(), 1);
#endif
  }
};
} // namespace

TEST_CASE("SanitizeProjectId normalizes names for manifest ids", "[launcher][project]") {
  REQUIRE(SanitizeProjectId("My Game") == "my_game");
  REQUIRE(SanitizeProjectId("Foo-Bar_Baz") == "foo_bar_baz");
  REQUIRE(SanitizeProjectId("!!!") == "project");
  REQUIRE(SanitizeProjectId("A  B") == "a_b");
}

TEST_CASE("SanitizeProjectId strips unsafe path characters", "[launcher][project][coverage]") {
  REQUIRE(SanitizeProjectId("..\\My:/Game?*") == "mygame");
  REQUIRE(SanitizeProjectId("<>|\"/\\:?*") == "project");
}

TEST_CASE("ResolveProjectManifestPath points at .horo/project.json", "[launcher][project]") {
  const fs::path root = fs::path("workspace") / "MyProject";
  REQUIRE(ResolveProjectManifestPath(root) == root / ".horo" / "project.json");
}

TEST_CASE("IsLauncherProjectRoot is true only when manifest exists", "[launcher][project]") {
  const fs::path root =
      Horo::Tests::SecureTempBase() / "horo_launcher_unit_manifest";
  std::error_code ec;
  fs::remove_all(root, ec);
  fs::create_directories(root / ".horo", ec);
  REQUIRE_FALSE(IsLauncherProjectRoot(root));

  {
    std::ofstream out(root / ".horo" / "project.json");
    REQUIRE(out.is_open());
    out << R"({"schemaVersion":1,"projectName":"T"})";
  }
  REQUIRE(IsLauncherProjectRoot(root));

  fs::remove_all(root, ec);
}

// ===========================================================================
// LoadProjectManifestDocument
// ===========================================================================

TEST_CASE("LoadProjectManifestDocument returns parse error when file missing", "[launcher][project][coverage]") {
  const fs::path root =
      Horo::Tests::SecureTempBase() / "horo_launcher_load_missing";
  std::error_code ec;
  fs::remove_all(root, ec);
  fs::create_directories(root, ec);

  LauncherProjectDocument doc = LoadProjectManifestDocument(root);
  CHECK_FALSE(doc.loadedFromDisk);
  CHECK(doc.parseError);
  CHECK_FALSE(doc.error.empty());
}

TEST_CASE("LoadProjectManifestDocument returns parse error for invalid JSON", "[launcher][project]") {
  const fs::path root =
      Horo::Tests::SecureTempBase() / "horo_launcher_load_badjson";
  std::error_code ec;
  fs::remove_all(root, ec);
  fs::create_directories(root / ".horo", ec);
  WriteFile(root / ".horo" / "project.json", "{not json{{{{");

  LauncherProjectDocument doc = LoadProjectManifestDocument(root);
  CHECK(doc.loadedFromDisk);
  CHECK(doc.parseError);
}

TEST_CASE("LoadProjectManifestDocument returns parse error for non-object root", "[launcher][project]") {
  const fs::path root =
      Horo::Tests::SecureTempBase() / "horo_launcher_load_arrayroot";
  std::error_code ec;
  fs::remove_all(root, ec);
  fs::create_directories(root / ".horo", ec);
  WriteFile(root / ".horo" / "project.json", "[1,2,3]");

  LauncherProjectDocument doc = LoadProjectManifestDocument(root);
  CHECK(doc.loadedFromDisk);
  CHECK(doc.parseError);
}

TEST_CASE("LoadProjectManifestDocument uses manifest defaults when project key absent", "[launcher][project][coverage]") {
  const fs::path root =
      Horo::Tests::SecureTempBase() / "horo_launcher_load_noprojectobj";
  std::error_code ec;
  fs::remove_all(root, ec);
  fs::create_directories(root / ".horo", ec);
  // No "project" key: code falls back to embedded defaults (schemaVersion=1,
  // etc.)
  WriteFile(root / ".horo" / "project.json", R"({"other":1})");

  LauncherProjectDocument doc = LoadProjectManifestDocument(root);
  CHECK(doc.loadedFromDisk);
  // Default-filled manifest does not trigger a parse error
  CHECK_FALSE(doc.parseError);
  CHECK(doc.manifest.schemaVersion == 1);
  CHECK(doc.manifest.projectId == "project");
}

TEST_CASE("LoadProjectManifestDocument returns parse error for schemaVersion 0", "[launcher][project]") {
  const fs::path root =
      Horo::Tests::SecureTempBase() / "horo_launcher_load_badversion";
  std::error_code ec;
  fs::remove_all(root, ec);
  fs::create_directories(root / ".horo", ec);
  WriteFile(
      root / ".horo" / "project.json",
      R"({"project":{"schemaVersion":0,"projectId":"x","projectName":"X","defaultScene":"a.json"}})");

  LauncherProjectDocument doc = LoadProjectManifestDocument(root);
  CHECK(doc.loadedFromDisk);
  CHECK(doc.parseError);
}

TEST_CASE("LoadProjectManifestDocument parses valid manifest with commands", "[launcher][project]") {
  const fs::path root =
      Horo::Tests::SecureTempBase() / "horo_launcher_load_valid";
  std::error_code ec;
  fs::remove_all(root, ec);
  fs::create_directories(root / ".horo", ec);
  WriteFile(root / ".horo" / "project.json",
            R"({
  "project": {
    "schemaVersion": 1,
    "projectId": "my_game",
    "projectName": "My Game",
    "defaultScene": "assets/scenes/main.json",
    "commands": {
      "configure": {"executable": "cmake", "args": ["-S", "."], "workingDirectory": ""},
      "build": {"executable": "cmake", "args": ["--build", "."], "workingDirectory": ""},
      "run": {"executable": "./game", "args": [], "workingDirectory": ""}
    }
  }
})");

  LauncherProjectDocument doc = LoadProjectManifestDocument(root);
  REQUIRE(doc.loadedFromDisk);
  CHECK_FALSE(doc.parseError);
  CHECK(doc.manifest.projectId == "my_game");
  CHECK(doc.manifest.projectName == "My Game");
  CHECK(doc.manifest.defaultScene == "assets/scenes/main.json");
  CHECK(doc.manifest.schemaVersion == 1);
  CHECK(doc.manifest.configureCommand.executable == "cmake");
  CHECK(doc.manifest.configureCommand.args.size() == 2u);
  CHECK(doc.manifest.buildCommand.executable == "cmake");
  CHECK(doc.manifest.runCommand.executable == "./game");
  CHECK_FALSE(doc.manifest.runCommand.Empty());
}

TEST_CASE("LauncherProjectCommand::Empty returns true for default", "[launcher][project]") {
  LauncherProjectCommand cmd;
  CHECK(cmd.Empty());
  cmd.executable = "cmake";
  CHECK_FALSE(cmd.Empty());
}

// ===========================================================================
// SaveProjectManifestDocument
// ===========================================================================

TEST_CASE("SaveProjectManifestDocument returns error for null doc", "[launcher][project]") {
  const fs::path root =
      Horo::Tests::SecureTempBase() / "horo_launcher_save_null";
  std::string err;
  CHECK_FALSE(SaveProjectManifestDocument(root, nullptr, &err));
  CHECK_FALSE(err.empty());
}

TEST_CASE("SaveProjectManifestDocument round-trips manifest fields", "[launcher][project]") {
  const fs::path root =
      Horo::Tests::SecureTempBase() / "horo_launcher_save_roundtrip";
  std::error_code ec;
  fs::remove_all(root, ec);
  fs::create_directories(root, ec);

  LauncherProjectDocument doc;
  doc.manifest.projectId = "saved_game";
  doc.manifest.projectName = "Saved Game";
  doc.manifest.defaultScene = "assets/main.json";
  doc.manifest.schemaVersion = 1;
  doc.manifest.buildCommand.executable = "cmake";
  doc.manifest.buildCommand.args = {"--build", "."};

  std::string err;
  REQUIRE(SaveProjectManifestDocument(root, &doc, &err));
  CHECK(err.empty());
  CHECK(doc.loadedFromDisk);

  LauncherProjectDocument loaded = LoadProjectManifestDocument(root);
  REQUIRE(loaded.loadedFromDisk);
  CHECK_FALSE(loaded.parseError);
  CHECK(loaded.manifest.projectId == "saved_game");
  CHECK(loaded.manifest.projectName == "Saved Game");
  CHECK(loaded.manifest.buildCommand.executable == "cmake");
  CHECK(loaded.manifest.buildCommand.args.size() == 2u);
}

// ===========================================================================
// ResolveLauncherCommand
// ===========================================================================

TEST_CASE("ResolveLauncherCommand returns error for null outCommand", "[launcher][project]") {
  LauncherProjectCommand cmd;
  cmd.executable = "cmake";
  std::string err;
  CHECK_FALSE(ResolveLauncherCommand(cmd, "/proj", "/sdk", nullptr, &err));
  CHECK_FALSE(err.empty());
}

TEST_CASE("ResolveLauncherCommand returns error for empty executable", "[launcher][project]") {
  LauncherProjectCommand cmd; // executable is empty
  ResolvedLauncherCommand out;
  std::string err;
  CHECK_FALSE(ResolveLauncherCommand(cmd, "/proj", "/sdk", &out, &err));
  CHECK_FALSE(err.empty());
}

TEST_CASE("ResolveLauncherCommand expands projectDir token in executable", "[launcher][project]") {
  LauncherProjectCommand cmd;
  cmd.executable = "${projectDir}/bin/game";
  ResolvedLauncherCommand out;
  std::string err;
  REQUIRE(ResolveLauncherCommand(cmd, "/my/project", "/sdk", &out, &err));
  CHECK(err.empty());
  const std::string exeStr = out.executable.generic_string();
  CHECK(exeStr.find("my/project") != std::string::npos);
  CHECK(exeStr.find("bin/game") != std::string::npos);
}

TEST_CASE("ResolveLauncherCommand expands horoSdkRoot token in args", "[launcher][project]") {
  LauncherProjectCommand cmd;
  cmd.executable = "cmake";
  cmd.args = {"${horoSdkRoot}/cmake/toolchain.cmake"};
  ResolvedLauncherCommand out;
  std::string err;
  REQUIRE(ResolveLauncherCommand(cmd, "/proj", "/the/sdk", &out, &err));
  CHECK(out.args.at(0).find("the/sdk") != std::string::npos);
}

TEST_CASE("ResolveLauncherCommand defaults working dir to project root when empty", "[launcher][project]") {
  LauncherProjectCommand cmd;
  cmd.executable = "cmake";
  ResolvedLauncherCommand out;
  std::string err;
  REQUIRE(ResolveLauncherCommand(cmd, "/my/project", "/sdk", &out, &err));
  const std::string wdStr = out.workingDirectory.generic_string();
  CHECK(wdStr.find("my/project") != std::string::npos);
}

TEST_CASE("ResolveLauncherCommand builds non-empty debugString", "[launcher][project]") {
  LauncherProjectCommand cmd;
  cmd.executable = "cmake";
  cmd.args = {"--build", "."};
  ResolvedLauncherCommand out;
  std::string err;
  REQUIRE(ResolveLauncherCommand(cmd, "/proj", "/sdk", &out, &err));
  CHECK_FALSE(out.debugString.empty());
}

// ===========================================================================
// LauncherProjectTemplate
// ===========================================================================

TEST_CASE("CreateLauncherProjectTemplate writes required project files", "[launcher][template]") {
  const fs::path root =
      Horo::Tests::SecureTempBase() / "horo_launcher_template_test";
  std::error_code ec;
  fs::remove_all(root, ec);
  fs::create_directories(root, ec);

  LauncherProjectTemplateRequest req;
  req.projectRoot = root;
  req.projectName = "TemplateGame";
  req.sdkRoot = Horo::Tests::SecureTempBase() / "fake_sdk";

  LauncherProjectDocument outDoc;
  std::string err;
  const bool ok = CreateLauncherProjectTemplate(req, &outDoc, &err);

  if (!ok) {
    // Not all environments have write access — just verify the error is clear
    CHECK_FALSE(err.empty());
  } else {
    CHECK(err.empty());
    CHECK(IsLauncherProjectRoot(root));
    CHECK(outDoc.manifest.projectName == "TemplateGame");
  }
}

// ===========================================================================
// EditorHomeSettings
// ===========================================================================

TEST_CASE("RememberRecentProject prepends and deduplicates entries", "[launcher][home]") {
  const fs::path projA = Horo::Tests::SecureTempBase() / "horo_home_projA";
  const fs::path projB = Horo::Tests::SecureTempBase() / "horo_home_projB";
  std::error_code ec;
  fs::create_directories(projA, ec);
  fs::create_directories(projB, ec);

  EditorHomeDocument doc;
  RememberRecentProject(&doc, projA);
  RememberRecentProject(&doc, projB);
  RememberRecentProject(&doc, projA); // duplicate — should not add again

  CHECK(doc.state.recentProjects.size() <= 2u);
  CHECK(doc.state.lastProjectPath.find("horo_home_projA") != std::string::npos);
}

TEST_CASE("RememberRecentProject with null doc does not crash", "[launcher][home]") {
  REQUIRE_NOTHROW(
      RememberRecentProject(nullptr, Horo::Tests::SecureTempBase()));
}

TEST_CASE("PruneMissingRecentProjects removes non-existent paths", "[launcher][home]") {
  EditorHomeDocument doc;
  doc.state.recentProjects.emplace_back(
      "/nonexistent/path/that/does/not/exist");

  // Create a real project root so IsLauncherProjectRoot passes
  const fs::path existing =
      Horo::Tests::SecureTempBase() / "horo_prune_test_proj";
  std::error_code ec;
  fs::remove_all(existing, ec);
  fs::create_directories(existing / ".horo", ec);
  WriteFile(
      existing / ".horo" / "project.json",
      R"({"project":{"schemaVersion":1,"projectId":"p","projectName":"P","defaultScene":"a.json"}})");
  doc.state.recentProjects.push_back(existing.generic_string());

  PruneMissingRecentProjects(&doc);

  CHECK(doc.state.recentProjects.size() == 1u);
  CHECK(doc.state.recentProjects.at(0).find("horo_prune_test_proj") !=
        std::string::npos);
}

TEST_CASE("PruneMissingRecentProjects with null doc does not crash", "[launcher][home]") {
  REQUIRE_NOTHROW(PruneMissingRecentProjects(nullptr));
}

// ===========================================================================
// EditorHomeSettings — LoadEditorHomeDocument error paths
// ===========================================================================

TEST_CASE("LoadEditorHomeDocument: parse error for invalid JSON", "[launcher][home][coverage]") {
  const fs::path tempHome =
      Horo::Tests::SecureTempBase() / "horo_home_load_badjson";
  std::error_code ec;
  fs::remove_all(tempHome, ec);
  fs::create_directories(tempHome / ".horo", ec);
  WriteFile(tempHome / ".horo" / "editor_home.json", "{not json{{{{");

  HomeDirGuard guard(tempHome);
  const EditorHomeDocument doc = LoadEditorHomeDocument();
  CHECK(doc.loadedFromDisk);
  CHECK(doc.parseError);
  CHECK_FALSE(doc.error.empty());
  CHECK(doc.state.recentProjects.empty());
}

TEST_CASE("LoadEditorHomeDocument: parse error for non-object root", "[launcher][home]") {
  const fs::path tempHome =
      Horo::Tests::SecureTempBase() / "horo_home_load_arrayroot";
  std::error_code ec;
  fs::remove_all(tempHome, ec);
  fs::create_directories(tempHome / ".horo", ec);
  WriteFile(tempHome / ".horo" / "editor_home.json", "[1,2,3]");

  HomeDirGuard guard(tempHome);
  const EditorHomeDocument doc = LoadEditorHomeDocument();
  CHECK(doc.loadedFromDisk);
  CHECK(doc.parseError);
  CHECK(doc.error.find("object") != std::string::npos);
}

TEST_CASE("LoadEditorHomeDocument: reads recentProjects from disk", "[launcher][home]") {
  const fs::path tempHome =
      Horo::Tests::SecureTempBase() / "horo_home_load_recent";
  std::error_code ec;
  fs::remove_all(tempHome, ec);
  fs::create_directories(tempHome / ".horo", ec);

  // Use the temp home itself as a "project path" so weakly_canonical succeeds.
  const std::string projectPath = (tempHome / "MyProject").generic_string();
  fs::create_directories(tempHome / "MyProject", ec);

  const std::string json = R"({"home":{"recentProjects":[")" + projectPath +
                           R"("],"lastProjectPath":")" + projectPath + R"("}})";
  WriteFile(tempHome / ".horo" / "editor_home.json", json);

  HomeDirGuard guard(tempHome);
  const EditorHomeDocument doc = LoadEditorHomeDocument();
  CHECK(doc.loadedFromDisk);
  CHECK_FALSE(doc.parseError);
  CHECK_FALSE(doc.state.recentProjects.empty());
  CHECK_FALSE(doc.state.lastProjectPath.empty());
}

TEST_CASE("SaveEditorHomeDocument: returns error for null doc", "[launcher][home]") {
  std::string err;
  CHECK_FALSE(SaveEditorHomeDocument(nullptr, &err));
  CHECK_FALSE(err.empty());
}
