#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

#include "core/EngineLaunchArgs.h"
#include "core/ProjectPath.h"
#include "editor/EditorWorkspaceSettings.h"
#include "launcher/EditorHomeSettings.h"
#include "launcher/ExternalProcessRunner.h"
#include "launcher/LauncherProject.h"
#include "launcher/LauncherProjectTemplate.h"

using namespace Monolith;
using namespace Monolith::Editor;
using namespace Monolith::Launcher;

namespace {

std::filesystem::path MakeTempRoot(const std::string& name) {
  return std::filesystem::temp_directory_path() / "horo_launcher_tests" / name;
}

std::string NormalizeComparablePath(const std::filesystem::path& path) {
  std::error_code ec;
  std::filesystem::path normalized = std::filesystem::weakly_canonical(path, ec);
  if (ec)
    normalized = std::filesystem::absolute(path, ec);
  if (ec)
    normalized = path;
  return normalized.lexically_normal().generic_string();
}

std::string ReadTextFile(const std::filesystem::path& path) {
  std::ifstream in(path);
  REQUIRE(in.is_open());
  return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
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

TEST_CASE("EngineLaunchArgs parses launcher project path", "[launcher][cli]") {
  char arg0[] = "HoroEditor";
  char projectFlag[] = "--project";
  char projectPath[] = "C:/games/demo";
  char* argv[] = {arg0, projectFlag, projectPath};

  const EngineLaunchOptions options = ParseEngineLaunchOptions(3, argv);

  REQUIRE(options.editorStartup == EditorStartupCli::Default);
  REQUIRE(options.projectPath == std::filesystem::path(projectPath));
}

TEST_CASE("ProjectPath explicit root switches editor workspace to project-local settings",
          "[launcher][workspace]") {
  const std::filesystem::path projectRoot = MakeTempRoot("project_workspace");
  std::filesystem::remove_all(projectRoot);
  std::filesystem::create_directories(projectRoot);

  ProjectPath::SetProjectRoot(projectRoot);
  REQUIRE(ProjectPath::HasExplicitProjectRoot());
  REQUIRE(ResolveEditorWorkspacePath() ==
          std::filesystem::path(NormalizeComparablePath(projectRoot / ".horo" / "editor_workspace.json")));

  ProjectPath::SetProjectRoot({});
  REQUIRE_FALSE(ProjectPath::HasExplicitProjectRoot());
}

TEST_CASE("Editor home settings remember and prune recent projects", "[launcher][home]") {
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
  REQUIRE(doc.state.recentProjects.front() == NormalizeComparablePath(projectA));
  REQUIRE(doc.state.lastProjectPath == NormalizeComparablePath(projectA));

  std::filesystem::remove(projectB / ".horo" / "project.json");
  PruneMissingRecentProjects(&doc);
  REQUIRE(doc.state.recentProjects.size() == 1);

  std::string saveError;
  REQUIRE(SaveEditorHomeDocument(&doc, &saveError));
  const EditorHomeDocument loaded = LoadEditorHomeDocument();
  REQUIRE_FALSE(loaded.parseError);
  REQUIRE(loaded.state.recentProjects.size() == 1);
  REQUIRE(loaded.state.recentProjects.front() == NormalizeComparablePath(projectA));
}

TEST_CASE("Launcher project manifest resolves SDK and project tokens", "[launcher][manifest]") {
  const std::filesystem::path projectRoot = MakeTempRoot("manifest_project");
  const std::filesystem::path sdkRoot = MakeTempRoot("manifest_sdk");

  ResolvedLauncherCommand resolved;
  std::string error;
  REQUIRE(ResolveLauncherCommand({"cmake",
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

TEST_CASE("Launcher project template creates manifest, source, and scene scaffold",
          "[launcher][template]") {
  const std::filesystem::path projectRoot = MakeTempRoot("template_project");
  std::filesystem::remove_all(projectRoot);

  LauncherProjectDocument doc;
  std::string error;
  REQUIRE(CreateLauncherProjectTemplate({projectRoot, "TemplateGame", MakeTempRoot("sdk")},
                                          &doc,
                                          &error));

  REQUIRE(IsLauncherProjectRoot(projectRoot));
  REQUIRE(std::filesystem::exists(projectRoot / "src" / "main.cpp"));
  REQUIRE(std::filesystem::exists(projectRoot / "CMakeLists.txt"));
  REQUIRE(std::filesystem::exists(projectRoot / "assets" / "scenes" / "level.json"));
  REQUIRE(doc.manifest.projectName == "TemplateGame");
  REQUIRE(doc.manifest.defaultScene == "assets/scenes/level.json");

  const std::string mainCpp = ReadTextFile(projectRoot / "src" / "main.cpp");
  REQUIRE(mainCpp.find("m_camera.target = camera.position + forward;") != std::string::npos);
  REQUIRE(mainCpp.find("m_camera.fovY = camera.fovY;") != std::string::npos);
  REQUIRE(mainCpp.find("m_camera.yaw") == std::string::npos);

  const std::string cmakeLists = ReadTextFile(projectRoot / "CMakeLists.txt");
  REQUIRE(cmakeLists.find("OUTPUT_NAME \"TemplateGame\"") != std::string::npos);
  REQUIRE(cmakeLists.find("RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/bin") !=
          std::string::npos);

  const std::string fragmentShader = ReadTextFile(projectRoot / "shaders" / "basic.frag");
  REQUIRE(fragmentShader.find("u_hasTexture") != std::string::npos);
  REQUIRE(fragmentShader.find("u_hasAlbedoMap") == std::string::npos);
}

TEST_CASE("External process runner starts, completes, and records exit status",
          "[launcher][process]") {
  ExternalProcessRunner runner;
  ResolvedLauncherCommand command;
  command.workingDirectory = std::filesystem::temp_directory_path();
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

TEST_CASE("External process runner stop marks user-terminated commands as finished",
          "[launcher][process]") {
  ExternalProcessRunner runner;
  ResolvedLauncherCommand command;
  command.workingDirectory = std::filesystem::temp_directory_path();
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
