#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

#include "core/EngineLaunchArgs.h"
#include "core/ProjectPath.h"
#include "editor/EditorWorkspaceSettings.h"
#include "standalone/EditorHomeSettings.h"
#include "standalone/ExternalProcessRunner.h"
#include "standalone/StandaloneProject.h"
#include "standalone/StandaloneProjectTemplate.h"

using namespace Monolith;
using namespace Monolith::Editor;
using namespace Monolith::Standalone;

namespace {

std::filesystem::path MakeTempRoot(const std::string& name) {
  return std::filesystem::temp_directory_path() / "horo_standalone_tests" / name;
}

struct HomeDirGuard {
  std::string previousUserProfile;
  std::string previousHomeDrive;
  std::string previousHomePath;
  std::string previousHome;

  static std::string ReadEnv(const char* name) {
    if (!name || !*name)
      return {};
#ifdef _WIN32
    char* value = nullptr;
    size_t len = 0;
    if (_dupenv_s(&value, &len, name) != 0 || !value)
      return {};
    std::string out(value);
    free(value);
    return out;
#else
    const char* value = std::getenv(name);
    return value ? std::string(value) : std::string();
#endif
  }

  explicit HomeDirGuard(const std::filesystem::path& nextHome)
      : previousUserProfile(ReadEnv("USERPROFILE")),
        previousHomeDrive(ReadEnv("HOMEDRIVE")),
        previousHomePath(ReadEnv("HOMEPATH")),
        previousHome(ReadEnv("HOME")) {
#ifdef _WIN32
    _putenv_s("USERPROFILE", nextHome.string().c_str());
    _putenv_s("HOMEDRIVE", "");
    _putenv_s("HOMEPATH", "");
#else
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

}  // namespace

TEST_CASE("EngineLaunchArgs parses standalone project path", "[standalone][cli]") {
  char arg0[] = "HoroEditor";
  char projectFlag[] = "--project";
  char projectPath[] = "C:/games/demo";
  char* argv[] = {arg0, projectFlag, projectPath};

  const EngineLaunchOptions options = ParseEngineLaunchOptions(3, argv);

  REQUIRE(options.editorStartup == EditorStartupCli::Default);
  REQUIRE(options.projectPath == std::filesystem::path(projectPath));
}

TEST_CASE("ProjectPath explicit root switches editor workspace to project-local settings",
          "[standalone][workspace]") {
  const std::filesystem::path projectRoot = MakeTempRoot("project_workspace");
  std::filesystem::remove_all(projectRoot);
  std::filesystem::create_directories(projectRoot);

  ProjectPath::SetProjectRoot(projectRoot);
  REQUIRE(ProjectPath::HasExplicitProjectRoot());
  REQUIRE(ResolveEditorWorkspacePath() == projectRoot / ".horo" / "editor_workspace.json");

  ProjectPath::SetProjectRoot({});
  REQUIRE_FALSE(ProjectPath::HasExplicitProjectRoot());
}

TEST_CASE("Editor home settings remember and prune recent projects", "[standalone][home]") {
  const std::filesystem::path tempHome = MakeTempRoot("editor_home");
  std::filesystem::remove_all(tempHome);
  std::filesystem::create_directories(tempHome);
  HomeDirGuard guard(tempHome);

  const std::filesystem::path projectA = tempHome / "ProjectA";
  const std::filesystem::path projectB = tempHome / "ProjectB";
  std::filesystem::create_directories(projectA / ".horo");
  std::filesystem::create_directories(projectB / ".horo");
  std::ofstream(projectA / ".horo" / "project.json") << "{}";
  std::ofstream(projectB / ".horo" / "project.json") << "{}";

  EditorHomeDocument doc;
  RememberRecentProject(&doc, projectA);
  RememberRecentProject(&doc, projectB);
  RememberRecentProject(&doc, projectA);

  REQUIRE(doc.state.recentProjects.size() == 2);
  REQUIRE(doc.state.recentProjects.front() == projectA.lexically_normal().generic_string());

  std::filesystem::remove(projectB / ".horo" / "project.json");
  PruneMissingRecentProjects(&doc);
  REQUIRE(doc.state.recentProjects.size() == 1);

  std::string saveError;
  REQUIRE(SaveEditorHomeDocument(&doc, &saveError));
  const EditorHomeDocument loaded = LoadEditorHomeDocument();
  REQUIRE_FALSE(loaded.parseError);
  REQUIRE(loaded.state.recentProjects.size() == 1);
}

TEST_CASE("Standalone project manifest resolves SDK and project tokens", "[standalone][manifest]") {
  const std::filesystem::path projectRoot = MakeTempRoot("manifest_project");
  const std::filesystem::path sdkRoot = MakeTempRoot("manifest_sdk");

  ResolvedStandaloneCommand resolved;
  std::string error;
  REQUIRE(ResolveStandaloneCommand({"cmake",
                                    {"-S", "${projectDir}", "-B", "${projectDir}/build"},
                                    "${horoSdkRoot}"},
                                   projectRoot,
                                   sdkRoot,
                                   &resolved,
                                   &error));
  REQUIRE(resolved.executable == std::filesystem::path("cmake"));
  REQUIRE(resolved.args[1] == projectRoot.generic_string());
  REQUIRE(resolved.workingDirectory == sdkRoot.lexically_normal());
}

TEST_CASE("Standalone project template creates manifest, source, and scene scaffold",
          "[standalone][template]") {
  const std::filesystem::path projectRoot = MakeTempRoot("template_project");
  std::filesystem::remove_all(projectRoot);

  StandaloneProjectDocument doc;
  std::string error;
  REQUIRE(CreateStandaloneProjectTemplate({projectRoot, "TemplateGame", MakeTempRoot("sdk")},
                                          &doc,
                                          &error));

  REQUIRE(IsStandaloneProjectRoot(projectRoot));
  REQUIRE(std::filesystem::exists(projectRoot / "src" / "main.cpp"));
  REQUIRE(std::filesystem::exists(projectRoot / "CMakeLists.txt"));
  REQUIRE(std::filesystem::exists(projectRoot / "assets" / "scenes" / "level.json"));
  REQUIRE(doc.manifest.projectName == "TemplateGame");
  REQUIRE(doc.manifest.defaultScene == "assets/scenes/level.json");
}

TEST_CASE("External process runner starts, completes, and records exit status",
          "[standalone][process]") {
  ExternalProcessRunner runner;
  ResolvedStandaloneCommand command;
  command.workingDirectory = std::filesystem::temp_directory_path();
#ifdef _WIN32
  command.executable = "cmd";
  command.args = {"/c", "echo horo-standalone"};
#else
  command.executable = "sh";
  command.args = {"-c", "printf horo-standalone\\n"};
#endif
  command.debugString = command.executable.generic_string();

  std::string error;
  REQUIRE(runner.Start(command, "test-process", &error));

  for (int i = 0; i < 100 && runner.IsActive(); ++i) {
    runner.Poll();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  REQUIRE_FALSE(runner.IsActive());
  REQUIRE(runner.GetStatus().finished);
  REQUIRE(runner.GetStatus().exitCode == 0);
}
