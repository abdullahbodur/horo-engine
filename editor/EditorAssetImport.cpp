#include "editor/EditorAssetImport.h"

#include <algorithm>
#include <cctype>
#include <filesystem>

namespace Monolith {
namespace Editor {

bool IsObjFilePath(const std::string& path) {
  if (path.empty())
    return false;
  const std::filesystem::path p(path);
  std::string ext = p.extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return ext == ".obj";
}

std::string AssetIdFromImportedPath(const std::string& path) {
  if (path.empty())
    return {};
  return std::filesystem::path(path).stem().string();
}

std::string MeshTagFromImportedPath(const std::string& path) {
  if (path.empty())
    return {};
  const std::filesystem::path src(path);
  return (std::filesystem::path("assets/models") / src.filename()).generic_string();
}

}  // namespace Editor
}  // namespace Monolith
