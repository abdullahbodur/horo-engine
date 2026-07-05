/** @file ReleaseHistory.h
 *  @brief Build history persistence: JSON read/write for completed build
 *         sessions.
 *
 *  Extraction from ReleasePipeline.h — HORO-32 P7.1.
 */
#pragma once

#include "core/pipeline/ReleaseTypes.h"

#include <filesystem>
#include <vector>

namespace Horo::Build {

/** @brief Returns the path to the persisted build history JSON file.
 *
 *  Platform-specific base directory:
 *  - Windows: %APPDATA%/.horo/build_history.json
 *  - POSIX:   $HOME/.horo/build_history.json
 *
 *  The .horo directory is created automatically if it does not exist.
 *  Falls back to the current working directory when the environment variable
 *  is missing or empty.
 *
 *  @return Absolute path to the history file (parent directory is guaranteed
 *          to exist after this call). */
std::filesystem::path BuildHistoryPath();

/** @brief Serialises a list of build history entries to a JSON file.
 *
 *  Overwrites the file at @p path with a compact two-space-indented JSON array.
 *  Each entry is encoded with keys: version, timestamp, allSucceeded, jobs.
 *  The file is truncated and rewritten atomically per ofstream semantics;
 *  no backup is made on failure.
 *
 *  @param path     Destination file path (parent directory must exist).
 *  @param entries  History entries to serialise; may be empty. */
void WriteHistoryJson(const std::filesystem::path &path,
                      const std::vector<BuildHistoryEntry> &entries);

/** @brief Reads build history entries from a JSON file.
 *
 *  Returns an empty vector when the file does not exist, cannot be opened,
 *  is not a JSON array, or contains invalid data (parse exceptions are
 *  swallowed).  Unknown fields are silently ignored.
 *
 *  @param path  Source file path.
 *  @return      Parsed entries in file order; empty on any failure. */
std::vector<BuildHistoryEntry> ReadHistoryJson(const std::filesystem::path &path);

} // namespace Horo::Build
