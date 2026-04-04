#include "editor/EditorAssetImport.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>

#include "renderer/ObjLoader.h"

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
  const std::string stem = src.stem().string();
  return (std::filesystem::path("assets/models") / stem / src.filename()).generic_string();
}

std::string SuggestRenderScale(const std::string& meshTag, float targetHeight)
{
    auto aabb = ObjLoader::ComputeAABB(meshTag);
    if (!aabb.valid)
        return "1.0000,1.0000,1.0000";
    float height = aabb.max.y - aabb.min.y;
    if (height < 1e-6f)
        return "1.0000,1.0000,1.0000";
    float scale = targetHeight / height;
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.4f,%.4f,%.4f", scale, scale, scale);
    return buf;
}

}  // namespace Editor
}  // namespace Monolith
