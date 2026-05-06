#pragma once

#include <filesystem>
#include <string_view>

namespace Horo::Editor {
struct AssetDef;

bool IsPathWithinDirectory(const std::filesystem::path &path,
                           const std::filesystem::path &directory);
std::filesystem::path ResolveProjectAssetPath(std::string_view rawPath);
std::filesystem::path GetManagedImportedAssetDirectory(const AssetDef &asset);
} // namespace Horo::Editor
