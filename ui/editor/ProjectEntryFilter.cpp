/**
 * @file ProjectEntryFilter.cpp
 * @brief Project-browser filtering for dotfiles and generated/build directory names.
 */
#include "ui/editor/ProjectEntryFilter.h"

namespace Horo::Editor {
    /** @copydoc IsHiddenDotEntry */
    bool IsHiddenDotEntry(std::string_view filenameUtf8) {
        return !filenameUtf8.empty() && filenameUtf8.front() == '.';
    }

    /** @copydoc IsBlockedProjectDirName */
    bool IsBlockedProjectDirName(
        std::string_view dirname,
        const std::unordered_set<std::string, StringHash, std::equal_to<> >
        *extraBlocklist) {
        if (static const std::unordered_set<std::string, StringHash, std::equal_to<> >
            kDefaultBlock{
                "build", "engine", "out", "bin", "obj", ".vs",
                "cmake-build-debug", "cmake-build-release"
            };
            kDefaultBlock.contains(dirname))
            return true;
        if (extraBlocklist && extraBlocklist->contains(dirname))
            return true;
        // Prefix cmake-build-
        if (dirname.size() > 13 && dirname.compare(0, 13, "cmake-build-") == 0)
            return true;
        return false;
    }
} // namespace Horo::Editor
