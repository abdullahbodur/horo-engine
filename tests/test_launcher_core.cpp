#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "core/EngineLaunchArgs.h"
#include "core/ProjectPath.h"
#include "editor/EditorWorkspaceSettings.h"
#include "launcher/EditorHomeSettings.h"
#include "launcher/ExternalProcessRunner.h"
#include "launcher/LauncherProject.h"
#include "launcher/LauncherProjectTemplate.h"
#include "tests/TestTempPaths.h"

using namespace Monolith;
using namespace Monolith::Editor;
using namespace Monolith::Launcher;

namespace {
std::filesystem::path MakeTempRoot(const std::string &name) {
  return Monolith::Tests::SecureTempBase() / "horo_launcher_tests" / name;
}

std::string NormalizeComparablePath(const std::filesystem::path &path) {
  std::error_code ec;
  std::filesystem::path normalized =
      std::filesystem::weakly_canonical(path, ec);
  if (ec)
    normalized = std::filesystem::absolute(path, ec);
  if (ec)
    normalized = path;
  return normalized.lexically_normal().generic_string();
}

std::string ReadTextFile(const std::filesystem::path &path) {
  std::ifstream in(path);
  REQUIRE(in.is_open());
  return std::string(std::istreambuf_iterator<char>(in),
                     std::istreambuf_iterator<char>());
}

struct HomeDirGuard {
  std::string previousUserProfile = {};
  std::string previousHomeDrive = {};
  std::string previousHomePath = {};
  std::string previousHome = {};

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

