#pragma once

#include <string>

namespace Monolith {
namespace Editor {

bool IsObjFilePath(const std::string& path);
std::string AssetIdFromImportedPath(const std::string& path);
std::string MeshTagFromImportedPath(const std::string& path);

}  // namespace Editor
}  // namespace Monolith
