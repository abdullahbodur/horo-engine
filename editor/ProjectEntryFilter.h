#pragma once

#include <string>
#include <string_view>
#include <unordered_set>

#include "core/StringHash.h"

namespace Monolith::Editor {
// Pure helpers for Project browser listing (unit-testable).
bool IsHiddenDotEntry(std::string_view filenameUtf8);

// Returns true if this directory/file name should be hidden (exact name match,
// ASCII).
bool IsBlockedProjectDirName(
    std::string_view dirname,
    const std::unordered_set<std::string, StringHash, std::equal_to<>>
        *extraBlocklist = nullptr);
} // namespace Monolith::Editor
