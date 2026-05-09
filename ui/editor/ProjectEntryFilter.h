/** @file ProjectEntryFilter.h
 *  @brief Pure helpers for filtering project browser directory and file entries.
 */
#pragma once

#include <string>
#include <string_view>
#include <unordered_set>

#include "core/StringHash.h"

namespace Horo::Editor {
    // Pure helpers for Project browser listing (unit-testable).
    /** @brief Returns true when a filename starts with a dot, indicating a hidden entry.
     *  @param filenameUtf8 UTF-8 filename (basename only, not a full path).
     *  @return True if the filename begins with '.'.
     */
    bool IsHiddenDotEntry(std::string_view filenameUtf8);

    // Returns true if this directory/file name should be hidden (exact name match,
    // ASCII).
    /** @brief Returns true when a directory or file name matches the built-in or extra blocklist.
     *  @param dirname      Directory or file name to test (exact ASCII match).
     *  @param extraBlocklist Optional caller-supplied set of additional names to block.
     *  @return True if the name should be hidden from the project browser.
     */
    bool IsBlockedProjectDirName(
        std::string_view dirname,
        const std::unordered_set<std::string, StringHash, std::equal_to<> >
        *extraBlocklist = nullptr);
} // namespace Horo::Editor