  explicit HomeDirGuard(const std::filesystem::path &nextHome) {
    previousUserProfile = ReadEnv("USERPROFILE");
    previousHomeDrive = ReadEnv("HOMEDRIVE");
    previousHomePath = ReadEnv("HOMEPATH");
    previousHome = ReadEnv("HOME");
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
} // namespace

TEST_CASE("EngineLaunchArgs parses launcher project path", "[launcher][cli]") {
  std::string arg0 = "HoroEditor";
  std::string projectFlag = "--project";
  const std::filesystem::path projectPath = "C:/games/demo";
  std::string projectPathArg = projectPath.generic_string();
  std::array<char *, 3> argv = {arg0.data(), projectFlag.data(),
                                projectPathArg.data()};

  const EngineLaunchOptions options =
      ParseEngineLaunchOptions(static_cast<int>(argv.size()), argv.data());

  REQUIRE(options.editorStartup == EditorStartupCli::Default);
  REQUIRE(options.projectPath == projectPath);
}

TEST_CASE("EngineLaunchArgs handles inline project path and startup overrides", "[launcher][cli]") {
  std::string arg0 = "HoroEditor";
  std::string editorArg = "--editor";
  std::string playArg = "--play";
  std::string inlineProject = "--project=./Games/TestProject";
  std::array<char *, 4> argv = {arg0.data(), editorArg.data(),
                                inlineProject.data(), playArg.data()};

  const EngineLaunchOptions options =
      ParseEngineLaunchOptions(static_cast<int>(argv.size()), argv.data());

  REQUIRE(options.projectPath == std::filesystem::path("./Games/TestProject"));
  // Last startup flag wins.
  REQUIRE(options.editorStartup == EditorStartupCli::ForcePlay);
}

TEST_CASE("EngineLaunchArgs ignores null argv entries", "[launcher][cli]") {
  std::string arg0 = "HoroEditor";
  std::string projectFlag = "--project";
  std::string projectPathArg = "./SomeProject";
  std::array<char *, 5> argv = {arg0.data(), nullptr, projectFlag.data(),
                                projectPathArg.data(), nullptr};

  const EngineLaunchOptions options =
      ParseEngineLaunchOptions(static_cast<int>(argv.size()), argv.data());
  REQUIRE(options.projectPath == std::filesystem::path("./SomeProject"));
}

TEST_CASE("ProjectPath explicit root switches editor workspace to project-local settings", "[launcher][workspace]") {
  const std::filesystem::path projectRoot = MakeTempRoot("project_workspace");
  std::filesystem::remove_all(projectRoot);
  std::filesystem::create_directories(projectRoot);

  ProjectPath::SetProjectRoot(projectRoot);
  REQUIRE(ProjectPath::HasExplicitProjectRoot());
  REQUIRE(ResolveEditorWorkspacePath() ==
          std::filesystem::path(NormalizeComparablePath(
              projectRoot / ".horo" / "editor_workspace.json")));

  ProjectPath::SetProjectRoot({});
  REQUIRE_FALSE(ProjectPath::HasExplicitProjectRoot());
}

TEST_CASE("Editor home settings remember and prune recent projects", "[launcher][home][coverage]") {
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
  REQUIRE(doc.state.recentProjects.front() ==
          NormalizeComparablePath(projectA));
  REQUIRE(doc.state.recentProjects[1] == NormalizeComparablePath(projectB));
  REQUIRE(doc.state.lastProjectPath == NormalizeComparablePath(projectA));

  std::filesystem::remove(projectB / ".horo" / "project.json");
  PruneMissingRecentProjects(&doc);
  REQUIRE(doc.state.recentProjects.size() == 1);

  std::string saveError;
  REQUIRE(SaveEditorHomeDocument(&doc, &saveError));
  const EditorHomeDocument loaded = LoadEditorHomeDocument();
  REQUIRE_FALSE(loaded.parseError);
  REQUIRE(loaded.state.recentProjects.size() == 1);
  REQUIRE(loaded.state.recentProjects.front() ==
          NormalizeComparablePath(projectA));
}

TEST_CASE("Launcher project manifest resolves SDK and project tokens", "[launcher][manifest]") {
  const std::filesystem::path projectRoot = MakeTempRoot("manifest_project");
  const std::filesystem::path sdkRoot = MakeTempRoot("manifest_sdk");

  ResolvedLauncherCommand resolved;
  std::string error;
  REQUIRE(ResolveLauncherCommand(
      {"cmake",
       {"-S", "${projectDir}", "-B", "${projectDir}/build"},
       "${horoSdkRoot}"},
      projectRoot, sdkRoot, &resolved, &error));
  REQUIRE(resolved.executable == std::filesystem::path("cmake"));
  REQUIRE(resolved.args[1] == projectRoot.generic_string());
  REQUIRE(resolved.workingDirectory == sdkRoot.lexically_normal());
}

TEST_CASE("Launcher manifest load reports structured parse failures", "[launcher][manifest]") {
  const std::filesystem::path projectRoot =
      MakeTempRoot("manifest_parse_failures");
  std::error_code ec;
  std::filesystem::remove_all(projectRoot, ec);
  std::filesystem::create_directories(projectRoot / ".horo", ec);

  const std::filesystem::path manifestPath =
      ResolveProjectManifestPath(projectRoot);

  SECTION("invalid JSON") {
    {
      std::ofstream out(manifestPath);
      REQUIRE(out.is_open());
      out << "{ invalid json";
    }
    const LauncherProjectDocument doc =
        LoadProjectManifestDocument(projectRoot);
    REQUIRE(doc.parseError);
    REQUIRE(doc.loadedFromDisk);
    REQUIRE_FALSE(doc.error.empty());
  }

  SECTION("root is not object") {
    {
      std::ofstream out(manifestPath);
      REQUIRE(out.is_open());
      out << R"([1,2,3])";
    }
    const LauncherProjectDocument doc =
        LoadProjectManifestDocument(projectRoot);
    REQUIRE(doc.parseError);
    REQUIRE(doc.error.find("root must be an object") != std::string::npos);
  }

  SECTION("missing project object") {
    {
      std::ofstream out(manifestPath);
      REQUIRE(out.is_open());
      // Loader treats absent "project" as defaults; to validate this error
      // path, provide an explicit non-object project node.
      out << R"({"project":42})";
    }
    const LauncherProjectDocument doc =
        LoadProjectManifestDocument(projectRoot);
    REQUIRE(doc.parseError);
    REQUIRE(doc.error.find("missing 'project' object") != std::string::npos);
  }

  std::filesystem::remove_all(projectRoot, ec);
}

TEST_CASE("Launcher manifest save validates inputs", "[launcher][manifest]") {
  std::string error;
  REQUIRE_FALSE(SaveProjectManifestDocument(
      Monolith::Tests::SecureTempBase() / "unused", nullptr, &error));
  REQUIRE(error.find("document is null") != std::string::npos);
}

TEST_CASE("ResolveLauncherCommand validates required fields", "[launcher][manifest]") {
  const std::filesystem::path projectRoot =
      MakeTempRoot("resolve_command_validation_project");
  const std::filesystem::path sdkRoot =
      MakeTempRoot("resolve_command_validation_sdk");
  std::string error;

  SECTION("null output command") {
    REQUIRE_FALSE(ResolveLauncherCommand({"cmake", {}, ""}, projectRoot,
                                         sdkRoot, nullptr, &error));
    REQUIRE(error.find("output is null") != std::string::npos);
  }

  SECTION("missing executable") {
    ResolvedLauncherCommand resolved;
    REQUIRE_FALSE(ResolveLauncherCommand({"", {}, ""}, projectRoot, sdkRoot,
                                         &resolved, &error));
    REQUIRE(error.find("missing an executable") != std::string::npos);
  }
}

TEST_CASE("ResolveLauncherCommand normalizes relative executable against working directory", "[launcher][manifest]") {
  const std::filesystem::path projectRoot =
      MakeTempRoot("resolve_command_paths_project");
  const std::filesystem::path sdkRoot =
      MakeTempRoot("resolve_command_paths_sdk");

  ResolvedLauncherCommand resolved;
  std::string error;
  REQUIRE(ResolveLauncherCommand(
      {"tools/run-game", {"--map", "${projectDir}/maps/demo.json"}, "build"},
      projectRoot, sdkRoot, &resolved, &error));

  const std::filesystem::path expectedWorkingDir =
      (projectRoot / "build").lexically_normal();
  REQUIRE(resolved.workingDirectory == expectedWorkingDir);
  REQUIRE(resolved.executable ==
          (expectedWorkingDir / "tools/run-game").lexically_normal());
  REQUIRE(resolved.args.size() == 2);
  REQUIRE(resolved.args[1] ==
          (projectRoot / "maps" / "demo.json").generic_string());
  REQUIRE_FALSE(resolved.debugString.empty());
}

TEST_CASE("Launcher project template creates manifest, source, and scene scaffold", "[launcher][template]") {
  const std::filesystem::path projectRoot = MakeTempRoot("template_project");
  std::filesystem::remove_all(projectRoot);

  LauncherProjectDocument doc;
  std::string error;
  REQUIRE(CreateLauncherProjectTemplate(
      {projectRoot, "TemplateGame", MakeTempRoot("sdk")}, &doc, &error));

  REQUIRE(IsLauncherProjectRoot(projectRoot));
  REQUIRE(std::filesystem::exists(projectRoot / "src" / "main.cpp"));
  REQUIRE(std::filesystem::exists(projectRoot / "CMakeLists.txt"));
  REQUIRE(std::filesystem::exists(projectRoot / "assets" / "scenes" /
                                  "level.json"));
  REQUIRE(doc.manifest.projectName == "TemplateGame");
  REQUIRE(doc.manifest.defaultScene == "assets/scenes/level.json");

  const std::string mainCpp = ReadTextFile(projectRoot / "src" / "main.cpp");
  REQUIRE(mainCpp.find("m_camera.target = camera.position + forward;") !=
          std::string::npos);
  REQUIRE(mainCpp.find("m_camera.fovY = camera.fovY;") != std::string::npos);
  REQUIRE(mainCpp.find("m_camera.yaw") == std::string::npos);

  const std::string cmakeLists = ReadTextFile(projectRoot / "CMakeLists.txt");
  REQUIRE(cmakeLists.find("OUTPUT_NAME \"TemplateGame\"") != std::string::npos);
  REQUIRE(cmakeLists.find(
              "RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/bin") !=
          std::string::npos);

  const std::string fragmentShader =
      ReadTextFile(projectRoot / "shaders" / "basic.frag");
  REQUIRE(fragmentShader.find("u_hasTexture") != std::string::npos);
  REQUIRE(fragmentShader.find("u_hasAlbedoMap") == std::string::npos);
}

TEST_CASE("External process runner starts, completes, and records exit status", "[launcher][process]") {
  ExternalProcessRunner runner;
  ResolvedLauncherCommand command;
  command.workingDirectory = Monolith::Tests::SecureTempBase();
#ifdef _WIN32
  command.executable = "cmd";
  command.args = {"/c", "echo horo-launcher"};
#else
  command.executable = "sh";
  command.args = {"-c", "printf horo-launcher\\n"};
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

TEST_CASE("External process runner stop marks user-terminated commands as finished", "[launcher][process]") {
  ExternalProcessRunner runner;
  ResolvedLauncherCommand command;
  command.workingDirectory = Monolith::Tests::SecureTempBase();
#ifdef _WIN32
  command.executable = "cmd";
  command.args = {"/c", "ping -n 6 127.0.0.1 >nul"};
#else
  command.executable = "sh";
  command.args = {"-c", "sleep 5"};
#endif
  command.debugString = command.executable.generic_string();

  std::string error;
  REQUIRE(runner.Start(command, "test-stop-process", &error));
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  runner.Stop();

  REQUIRE_FALSE(runner.IsActive());
  REQUIRE(runner.GetStatus().finished);
  REQUIRE(runner.GetStatus().terminatedByUser);
}

TEST_CASE("External process runner rejects Start while process is active", "[launcher][process]") {
  ExternalProcessRunner runner;
  ResolvedLauncherCommand slowCommand;
  slowCommand.workingDirectory = Monolith::Tests::SecureTempBase();
#ifdef _WIN32
  slowCommand.executable = "cmd";
  slowCommand.args = {"/c", "ping -n 6 127.0.0.1 >nul"};
#else
  slowCommand.executable = "sh";
  slowCommand.args = {"-c", "sleep 2"};
#endif
  slowCommand.debugString = slowCommand.executable.generic_string();

  std::string error;
  REQUIRE(runner.Start(slowCommand, "slow-process-active", &error));
  REQUIRE(runner.IsActive());

  ResolvedLauncherCommand secondCommand = slowCommand;
  secondCommand.debugString = "second command";
  error.clear();
  REQUIRE_FALSE(runner.Start(secondCommand, "second-start-attempt", &error));
  REQUIRE(error.find("already running") != std::string::npos);

  runner.Stop();
  REQUIRE_FALSE(runner.IsActive());
}

TEST_CASE("External process runner reports non-zero exit for invalid command", "[launcher][process]") {
  ExternalProcessRunner runner;
  ResolvedLauncherCommand command;
  command.workingDirectory = Monolith::Tests::SecureTempBase();
#ifdef _WIN32
  command.executable = "definitely_missing_executable_test.exe";
  command.args = {};
#else
  command.executable = "definitely_missing_executable_test";
  command.args = {};
#endif
  command.debugString = command.executable.generic_string();

  std::string error;
  REQUIRE(runner.Start(command, "invalid-process-name", &error));

  for (int i = 0; i < 120 && runner.IsActive(); ++i) {
    runner.Poll();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  REQUIRE_FALSE(runner.IsActive());
  REQUIRE(runner.GetStatus().finished);
  REQUIRE_FALSE(runner.GetStatus().terminatedByUser);
  REQUIRE(runner.GetStatus().exitCode != 0);
}
