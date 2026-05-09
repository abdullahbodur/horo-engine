/** @file EditorWorkspaceSettings.h
 *  @brief Persistent editor workspace state (console filters, project-browser path) and its JSON I/O helpers. */
#pragma once

#include <filesystem>
#include <string>

#include <nlohmann/json.hpp>

namespace Horo::Editor {
    /** @brief Runtime-editable state that is serialised to the workspace settings file on disk. */
    struct EditorWorkspaceState {
        bool consoleShowInfo = true;    /**< Show informational log entries in the console panel. */
        bool consoleShowWarn = true;    /**< Show warning log entries in the console panel. */
        bool consoleShowError = true;   /**< Show error log entries in the console panel. */
        std::string projectBrowserCwd;  /**< Last directory visited in the project browser. */
    };

    /** @brief In-memory representation of the workspace settings JSON document.
     *  @note The rootJson field preserves all keys present in the file so unknown
     *        fields survive a save round-trip. */
    struct EditorWorkspaceDocument {
        EditorWorkspaceState state;                         /**< Deserialised workspace state. */
        nlohmann::json rootJson = nlohmann::json::object(); /**< Full JSON root for round-trip preservation of unrecognised keys. */
        bool loadedFromDisk = false;                        /**< True when the document was successfully read from disk. */
        bool parseError = false;                            /**< True when the JSON parse failed. */
        std::string error;                                  /**< Human-readable error description on failure. */
    };

    /** @brief Returns the absolute path to the ImGui dock-layout (.ini) file for the editor. */
    std::filesystem::path ResolveEditorLayoutPath();

    /** @brief Returns the absolute path to the editor workspace settings JSON file. */
    std::filesystem::path ResolveEditorWorkspacePath();

    /** @brief Loads the editor workspace document from disk.
     *  @return A populated EditorWorkspaceDocument; on failure the error fields are set and
     *          loadedFromDisk is false. */
    EditorWorkspaceDocument LoadEditorWorkspaceDocument();

    /** @brief Serialises @p doc to the workspace settings file on disk.
     *  @param doc      The workspace document to save.
     *  @param outError Receives a human-readable message on failure; may be nullptr.
     *  @return True on success. */
    bool SaveEditorWorkspaceDocument(EditorWorkspaceDocument *doc,
                                     std::string *outError);
} // namespace Horo::Editor
