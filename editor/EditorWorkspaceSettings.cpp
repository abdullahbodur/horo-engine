#include "editor/EditorWorkspaceSettings.h"

#include <fstream>

#include "core/ProjectPath.h"
#include "mcp/McpSettings.h"

namespace Monolith {
namespace Editor {

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

fs::path ResolveEditorSettingsDirectory() {
  return Mcp::ResolveMcpSettingsDirectory();
}

}  // namespace

fs::path ResolveEditorLayoutPath() {
  return ResolveEditorSettingsDirectory() / "editor_layout.ini";
}

fs::path ResolveEditorWorkspacePath() {
  if (ProjectPath::HasExplicitProjectRoot())
    return ProjectPath::Root() / ".horo" / "editor_workspace.json";
  return ResolveEditorSettingsDirectory() / "editor_workspace.json";
}

EditorWorkspaceDocument LoadEditorWorkspaceDocument() {
  EditorWorkspaceDocument out;
  std::ifstream in(ResolveEditorWorkspacePath());
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
    out.error = "Workspace settings root must be an object.";
    return out;
  }

  const json editorJson = out.rootJson.value("editor", json::object());
  if (!editorJson.is_object())
    return out;

  out.state.consoleShowInfo = editorJson.value("consoleShowInfo", out.state.consoleShowInfo);
  out.state.consoleShowWarn = editorJson.value("consoleShowWarn", out.state.consoleShowWarn);
  out.state.consoleShowError = editorJson.value("consoleShowError", out.state.consoleShowError);
  out.state.projectBrowserCwd = editorJson.value("projectBrowserCwd", out.state.projectBrowserCwd);
  return out;
}

bool SaveEditorWorkspaceDocument(EditorWorkspaceDocument* doc, std::string* outError) {
  if (outError)
    outError->clear();
  if (!doc) {
    if (outError)
      *outError = "Workspace document is null.";
    return false;
  }

  json root = doc->rootJson.is_object() ? doc->rootJson : json::object();
  json editorJson = root.value("editor", json::object());
  if (!editorJson.is_object())
    editorJson = json::object();

  editorJson["consoleShowInfo"] = doc->state.consoleShowInfo;
  editorJson["consoleShowWarn"] = doc->state.consoleShowWarn;
  editorJson["consoleShowError"] = doc->state.consoleShowError;
  editorJson["projectBrowserCwd"] = doc->state.projectBrowserCwd;
  root["editor"] = std::move(editorJson);

  std::error_code ec;
  fs::create_directories(ResolveEditorSettingsDirectory(), ec);
  if (ec) {
    if (outError)
      *outError = ec.message();
    return false;
  }

  std::ofstream outFile(ResolveEditorWorkspacePath());
  if (!outFile.is_open()) {
    if (outError)
      *outError = "Failed to open workspace settings file for writing.";
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

}  // namespace Editor
}  // namespace Monolith
