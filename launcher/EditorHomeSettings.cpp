#include "launcher/EditorHomeSettings.h"

#include <algorithm>
#include <fstream>

#include "launcher/LauncherProject.h"
#include "mcp/McpSettings.h"

namespace Monolith::Launcher {

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

constexpr size_t kMaxRecentProjects = 10;

std::string NormalizeRecentProjectPath(const fs::path& path) {
  if (path.empty())
    return {};

  std::error_code ec;
  fs::path normalized = fs::weakly_canonical(path, ec);
  if (ec) {
    fs::path absPath = fs::absolute(path, ec);
    if (!ec)
      normalized = absPath;
  }
  if (normalized.empty())
    return {};
  return normalized.lexically_normal().generic_string();
}

}  // namespace

fs::path ResolveEditorHomePath() {
  return Monolith::Mcp::ResolveMcpSettingsDirectory() / "editor_home.json";
}

EditorHomeDocument LoadEditorHomeDocument() {
  EditorHomeDocument out;
  std::ifstream in(ResolveEditorHomePath());
  if (!in.is_open())
    return out;

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
    out.error = "Editor home settings root must be an object.";
    return out;
  }

  const json homeJson = out.rootJson.value("home", json::object());
  if (!homeJson.is_object())
    return out;

  if (const json recentJson = homeJson.value("recentProjects", json::array()); recentJson.is_array()) {
    for (const json& entry : recentJson) {
      if (entry.is_string()) {
        const std::string value = NormalizeRecentProjectPath(entry.get<std::string>());
        if (!value.empty())
          out.state.recentProjects.push_back(value);
      }
    }
  }
  out.state.lastProjectPath =
      NormalizeRecentProjectPath(homeJson.value("lastProjectPath", out.state.lastProjectPath));
  return out;
}

bool SaveEditorHomeDocument(EditorHomeDocument* doc, std::string* outError) {
  if (outError)
    outError->clear();
  if (!doc) {
    if (outError)
      *outError = "Editor home document is null.";
    return false;
  }

  PruneMissingRecentProjects(doc);

  json root = doc->rootJson.is_object() ? doc->rootJson : json::object();
  json homeJson = root.value("home", json::object());
  if (!homeJson.is_object())
    homeJson = json::object();

  homeJson["recentProjects"] = doc->state.recentProjects;
  if (!doc->state.lastProjectPath.empty())
    homeJson["lastProjectPath"] = doc->state.lastProjectPath;
  else
    homeJson.erase("lastProjectPath");
  root["home"] = std::move(homeJson);

  std::error_code ec;
  fs::create_directories(ResolveEditorHomePath().parent_path(), ec);
  if (ec) {
    if (outError)
      *outError = ec.message();
    return false;
  }

  std::ofstream outFile(ResolveEditorHomePath());
  if (!outFile.is_open()) {
    if (outError)
      *outError = "Failed to open editor home settings file for writing.";
    return false;
  }

  try {
    outFile << root.dump(2);
  } catch (const json::exception& e) {
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

void RememberRecentProject(EditorHomeDocument* doc, const fs::path& projectRoot) {
  if (!doc)
    return;

  const std::string normalized = NormalizeRecentProjectPath(projectRoot);
  if (normalized.empty())
    return;

  auto& recent = doc->state.recentProjects;
  std::erase(recent, normalized);
  recent.insert(recent.begin(), normalized);
  if (recent.size() > kMaxRecentProjects)
    recent.resize(kMaxRecentProjects);
  doc->state.lastProjectPath = normalized;
}

void PruneMissingRecentProjects(EditorHomeDocument* doc) {
  if (!doc)
    return;

  auto& recent = doc->state.recentProjects;
  std::erase_if(recent, [](const std::string& entry) {
    return entry.empty() || !IsLauncherProjectRoot(entry);
  });

  if (!doc->state.lastProjectPath.empty() && !IsLauncherProjectRoot(doc->state.lastProjectPath))
    doc->state.lastProjectPath.clear();
}

}  // namespace Monolith::Launcher
