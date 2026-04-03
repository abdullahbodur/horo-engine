#include "renderer/MeshCache.h"

#include <algorithm>
#include <cctype>
#include <exception>

#include "core/Logger.h"
#include "renderer/ObjLoader.h"

namespace Horo {

namespace {

static bool EndsWithInsensitive(const std::string& s, const std::string& suffix) {
  if (s.size() < suffix.size())
    return false;
  return std::equal(suffix.rbegin(),
                    suffix.rend(),
                    s.rbegin(),
                    [](char a, char b) {
                      return std::tolower(static_cast<unsigned char>(a)) ==
                             std::tolower(static_cast<unsigned char>(b));
                    });
}

static std::string ResolveRuntimeMeshPath(const std::string& requestedPath) {
  if (EndsWithInsensitive(requestedPath, ".fbx") || EndsWithInsensitive(requestedPath, ".glb") ||
      EndsWithInsensitive(requestedPath, ".gltf")) {
    std::string objPath = requestedPath;
    const size_t dot = objPath.find_last_of('.');
    if (dot != std::string::npos)
      objPath.replace(dot, std::string::npos, ".obj");
    LOG_WARN("MeshCache::Get — '%s' is not supported at runtime; trying '%s'",
             requestedPath.c_str(),
             objPath.c_str());
    return objPath;
  }
  return requestedPath;
}

}  // namespace

std::shared_ptr<Mesh> MeshCache::Get(const std::string& path) {
  const std::string resolvedPath = ResolveRuntimeMeshPath(path);

  auto it = m_cache.find(resolvedPath);
  if (it != m_cache.end())
    return it->second;

  std::shared_ptr<Mesh> mesh;
  try {
    mesh = std::make_shared<Mesh>(ObjLoader::Load(resolvedPath));
  } catch (const std::exception& e) {
    LOG_WARN("MeshCache::Get — failed to load '%s': %s; using fallback box mesh",
             resolvedPath.c_str(),
             e.what());
    mesh = std::make_shared<Mesh>(Mesh::CreateBox());
  }

  m_cache.emplace(resolvedPath, mesh);
  return mesh;
}

}  // namespace Horo
