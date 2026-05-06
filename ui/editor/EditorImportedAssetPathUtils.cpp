#include "ui/editor/EditorImportedAssetPathUtils.h"

#include <cctype>
#include <system_error>

#include "core/ProjectPath.h"
#include "ui/editor/AssetMetadata.h"

namespace Horo::Editor {

namespace {

bool IsValidManagedAssetGuid(std::string_view guid) {
  if (guid.empty())
    return false;

  for (unsigned char ch : guid) {
    if (!std::isalnum(ch) && ch != '-' && ch != '_')
      return false;
  }

  return true;
}

} // namespace

bool IsPathWithinDirectory(const std::filesystem::path &path,
                           const std::filesystem::path &directory) {
  namespace fs = std::filesystem;
  std::error_code ec;
  const fs::path normPath = fs::weakly_canonical(path, ec);
  if (ec)
    return false;
  ec.clear();
  const fs::path normDir = fs::weakly_canonical(directory, ec);
  if (ec)
    return false;

  auto dirIt = normDir.begin();
  auto pathIt = normPath.begin();
  for (; dirIt != normDir.end() && pathIt != normPath.end(); ++dirIt, ++pathIt) {
    if (*dirIt != *pathIt)
      return false;
  }
  return dirIt == normDir.end();
}

std::filesystem::path ResolveProjectAssetPath(std::string_view rawPath) {
  namespace fs = std::filesystem;
  if (rawPath.empty())
    return {};

  const fs::path path(rawPath);
  if (path.is_absolute())
    return {};

  std::error_code ec;
  const fs::path root = fs::weakly_canonical(ProjectPath::Root(), ec);
  if (ec)
    return {};

  return fs::weakly_canonical(root / path, ec);
}

std::filesystem::path GetManagedImportedAssetDirectory(const AssetDef &asset) {
  namespace fs = std::filesystem;
  if (!asset.guid.empty()) {
    if (!IsValidManagedAssetGuid(asset.guid))
      return {};

    const fs::path guidDirectory = GetManagedAssetDirectory(asset.guid);
    std::error_code ec;
    if (fs::exists(guidDirectory, ec) && !ec)
      return guidDirectory;
  }
  if (asset.mesh.empty())
    return {};

  const fs::path meshPath = ResolveProjectAssetPath(asset.mesh);
  if (meshPath.empty())
    return {};

  std::error_code ec;
  const fs::path modelsRoot =
      fs::weakly_canonical(ProjectPath::Root() / "assets" / "models", ec);
  if (ec || !IsPathWithinDirectory(meshPath, modelsRoot))
    return {};

  const fs::path relativeMesh = fs::relative(meshPath, modelsRoot, ec);
  if (ec || relativeMesh.empty())
    return {};

  auto relIt = relativeMesh.begin();
  if (relIt == relativeMesh.end())
    return {};
  const fs::path folder = *relIt;
  ++relIt;
  if (relIt == relativeMesh.end() || folder.empty() || folder == "." ||
      folder == "..")
    return {};

  const fs::path managedDir = modelsRoot / folder;
  if (!asset.albedoMap.empty()) {
    const fs::path albedoPath = ResolveProjectAssetPath(asset.albedoMap);
    if (albedoPath.empty() || !IsPathWithinDirectory(albedoPath, managedDir))
      return {};
  }

  return managedDir;
}

} // namespace Horo::Editor
