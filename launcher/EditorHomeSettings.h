#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace Monolith::Launcher {
struct EditorHomeState {
  std::vector<std::string> recentProjects;
  std::string lastProjectPath;
};

struct EditorHomeDocument {
  EditorHomeState state;
  nlohmann::json rootJson = nlohmann::json::object();
  bool loadedFromDisk = false;
  bool parseError = false;
  std::string error;
};

std::filesystem::path ResolveEditorHomePath();

EditorHomeDocument LoadEditorHomeDocument();

bool SaveEditorHomeDocument(EditorHomeDocument *doc, std::string *outError);

void RememberRecentProject(EditorHomeDocument *doc,
                           const std::filesystem::path &projectRoot);

void PruneMissingRecentProjects(EditorHomeDocument *doc);
} // namespace Monolith::Launcher
