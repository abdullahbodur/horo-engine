#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace Monolith::Standalone {

struct StandaloneProjectCommand {
  std::string executable;
  std::vector<std::string> args;
  std::string workingDirectory;

  bool Empty() const { return executable.empty(); }
};

struct StandaloneProjectManifest {
  int schemaVersion = 1;
  std::string projectId = "project";
  std::string projectName = "Project";
  std::string defaultScene = "assets/scenes/level.json";
  StandaloneProjectCommand configureCommand;
  StandaloneProjectCommand buildCommand;
  StandaloneProjectCommand runCommand;
};

struct StandaloneProjectDocument {
  StandaloneProjectManifest manifest;
  nlohmann::json rootJson = nlohmann::json::object();
  bool loadedFromDisk = false;
  bool parseError = false;
  std::string error;
};

struct ResolvedStandaloneCommand {
  std::filesystem::path executable;
  std::vector<std::string> args;
  std::filesystem::path workingDirectory;
  std::string debugString;
};

std::filesystem::path ResolveProjectManifestPath(const std::filesystem::path& projectRoot);
bool IsStandaloneProjectRoot(const std::filesystem::path& projectRoot);
StandaloneProjectDocument LoadProjectManifestDocument(const std::filesystem::path& projectRoot);
bool SaveProjectManifestDocument(const std::filesystem::path& projectRoot,
                                 StandaloneProjectDocument* doc,
                                 std::string* outError);
std::string SanitizeProjectId(const std::string& projectName);
bool ResolveStandaloneCommand(const StandaloneProjectCommand& command,
                              const std::filesystem::path& projectRoot,
                              const std::filesystem::path& sdkRoot,
                              ResolvedStandaloneCommand* outCommand,
                              std::string* outError);

}  // namespace Monolith::Standalone
