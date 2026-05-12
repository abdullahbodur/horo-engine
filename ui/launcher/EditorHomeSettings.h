/** @file EditorHomeSettings.h
 *  @brief Persistence layer for the launcher home screen state, including the recent projects list. */
#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace Horo::Launcher {

/** @brief Transient home screen state that is persisted across launcher sessions. */
struct EditorHomeState {
    std::vector<std::string> recentProjects; /**< Ordered list of recently opened project root paths. */
    std::string lastProjectPath;             /**< Path of the most recently opened project. */
};

/** @brief Wraps the parsed home state together with its raw JSON and load status. */
struct EditorHomeDocument {
    EditorHomeState state;                              /**< Parsed home state. */
    nlohmann::json rootJson = nlohmann::json::object(); /**< Raw JSON preserved for round-trip writes. */
    bool loadedFromDisk = false;                        /**< True when the document was read from disk. */
    bool parseError = false;                            /**< True when JSON parsing failed. */
    std::string error;                                  /**< Human-readable description of any load error. */
};

/** @brief Returns the absolute path to the editor home settings file.
 *  @return Absolute path to the home document on the current platform. */
std::filesystem::path ResolveEditorHomePath();

/** @brief Reads and parses the editor home document from its platform-specific location.
 *  @return A document containing the parsed state and any error information. */
EditorHomeDocument LoadEditorHomeDocument();

/** @brief Serializes and writes the editor home document to disk.
 *  @param doc      The document to serialize; must not be nullptr.
 *  @param outError Receives a human-readable error message on failure.
 *  @return True if the document was written successfully. */
bool SaveEditorHomeDocument(EditorHomeDocument *doc, std::string *outError);

/** @brief Adds the given project path to the front of the recent projects list, deduplicating as needed.
 *  @param doc         The home document to update; must not be nullptr.
 *  @param projectRoot Absolute path to the project root to remember. */
void RememberRecentProject(EditorHomeDocument *doc,
                           const std::filesystem::path &projectRoot);

/** @brief Removes entries from the recent projects list whose directories no longer exist on disk.
 *  @param doc The home document to prune; must not be nullptr. */
void PruneMissingRecentProjects(EditorHomeDocument *doc);

} // namespace Horo::Launcher
