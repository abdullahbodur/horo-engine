#pragma once

#include <string>
#include <vector>

namespace Horo::Editor
{

/**
 * @file RecentProject.h
 * @brief Recent project value types used by HoroEditor startup screens.
 */

/**
 * @brief Recent project entry shown by Welcome and Project Browser screens.
 */
struct RecentProjectEntry
{
    std::string name;
    std::string rootPath;
    std::string lastOpenedLabel;
    std::string thumbnailKey;
};

/**
 * @brief Returns true when an entry has the minimum data needed for display and navigation.
 * @param entry Entry to validate.
 * @return True when the name is non-empty and the root path is absolute.
 */
[[nodiscard]] bool IsDisplayableRecentProject(const RecentProjectEntry &entry) noexcept;

/**
 * @brief Loads the recent project entries from user local storage (~/.horo/recent_projects.json).
 * @return Vector of valid recent project entries.
 */
[[nodiscard]] std::vector<RecentProjectEntry> LoadRecentProjectsFromDisk();

/**
 * @brief Saves the given recent projects vector to user local storage (~/.horo/recent_projects.json).
 * @param projects List of recent project entries to persist.
 * @return True on success, false if writing fails.
 */
bool SaveRecentProjectsToDisk(const std::vector<RecentProjectEntry> &projects);

} // namespace Horo::Editor
