#include "standalone/StandaloneProject.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

namespace Monolith::Standalone {

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

StandaloneProjectCommand ParseCommand(const json& value) {
  StandaloneProjectCommand out;
  if (!value.is_object())
    return out;

  out.executable = value.value("executable", "");
  out.workingDirectory = value.value("workingDirectory", "");
  if (const json args = value.value("args", json::array()); args.is_array()) {
    for (const json& arg : args) {
      if (arg.is_string())
        out.args.push_back(arg.get<std::string>());
    }
  }
  return out;
}

json SerializeCommand(const StandaloneProjectCommand& command) {
  json out = json::object();
  out["executable"] = command.executable;
  out["args"] = command.args;
  out["workingDirectory"] = command.workingDirectory;
  return out;
}

std::string ReplaceAll(std::string value, const std::string& needle, const std::string& replacement) {
  if (needle.empty())
    return value;

  size_t cursor = 0;
  while ((cursor = value.find(needle, cursor)) != std::string::npos) {
    value.replace(cursor, needle.size(), replacement);
    cursor += replacement.size();
  }
  return value;
}

std::string ExpandTokens(const std::string& value,
                         const fs::path& projectRoot,
                         const fs::path& sdkRoot) {
  std::string expanded = value;
  expanded = ReplaceAll(expanded, "${projectDir}", projectRoot.generic_string());
  expanded = ReplaceAll(expanded, "${horoSdkRoot}", sdkRoot.generic_string());
  return expanded;
}

std::string BuildDebugString(const ResolvedStandaloneCommand& command) {
  std::ostringstream out;
  out << command.executable.generic_string();
  for (const std::string& arg : command.args)
    out << ' ' << arg;
  return out.str();
}

}  // namespace

fs::path ResolveProjectManifestPath(const fs::path& projectRoot) {
  return projectRoot / ".horo" / "project.json";
}

bool IsStandaloneProjectRoot(const fs::path& projectRoot) {
  std::error_code ec;
  return fs::is_regular_file(ResolveProjectManifestPath(projectRoot), ec) && !ec;
}

StandaloneProjectDocument LoadProjectManifestDocument(const fs::path& projectRoot) {
  StandaloneProjectDocument out;
  std::ifstream in(ResolveProjectManifestPath(projectRoot));
  if (!in.is_open()) {
    out.parseError = true;
    out.error = "Standalone project manifest was not found.";
    return out;
  }

  out.loadedFromDisk = true;
  try {
    in >> out.rootJson;
  } catch (const json::exception& e) {
    out.rootJson = json::object();
    out.parseError = true;
    out.error = e.what();
    return out;
  }

  if (!out.rootJson.is_object()) {
    out.rootJson = json::object();
    out.parseError = true;
    out.error = "Standalone project manifest root must be an object.";
    return out;
  }

  const json projectJson = out.rootJson.value("project", json::object());
  if (!projectJson.is_object()) {
    out.parseError = true;
    out.error = "Standalone project manifest missing 'project' object.";
    return out;
  }

  out.manifest.schemaVersion = projectJson.value("schemaVersion", out.manifest.schemaVersion);
  out.manifest.projectId = projectJson.value("projectId", out.manifest.projectId);
  out.manifest.projectName = projectJson.value("projectName", out.manifest.projectName);
  out.manifest.defaultScene = projectJson.value("defaultScene", out.manifest.defaultScene);

  const json commandsJson = projectJson.value("commands", json::object());
  if (commandsJson.is_object()) {
    out.manifest.configureCommand = ParseCommand(commandsJson.value("configure", json::object()));
    out.manifest.buildCommand = ParseCommand(commandsJson.value("build", json::object()));
    out.manifest.runCommand = ParseCommand(commandsJson.value("run", json::object()));
  }

  if (out.manifest.projectId.empty() || out.manifest.projectName.empty() ||
      out.manifest.defaultScene.empty() || out.manifest.schemaVersion < 1) {
    out.parseError = true;
    out.error = "Standalone project manifest is missing required fields.";
  }

  return out;
}

