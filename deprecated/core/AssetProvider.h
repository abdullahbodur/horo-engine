/**
 * @file AssetProvider.h
 * @brief Runtime asset byte loading from project files or packaged release archives.
 */
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Horo {

/**
 * @brief Reads project-relative asset bytes from disk or assets.horo.
 *
 * The provider first tries @ref ProjectPath::Resolve for development builds. If
 * the file is absent, it looks for a packaged @c assets.horo archive next to the
 * executable/current working directory and reads the same logical path from it.
 *
 * @param logicalPath Project-relative asset path such as assets/scenes/level.json.
 * @param outError Receives an actionable error on failure.
 * @return Asset bytes when found.
 */
std::optional<std::vector<uint8_t>> ReadAssetBytes(std::string_view logicalPath,
                                                   std::string *outError = nullptr);

/**
 * @brief Reads project-relative asset text from disk or assets.horo.
 * @param logicalPath Project-relative asset path.
 * @param outError Receives an actionable error on failure.
 * @return UTF-8 text when found.
 */
std::optional<std::string> ReadAssetText(std::string_view logicalPath,
                                         std::string *outError = nullptr);

} // namespace Horo
