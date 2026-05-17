/** @file StringUtils.cpp
 *  @brief Implements general-purpose string utilities. See StringUtils.h. */
#include "core/StringUtils.h"

#include <algorithm>
#include <cctype>
#include <format>

namespace Horo {

/** @copydoc ToLowerAscii */
std::string ToLowerAscii(std::string_view str) {
    std::string result(str);
    for (auto& c : result)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return result;
}

/** @copydoc FormatFileSize */
std::string FormatFileSize(uint64_t bytes) {
    if (bytes < 1024) return std::format("{} B", bytes);
    if (bytes < 1024 * 1024) return std::format("{:.1f} KB", bytes / 1024.0);
    if (bytes < 1024 * 1024 * 1024) return std::format("{:.1f} MB", bytes / (1024.0 * 1024.0));
    return std::format("{:.1f} GB", bytes / (1024.0 * 1024.0 * 1024.0));
}

} // namespace Horo