bool SaveProjectManifestDocument(const fs::path& projectRoot,
                                 StandaloneProjectDocument* doc,
                                 std::string* outError) {
  if (outError)
    outError->clear();
  if (!doc) {
    if (outError)
      *outError = "Standalone project document is null.";
    return false;
  }

  json root = doc->rootJson.is_object() ? doc->rootJson : json::object();
  json projectJson = root.value("project", json::object());
  if (!projectJson.is_object())
    projectJson = json::object();

  projectJson["schemaVersion"] = doc->manifest.schemaVersion;
  projectJson["projectId"] = doc->manifest.projectId;
  projectJson["projectName"] = doc->manifest.projectName;
  projectJson["defaultScene"] = doc->manifest.defaultScene;
  projectJson["commands"] = json{
      {"configure", SerializeCommand(doc->manifest.configureCommand)},
      {"build", SerializeCommand(doc->manifest.buildCommand)},
      {"run", SerializeCommand(doc->manifest.runCommand)},
  };
  root["project"] = std::move(projectJson);

  std::error_code ec;
  fs::create_directories(ResolveProjectManifestPath(projectRoot).parent_path(), ec);
  if (ec) {
    if (outError)
      *outError = ec.message();
    return false;
  }

  std::ofstream outFile(ResolveProjectManifestPath(projectRoot));
  if (!outFile.is_open()) {
    if (outError)
      *outError = "Failed to open standalone project manifest for writing.";
    return false;
  }

  try {
    outFile << root.dump(2);
  } catch (const std::exception& e) {
    if (outError)
      *outError = e.what();
    return false;
  }

  doc->rootJson = std::move(root);
  doc->loadedFromDisk = true;
  doc->parseError = false;
  doc->error.clear();
  return true;
}

std::string SanitizeProjectId(const std::string& projectName) {
  std::string out;
  out.reserve(projectName.size());
  for (char c : projectName) {
    if (std::isalnum(static_cast<unsigned char>(c)))
      out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    else if ((c == ' ' || c == '-' || c == '_') && (out.empty() || out.back() != '_'))
      out.push_back('_');
  }
  while (!out.empty() && out.back() == '_')
    out.pop_back();
  if (out.empty())
    out = "project";
  return out;
}

bool ResolveStandaloneCommand(const StandaloneProjectCommand& command,
                              const fs::path& projectRoot,
                              const fs::path& sdkRoot,
                              ResolvedStandaloneCommand* outCommand,
                              std::string* outError) {
  if (outError)
    outError->clear();
  if (!outCommand) {
    if (outError)
      *outError = "Resolved command output is null.";
    return false;
  }
  if (command.executable.empty()) {
    if (outError)
      *outError = "Standalone project command is missing an executable.";
    return false;
  }

  ResolvedStandaloneCommand resolved;
  const std::string workingDirRaw = command.workingDirectory.empty()
                                        ? projectRoot.generic_string()
                                        : ExpandTokens(command.workingDirectory, projectRoot, sdkRoot);
  resolved.workingDirectory = fs::path(workingDirRaw);
  if (resolved.workingDirectory.is_relative())
    resolved.workingDirectory = projectRoot / resolved.workingDirectory;
  resolved.workingDirectory = resolved.workingDirectory.lexically_normal();

  const std::string executableRaw = ExpandTokens(command.executable, projectRoot, sdkRoot);
  fs::path executablePath = executableRaw;
  if (!executablePath.empty() && executablePath.has_parent_path() && executablePath.is_relative())
    executablePath = resolved.workingDirectory / executablePath;
  resolved.executable = executablePath.empty() ? fs::path(executableRaw) : executablePath.lexically_normal();

  resolved.args.reserve(command.args.size());
  for (const std::string& arg : command.args)
    resolved.args.push_back(ExpandTokens(arg, projectRoot, sdkRoot));

  resolved.debugString = BuildDebugString(resolved);
  *outCommand = std::move(resolved);
  return true;
}

}  // namespace Monolith::Standalone
