#pragma once

#include <filesystem>
#include <string>

#include <nlohmann/json.hpp>

namespace Horo::Editor {
    struct EditorWorkspaceState {
        bool consoleShowInfo = true;
        bool consoleShowWarn = true;
        bool consoleShowError = true;
        std::string projectBrowserCwd;
    };

    struct EditorWorkspaceDocument {
        EditorWorkspaceState state;
        nlohmann::json rootJson = nlohmann::json::object();
        bool loadedFromDisk = false;
        bool parseError = false;
        std::string error;
    };

    std::filesystem::path ResolveEditorLayoutPath();

    std::filesystem::path ResolveEditorWorkspacePath();

    EditorWorkspaceDocument LoadEditorWorkspaceDocument();

    bool SaveEditorWorkspaceDocument(EditorWorkspaceDocument *doc,
                                     std::string *outError);
} // namespace Horo::Editor
