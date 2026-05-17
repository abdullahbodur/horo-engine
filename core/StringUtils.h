/** @file StringUtils.h
 *  @brief General-purpose string utilities used across the engine.
 *
 *  These are free functions with no engine dependencies.  Keep them small,
 *  explicit, and ASCII-safe.
 */
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace Horo {

/** @brief Returns a lowercase copy of the input using ASCII semantics.
 *
 *  Does not depend on locale; only maps 'A'–'Z' to 'a'–'z'.
 *  Safe for file paths, asset IDs, and protocol keys.
 *  @param str Input string.
 *  @return Lowercased copy.
 */
std::string ToLowerAscii(std::string_view str);

/** @brief Formats a byte count into a human-readable string.
 *
 *  Examples: "42 B", "1.5 KB", "3.2 MB", "1.0 GB".
 *  @param bytes File or buffer size in bytes.
 *  @return Formatted string.
 */
std::string FormatFileSize(uint64_t bytes);

} // namespace Horo
