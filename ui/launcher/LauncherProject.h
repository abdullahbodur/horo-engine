/** @file LauncherProject.h
 *  @brief Project manifest types, loading helpers, and command resolution for the launcher. */
#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

namespace Horo::Launcher {

/** @brief An executable command entry as stored in the project manifest. */
struct LauncherProjectCommand {
    std::string executable;                 /**< Path or name of the executable to run. */
    std::vector<std::string> args;          /**< Ordered list of command-line arguments. */
    std::string workingDirectory;           /**< Working directory for the spawned process. */

    /** @brief Returns true when no executable has been configured.
     *  @return True if the command is unset. */
    bool Empty() const { return executable.empty(); }
};

/** @brief Parsed contents of the project manifest file (horo-project.json). */
struct LauncherProjectManifest {
    int schemaVersion = 1;                                     /**< Manifest format version. */
    std::string projectId = "project";                         /**< Unique machine-readable identifier for the project. */
    std::string projectName = "Project";                       /**< Human-readable display name. */
    std::string defaultScene = "assets/scenes/level.json";     /**< Relative path to the scene opened on launch. */
    LauncherProjectCommand configureCommand;                    /**< Command run to configure the project build system. */
    LauncherProjectCommand buildCommand;                        /**< Command run to compile the project. */
    LauncherProjectCommand runCommand;                          /**< Command run to execute the built project. */
};

/** @brief Wraps a parsed project manifest together with its raw JSON and load status. */
struct LauncherProjectDocument {
    LauncherProjectManifest manifest;                          /**< Parsed manifest data. */
    nlohmann::json rootJson = nlohmann::json::object();        /**< Raw JSON preserved for round-trip writes. */
    bool loadedFromDisk = false;                               /**< True when the manifest was read from a file. */
    bool parseError = false;                                   /**< True when JSON parsing failed. */
    std::string error;                                         /**< Human-readable description of any load error. */
};

/** @brief A fully resolved command ready to be passed to the OS process API. */
struct ResolvedLauncherCommand {
    std::filesystem::path executable;          /**< Absolute path to the executable. */
    std::vector<std::string> args;             /**< Final argument list after template substitution. */
    std::filesystem::path workingDirectory;    /**< Absolute working directory for the process. */
    std::string debugString;                   /**< Human-readable representation of the full command line. */
};

/** @brief Returns the absolute path to the project manifest file inside projectRoot.
 *  @param projectRoot Root directory of the project.
 *  @return Absolute path to the manifest file. */
std::filesystem::path
ResolveProjectManifestPath(const std::filesystem::path &projectRoot);

/** @brief Checks whether the given directory is a valid launcher project root.
 *  @param projectRoot Directory to test.
 *  @return True if a project manifest exists at the expected location. */
bool IsLauncherProjectRoot(const std::filesystem::path &projectRoot);

/** @brief Reads and parses the project manifest for the given project root.
 *  @param projectRoot Root directory of the project.
 *  @return A document containing the parsed manifest and any error state. */
LauncherProjectDocument
LoadProjectManifestDocument(const std::filesystem::path &projectRoot);

/** @brief Serializes and writes the project manifest to disk.
 *  @param projectRoot Root directory of the project.
 *  @param doc         The document to serialize; must not be nullptr.
 *  @param outError    Receives a human-readable error message on failure.
 *  @return True if the manifest was written successfully. */
bool SaveProjectManifestDocument(const std::filesystem::path &projectRoot,
                                 LauncherProjectDocument *doc,
                                 std::string *outError);

/** @brief Produces a filesystem-safe project identifier derived from a display name.
 *  @param projectName The human-readable project name to sanitize.
 *  @return A lowercase, alphanumeric-only identifier string. */
std::string SanitizeProjectId(std::string_view projectName);

/** @brief Resolves a manifest command entry into an absolute, runnable command.
 *  @param command     The raw command as stored in the manifest.
 *  @param projectRoot Absolute project root used to expand relative paths.
 *  @param sdkRoot     Absolute SDK root used for SDK-relative path tokens.
 *  @param outCommand  Receives the resolved command on success; must not be nullptr.
 *  @param outError    Receives a human-readable error message on failure.
 *  @return True if the command was resolved successfully. */
bool ResolveLauncherCommand(const LauncherProjectCommand &command,
                            const std::filesystem::path &projectRoot,
                            const std::filesystem::path &sdkRoot,
                            ResolvedLauncherCommand *outCommand,
                            std::string *outError);

} // namespace Horo::Launcher
