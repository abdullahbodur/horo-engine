#include "renderer/MeshCache.h"

#include <algorithm>
#include <cctype>
#include <string_view>

#include "core/Logger.h"
#include "renderer/ObjLoader.h"

namespace Horo {
    namespace {
        bool EndsWithInsensitive(std::string_view s, std::string_view suffix) {
            if (s.size() < suffix.size())
                return false;
            return std::equal(suffix.rbegin(), suffix.rend(), s.rbegin(),
                              [](char a, char b) {
                                  return std::tolower(static_cast<unsigned char>(a)) ==
                                         std::tolower(static_cast<unsigned char>(b));
                              });
        }

        std::string ResolveRuntimeMeshPath(const std::string &requestedPath) {
            if (EndsWithInsensitive(requestedPath, ".fbx") ||
                EndsWithInsensitive(requestedPath, ".glb") ||
                EndsWithInsensitive(requestedPath, ".gltf")) {
                std::string objPath = requestedPath;
                if (const size_t dot = objPath.find_last_of('.'); dot != std::string::npos)
                    objPath.replace(dot, std::string::npos, ".obj");
                LogWarn("MeshCache::Get — '{}' is not supported at runtime; trying '{}'",
                        requestedPath, objPath);
                return objPath;
            }
            return requestedPath;
        }
    } // namespace

    std::shared_ptr<Mesh> MeshCache::Get(const std::string &path) {
        const std::string resolvedPath = ResolveRuntimeMeshPath(path);

        if (auto it = m_cache.find(resolvedPath); it != m_cache.end())
            return it->second;

        std::shared_ptr<Mesh> mesh;
        try {
            mesh = std::make_shared<Mesh>(ObjLoader::Load(resolvedPath));
        } catch (const ObjLoader::ObjLoaderException &e) {
            LogWarn("MeshCache::Get — failed to load '{}': {}; using fallback box mesh",
                    resolvedPath, e.what());
            mesh = std::make_shared<Mesh>(Mesh::CreateBox());
        }

        return m_cache.try_emplace(resolvedPath, mesh).first->second;
    }
} // namespace Horo
