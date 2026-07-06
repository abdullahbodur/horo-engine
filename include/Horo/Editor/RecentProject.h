#pragma once

#include <string>

namespace Horo::Editor {

/**
 * @file RecentProject.h
 * @brief Recent project value types used by HoroEditor startup screens.
 */

/**
 * @brief Recent project entry shown by Welcome and Project Browser screens.
 */
struct RecentProjectEntry {
    std::string name;
    std::string rootPath;
    std::string lastOpenedLabel;
    std::string thumbnailKey;
};

/**
 * @brief Returns true when an entry has the minimum data needed for display and navigation.
 * @param entry Entry to validate.
 * @return True when name and root path are non-empty.
 */
[[nodiscard]] bool IsDisplayableRecentProject(const RecentProjectEntry& entry) noexcept;

} // namespace Horo::Editor
