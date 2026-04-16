#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

namespace Monolith::Launcher {

struct LauncherProjectCommand {
  std::string executable;
  std::vector<std::string> args;
  std::string workingDirectory;

  bool Empty() const { return executable.empty(); }
};

struct LauncherProjectManifest {
  int schemaVersion = 1;
  std::string projectId = "project";
  std::string projectName = "Project";
  std::string defaultScene = "assets/scenes/level.json";
  LauncherProjectCommand configureCommand;
  LauncherProjectCommand buildCommand;
  LauncherProjectCommand runCommand;
};

struct LauncherProjectDocument {
  LauncherProjectManifest manifest;
  nlohmann::json rootJson = nlohmann::json::object();
  bool loadedFromDisk = false;
  bool parseError = false;
  std::string error;
};

struct ResolvedLauncherCommand {
  std::filesystem::path executable;
  std::vector<std::string> args;
  std::filesystem::path workingDirectory;
  std::string debugString;
};

std::filesystem::path ResolveProjectManifestPath(const std::filesystem::path& projectRoot);
bool IsLauncherProjectRoot(const std::filesystem::path& projectRoot);
LauncherProjectDocument LoadProjectManifestDocument(const std::filesystem::path& projectRoot);
bool SaveProjectManifestDocument(const std::filesystem::path& projectRoot,
                                 LauncherProjectDocument* doc,
                                 std::string* outError);
std::string SanitizeProjectId(std::string_view projectName);
bool ResolveLauncherCommand(const LauncherProjectCommand& command,
                              const std::filesystem::path& projectRoot,
                              const std::filesystem::path& sdkRoot,
                              ResolvedLauncherCommand* outCommand,
                              std::string* outError);

}  // namespace Monolith::Launcher
