/** @file EditorImportedAssetPathUtils.h
 *  @brief Path utilities for resolving and validating imported asset locations on disk.
 */
#pragma once

#include <filesystem>
#include <string_view>

namespace Horo::Editor {
struct AssetDef;

/** @brief Returns true when path is located inside directory (non-strict prefix check).
 *  @param path      The path to test.
 *  @param directory The directory that must contain path.
 *  @return True if path is equal to or nested under directory.
 */
bool IsPathWithinDirectory(const std::filesystem::path &path,
                           const std::filesystem::path &directory);

/** @brief Resolves a raw project-relative or absolute asset path to an absolute filesystem path.
 *  @param rawPath Project-relative or absolute path string from an asset definition.
 *  @return Absolute filesystem path, resolving relative paths against the project root.
 */
std::filesystem::path ResolveProjectAssetPath(std::string_view rawPath);

/** @brief Returns the managed import directory for the given asset.
 *  @param asset AssetDef whose kind and ID determine the import directory.
 *  @return Absolute path to the directory that holds this asset's imported files.
 */
std::filesystem::path GetManagedImportedAssetDirectory(const AssetDef &asset);
} // namespace Horo::Editor
